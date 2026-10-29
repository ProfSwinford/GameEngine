#pragma once

// ============================================================================
//  Tilemap.h - a grid of tiles, and the system that draws it.
//
//  The map holds ONE SMALL NUMBER PER CELL and nothing else. Everything about
//  what a cell looks like - the sheet, the animation, what it joins up with -
//  lives in a TileAsset, and the map refers to those through a PALETTE: a
//  short list of the materials this map uses, with cells storing a position in
//  that list.
//
//  That indirection is what keeps a map small and a material shared. A
//  hundred-thousand-cell map is a hundred thousand 16-bit numbers, not a
//  hundred thousand copies of a file path, and changing how grass looks is one
//  edit rather than one per map.
//
//  ==========================================================================
//  WHY CHUNKS, AND NOT ONE BIG RECTANGLE
//
//  A tilemap has no edges. A player walks left for a while and cells at
//  x = -4000 need to exist; the straightforward "array of width x height"
//  needs its size known up front, and needs reallocating and recentring the
//  first time anybody paints outside it.
//
//  So the grid is stored as CHUNKS of 16x16 that exist only where something
//  has been painted. An empty region costs nothing, a chunk that is emptied is
//  dropped, and coordinates can be negative without anything special. The cost
//  is that reading one cell means finding its chunk first - which is why
//  there is a one-entry cache in front of it, since the next cell read is
//  almost always in the chunk the last one was in.
//
//  ==========================================================================
//  THE TRANSFORM APPLIES, ROTATION INCLUDED
//
//  A tilemap is a component on an entity like anything else, so moving,
//  scaling or turning the entity moves, scales and turns the map. Cell (0, 0)
//  sits at the entity's origin.
//
//  Rotation costs the seam-hiding described in the .cpp, so an UNROTATED map
//  draws with its tile edges snapped to whole pixels and a rotated one does
//  not. Unrotated is the overwhelmingly common case and the one that has to
//  look perfect.
//
//  ==========================================================================
//  NORTH IS +Y
//
//  Cell (0, 1) is ABOVE cell (0, 0), because the world is y-up everywhere in
//  this engine. The tile sheet is an image and counts the other way, and the
//  single place those two meet is QuadrantSourceRect in AutoTile.h.
// ============================================================================

#include <engine/math/Overlap.h>
#include <engine/scene/Component.h>
#include <engine/tilemap/AutoTile.h>
#include <engine/tilemap/TileAsset.h>

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class Camera;

// 16x16, so a chunk is 256 cells and 512 bytes.
//
// Small enough that a map drawn in a corner of the world wastes little, large
// enough that the per-chunk lookup is amortised over plenty of cells, and a
// power of two so the cell-to-chunk division is a shift.
inline constexpr int kTilemapChunkSize = 16;
inline constexpr int kTilemapChunkCells = kTilemapChunkSize * kTilemapChunkSize;

// A cell position, in cells. Signed, because a map has no corner to start at.
struct TileCoord {
    int x = 0;
    int y = 0;

    friend bool operator==(const TileCoord&, const TileCoord&) = default;
};

// ---------------------------------------------------------------------------
//  TilemapComponent
//
//  Scene file fields:
//    "palette":  ["tiles/grass.otile", ...]   the materials, in cell-id order
//    "chunks":   [ { "at": [cx, cy], "cells": "<run-length>" }, ... ]
//    "cellSize": [w, h]    world units per cell; omit to use the tile's size
//    "layer":    0         same meaning as SpriteComponent.layer
//    "tint":     [r,g,b,a] multiplies every tile
// ---------------------------------------------------------------------------
class TilemapComponent final : public Component {
public:
    static constexpr const char* kTypeName = "TilemapComponent";

    ~TilemapComponent() override;

    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    // ---- reading and writing cells ----------------------------------------

    // kEmptyTile for a cell with nothing in it, which is most of the plane.
    TileId GetTile(int x, int y) const;
    TileId GetTile(TileCoord cell) const { return GetTile(cell.x, cell.y); }

