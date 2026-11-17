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
#include <engine/scene/EntityId.h>
#include <engine/scene/SystemOrder.h>
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
    // that remain.
    //
    // A MANUAL ACTION, not part of saving. The Inspector's "Drop unused"
    // button calls it and nothing on the save path does. There were two
    // buttons for it - the Tile Palette window had a "Tidy palette" that did
    // the same thing - and that window is gone.
    //
    // It used to claim it was called on save, which it never was: Serialize
    // writes the palette verbatim and is const, so it could not compact even
    // if it wanted to. The promise is the wrong way round to keep, too -
    // compaction RENUMBERS every cell, and a save that silently renumbers
    // your data is a worse surprise than a palette with a few abandoned
    // entries in it.
    //
    // IT PINS THE CELL SIZE when that size was being derived; see the note in
    // the implementation for the resize this prevents.
    std::size_t CompactPalette();

    // How many painted cells use this palette entry. Zero for kEmptyTile and
    // for an id past the end, so it is safe to ask about anything.
    //
    // FOR THE QUESTION THE EDITOR HAS TO ASK BEFORE IT REMOVES ONE. Counting
    // is a whole-map walk, so ask it once when something changes rather than
    // from inside a per-frame draw.
    std::size_t CellsUsing(TileId tile) const;

    // =======================================================================
    //  TAKES ONE ENTRY OUT, WHATEVER IS PAINTED WITH IT.
    //
    //  This is the operation an explicit per-map palette needs and
    //  CompactPalette is not: that one drops only what nothing uses, which by
    //  definition can never be the entry somebody is pointing at.
    //
    //  `eraseCells` IS THE WHOLE OF THE SAFETY. False refuses the removal and
    //  answers false while any cell still uses the entry, so a caller that
    //  has not asked first cannot quietly delete somebody's work; true erases
    //  those cells and then removes it. CellsUsing is how a caller finds out
    //  which of the two it is about to be. False for kEmptyTile and for an id
    //  past the end.
    //
    //  EVERY LATER ENTRY IS RENUMBERED, and so is every cell that referred to
    //  one - the palette is positional, and a cell id past the end of it is a
    //  hard scene-load error rather than a blank tile. That is the same walk
    //  CompactPalette does, and the two share it.
    //
    //  IT PINS THE CELL SIZE by the same rule and for the same reason: a map
    //  with no "cellSize" of its own takes it from the first entry that
    //  loads, so removing that entry would otherwise resize the whole map and
    //  move the solid world out from under whatever was standing on it.
    // =======================================================================
    bool RemoveFromPalette(TileId tile, bool eraseCells);

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

    // =======================================================================
    //  TILE OBJECTS - what makes a tile's components real.
    //
    //  A tile that carries components (see TileAsset.h) gets ONE ENTITY PER
    //  PAINTED CELL, parented to this map and sitting at the centre of its
    //  cell, holding those components. A spike tile is then a trigger that
    //  hurts, a ladder is climbable and a vent pushes - none of which a grid
    //  of identical squares can say.
    //
    //  This is Unity's Tile.gameObject, and it is the only honest way to make
    //  arbitrary components work: a component is a thing that lives on an
    //  entity, so for a tile's components to DO anything there has to be an
    //  entity for them to live on.
    //
    //  ----------------------------------------------------------------------
    //  WHAT IT COSTS, AND WHY THAT IS ALL RIGHT
    //
    //  One entity per painted cell OF A TILE THAT CARRIES COMPONENTS, and
    //  nothing at all for every other tile. A ground layer of fifty thousand
    //  grass cells costs zero objects; a level with two hundred spikes costs
    //  two hundred entities, which is nothing. The cheap case stays cheap
    //  because the expensive case is opt-in, one .otile at a time.
    //
    //  SOLID GROUND SHOULD STAY "collision": "Grid" and carry no collider
    //  component. That path merges fifty thousand cells into a handful of
    //  boxes (see TilemapCollider.h); a collider component on a tile is one
    //  box per cell, which is the thing that merge exists to avoid. The two
    //  are for different jobs: Grid for surfaces, components for the sparse
    //  interesting things standing on them.
    //
    //  ----------------------------------------------------------------------
    //  THEY ARE DERIVED, AND SO THEY ARE NEVER SAVED
    //
    //  Scene::SetDerived marks each one, and the save skips it exactly as it
    //  skips a prefab instance's members. The map's cells and the tile's
    //  .otile already say everything about them, so writing them out would
    //  freeze a copy of the tile into every scene using it and break the link
    //  to the asset. Edit the .otile and every object of it is rebuilt.
    //
    //  ----------------------------------------------------------------------
    //  THEY EXIST WHILE THE GAME IS RUNNING
    //
    //  TileObjectSystem drives this, and that is a simulation-stage system,
    //  so it does not run while the editor sits paused. An editor showing two
    //  hundred entities called "Level tile 4,-7" in its hierarchy - which you
    //  cannot usefully edit, because the tile owns them - would be worse than
    //  showing none. What the editor shows instead is the tile's own
    //  component list and its collider preview, in the Inspector.
    // =======================================================================

    // Builds what should exist and takes away what should not. Cheap and
    // safe to call every step: it does nothing at all unless the map has
    // changed since the last pass.
    void SyncTileObjects();

    // Queues every tile object for destruction and forgets them. DEFERRED and
    // not immediate, because this is called from OnDetach - which can itself
    // be running inside the destruction of the entity these are children of,
    // and destroying from inside a destruction is how a tidy-up turns into a
    // crash.
    void ClearTileObjects();

    std::size_t TileObjectCount() const { return m_tileObjects.size(); }

    // The object for one cell, or a null id. For the tests and for anything
    // that wants to find what a cell spawned.
    EntityId TileObjectAt(int x, int y) const;

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

    // THE ONE PLACE A PALETTE ENTRY EVER GOES AWAY. `drop` is one flag per
    // palette entry, in palette order; a cell using a dropped entry is
    // erased, and every entry that survives is renumbered along with the
    // cells that name it. CompactPalette and RemoveFromPalette are each a
    // mask and a call to this, which is what stops the renumbering and the
    // cell-size pinning from existing twice and drifting apart. Answers how
    // many entries went.
    std::size_t DropPaletteEntries(const std::vector<bool>& drop);

    // One cell's object. The TILE IS REMEMBERED ALONGSIDE IT so that a cell
    // repainted from one componented tile to another is rebuilt: the cell is
    // still occupied and still wants an object, so comparing cells alone
    // would leave the old tile's components sitting there.
    struct TileObject {
        EntityId id{};
        TileId   tile = kEmptyTile;
    };

    EntityId BuildTileObject(TileCoord cell, const TileAsset& asset);

    std::map<TileCoord, TileObject, CoordLess> m_tileObjects;

    // The revision the objects were built for. Equal to m_revision means
    // "nothing has happened since", which is the whole of the per-step cost
    // in the common case.
    unsigned long long m_tileObjectRevision = 0;

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

// ---------------------------------------------------------------------------
//  TileObjectSystem - calls SyncTileObjects on every registered map.
//
//  A SIMULATION-STAGE SYSTEM, at the input stage, which decides two things at
//  once. It runs before gameplay, so a spike painted by a script this step is
//  a real trigger before anything looks for one; and it does NOT run while
//  the editor sits with its clock paused, which is what keeps a scene you are
//  editing free of entities you did not put there. See the long note above
//  TilemapComponent::SyncTileObjects.
// ---------------------------------------------------------------------------
class TileObjectSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override { return "TileObjects"; }
    int         Order() const override { return SystemStage::kInput; }
};

} // namespace eng