    // Writing kEmptyTile erases. A cell set to the value it already holds
    // costs nothing and does NOT bump the revision, so painting over a tile
    // with itself does not rebuild the collider.
    void SetTile(int x, int y, TileId tile);
    void SetTile(TileCoord cell, TileId tile) { SetTile(cell.x, cell.y, tile); }

    void EraseTile(int x, int y) { SetTile(x, y, kEmptyTile); }
    void Clear();

    // Non-empty cells, and the chunks they live in.
    std::size_t TileCount()  const { return m_tileCount; }
    std::size_t ChunkCount() const { return m_chunks.size(); }
    bool        IsEmpty()    const { return m_tileCount == 0; }

    // The painted area, in cells, INCLUSIVE at both ends. False when the map
    // is empty, in which case there is no meaningful answer and the outputs
    // are left alone.
    bool TileBounds(TileCoord& outMin, TileCoord& outMax) const;

    // ---- the palette -------------------------------------------------------

    const std::vector<std::string>& Palette() const { return m_palette; }

    // The asset for a cell value, or nullptr for kEmptyTile, for a value
    // outside the palette, or for an entry whose file would not load.
    const TileAsset* AssetFor(TileId tile) const;

    // The id for a path, adding it to the palette if it is not there yet.
    // Returns kEmptyTile when the asset cannot be loaded - so a failed brush
    // paints nothing rather than painting a hole.
    TileId AddToPalette(std::string_view otilePath);

    // The id for a path, or kEmptyTile when the map does not use it.
    TileId FindInPalette(std::string_view otilePath) const;

    // Drops palette entries no cell uses any more, and renumbers the cells
    // that remain. Called when a map is saved, so a map edited for an hour
    // does not carry a palette of everything that was ever tried in it.
    std::size_t CompactPalette();

    // ---- geometry ----------------------------------------------------------

    // World units per cell. (0, 0) means "ask the first palette entry for its
    // tile size", which is what makes a map with nothing set up draw at the
    // size its art was drawn at.
    Vec2 CellSize() const;
    void SetCellSize(Vec2 size);

    // Whether cellSize was stated in the file or is being derived.
    bool HasExplicitCellSize() const { return m_cellSizeExplicit; }

    // The centre of a cell, and the cell containing a point. Both go through
    // the entity's transform, so they are in world space.
    Vec2      CellToWorld(int x, int y) const;
    TileCoord WorldToCell(Vec2 world) const;

    // The cell's rectangle in the map's OWN space, before the transform. What
    // the collider builds its boxes from.
    AABB CellBoundsLocal(int x, int y) const;

    // ---- appearance --------------------------------------------------------

    int  Layer() const { return m_layer; }
    void SetLayer(int layer) { m_layer = layer; }

    Color Tint() const { return m_tint; }
    void  SetTint(Color tint) { m_tint = tint; }

    // ---- the auto-tile question --------------------------------------------

    // Which of the eight neighbours this cell considers part of itself. Zero
    // for an empty cell. This is the input to ResolveQuadrants.
    //
    // "Part of itself" is not the same as "occupied": it follows the tile's
    // own connectsTo list and treats connector tiles as present. See
    // TileAsset.h.
    unsigned char NeighbourMask(int x, int y) const;

    // Does a cell holding `self` treat a cell holding `other` as a
    // continuation of itself? Reads the table built from the palette, so it is
    // two array indexes rather than a string comparison.
    bool Connects(TileId self, TileId other) const;

    // Bumped every time a cell changes. The collider rebuilds when it sees
    // this move, which is how a painted tile becomes solid without anything
    // having to call the collider.
    unsigned long long Revision() const { return m_revision; }

    // ---- drawing -----------------------------------------------------------

    // `seconds` drives animated tiles; `editorTints` draws TileAsset editor
    // tints and connector markers, which belong in the Scene view and must not
    // appear in the Game view.
    void Render(Camera& camera, double seconds, bool editorTints) const;

    // Reloads every palette entry and rebuilds the connection table. Called
    // when the palette changes, and by the editor after a .otile is edited.
    void RefreshPalette();

private:
    struct Chunk {
        std::array<TileId, kTilemapChunkCells> cells{};
        std::size_t                            used = 0;   // non-empty cells
    };

    struct CoordLess {
        // Row-major: by y, then x. Chosen so a saved file lists chunks in an
        // order somebody reading it can follow.
        bool operator()(const TileCoord& a, const TileCoord& b) const {
            return (a.y != b.y) ? (a.y < b.y) : (a.x < b.x);
        }
    };

    static TileCoord ChunkOf(int x, int y);
    static int       IndexInChunk(int x, int y);

    const Chunk* FindChunk(TileCoord chunk) const;

    bool DeserializeChunks(const Json& node, std::string& outError);

    std::map<TileCoord, Chunk, CoordLess> m_chunks;
    std::size_t                           m_tileCount = 0;
    unsigned long long                    m_revision  = 1;

    std::vector<std::string>      m_palette;
    std::vector<const TileAsset*> m_assets;

    // Row-major (m_palette.size() + 1) square table of "does a tile of id a
    // treat id b as its own?", built by RefreshPalette. One byte per entry
    // rather than std::vector<bool>, because this is read eight times per
    // visible cell and a bit-addressed read is not free.
    std::vector<unsigned char> m_connects;

    Vec2  m_cellSize{0.0f, 0.0f};
    bool  m_cellSizeExplicit = false;
    int   m_layer            = 0;
    Color m_tint             = Color::White();

    // A one-entry cache in front of m_chunks.
    //
    // WHY IT IS WORTH HAVING: working out one cell's appearance reads its eight
    // neighbours, and seven of those eight are in the same chunk for every
    // cell that is not on a chunk edge. Without the cache that is nine tree
    // lookups per cell; with it, usually one.
    //
    // `mutable` because GetTile is logically a read. The cache is invalidated
    // wholesale whenever a chunk is created or destroyed, which is the only
    // thing that can make the stored pointer wrong.
    mutable TileCoord    m_cachedChunkCoord{0, 0};
    mutable const Chunk* m_cachedChunk = nullptr;
};

// ---------------------------------------------------------------------------
//  TilemapRenderSystem
//
//  The same shape as SpriteRenderSystem: components register themselves when
//  attached, and the engine asks the system to draw.
//
//  TILEMAPS AND SPRITES SHARE ONE LAYER ORDERING, which is why this has a
//  RenderRange rather than a Render. A background tilemap at layer -10, the
//  player sprite at 0 and a foreground tilemap at 10 have to interleave, and
//  they cannot if all tilemaps draw before all sprites. Engine::RenderWorld
//  walks the layers in use and alternates between the two systems.
//
//  Within one layer, TILEMAPS DRAW FIRST. A tilemap is scenery and a sprite on
//  the same layer is standing on it.
// ---------------------------------------------------------------------------
class TilemapRenderSystem {
public:
    static void Register(TilemapComponent& tilemap);
    static void Unregister(TilemapComponent& tilemap);

    // Draws every registered tilemap whose layer is in [minLayer, maxLayer).
    static void RenderRange(Camera& camera, int minLayer, int maxLayer,
                            bool editorTints);

    // The distinct layer numbers in use, sorted and without duplicates. What
    // the engine needs to interleave the two systems.
    static void CollectLayers(std::vector<int>& outLayers);

    static std::size_t Count();
    static void        Clear();

    // How many cells the last RenderRange pass drew, and how many it passed
    // over. THE TWO ADD UP to every non-empty cell in every registered map,
    // which is what makes them worth reading: if a map is invisible, they say
    // whether it was culled, whether its tiles would not load, or whether
    // there was nothing there in the first place.
    static std::size_t CellsDrawn();
    static std::size_t CellsSkipped();
    static void        ResetCounters();

    static void RegisterComponentTypes();
};

} // namespace eng
