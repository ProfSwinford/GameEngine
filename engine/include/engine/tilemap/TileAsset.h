#pragma once

// ============================================================================
//  TileAsset.h - one material, described in a file.
//
//  A tile asset says "this is grass": which sheet its five bands live in, how
//  big a tile is, whether it animates, what it joins up with, and whether it
//  is solid. It is a .otile file, which is JSON, so a new material is a text
//  file rather than a code change - the same rule every other asset in this
//  engine follows.
//
//  ==========================================================================
//  WHY A TILE IS AN ASSET AND NOT PART OF THE MAP
//
//  Because the same grass is used by every map in the game. Put the sheet path
//  and the animation speed in the map and you have copied them into every map,
//  and changing how grass animates becomes an edit to twelve files that have
//  to agree. The map stores a PALETTE - a short list of which tile assets it
//  uses - and then one small number per cell. See Tilemap.h.
//
//  ==========================================================================
//  THE SMALLEST USEFUL FILE IS ONE LINE
//
//      { "sheet": "textures/tiles/grass.png" }
//
//  because the tile size and the frame count are DERIVED from the image. A
//  sheet is five tiles tall by definition, so the tile size is its height
//  divided by five, and the frame count is its width divided by that. Stating
//  them again in the file would be a second description of a fact the image
//  already carries, and the two could then disagree.
//
//  They can still be given explicitly, and are then CHECKED against the image
//  rather than believed. A mismatch is reported with both numbers in it.
//
//  ==========================================================================
//  FIELDS
//
//    "sheet"             required. The five-band sheet. See AutoTile.h.
//    "tileSize"          optional. Derived from the image height / 5.
//    "frames"            optional. Derived from the image width / tileSize.
//    "frameMode"         "Single" | "Animation" | "Random"
//    "animationMinSpeed" frames per second, slowest
//    "animationMaxSpeed" frames per second, fastest
//    "connector"         true for an invisible joining tile (see below)
//    "connectsTo"        other .otile paths this tile joins up with
//    "collision"         "None" | "Grid"
//    "editorTint"        [r, g, b, a], applied in the editor only
// ============================================================================

#include <engine/core/Json.h>
#include <engine/tilemap/AutoTile.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  What the extra columns of a sheet are for.
// ---------------------------------------------------------------------------
enum class TileFrameMode {
    // One column, or ignore the rest. The plain case.
    Single,

    // Cycle through the columns over time. Each cell gets its own speed
    // between the min and the max, and its own starting point, so a field of
    // grass does not pulse in unison - which is the thing that makes animated
    // tiles look wrong.
    Animation,

    // Pick a column per cell and keep it. Breaks up a large flat area without
    // any motion at all.
    //
    // THE CHOICE IS MADE FROM THE CELL COORDINATES, not from a random number
    // generator, so it is the same every time the map is loaded. A map whose
    // stones moved around between runs would be unusable.
    Random,
};

const char*   TileFrameModeName(TileFrameMode mode);
TileFrameMode ParseTileFrameMode(std::string_view name, bool& outRecognized);

// ---------------------------------------------------------------------------
//  Whether a tile is solid, and in what shape.
//
//  Fang also offers a shape traced from the sprite's own pixels. That is not
//  here: it needs an outline extracted from an image, which this engine has no
//  machinery for, and for a tile grid the honest answer is that Grid is what
//  almost everybody wants. See TilemapCollider.h for what Grid builds.
// ---------------------------------------------------------------------------
enum class TileCollision {
    None,   // decoration: nothing collides with it
    Grid,   // solid, filling its cell
};

const char*   TileCollisionName(TileCollision collision);
TileCollision ParseTileCollision(std::string_view name, bool& outRecognized);

// A tile's position in a map's palette. 0 IS ALWAYS EMPTY, so a cleared map is
// a map of zeroes and a palette index is its own "is there anything here?".
using TileId = std::uint16_t;

inline constexpr TileId kEmptyTile = 0;

// ---------------------------------------------------------------------------
//  TileAsset
//
//  Loaded through TileLibrary, which caches it. NOT copyable: the cache hands
//  out pointers to the one instance, and a copy would be a second object that
//  compares unequal to the original, which the connection rules depend on.
// ---------------------------------------------------------------------------
class TileAsset {
public:
    TileAsset() = default;

    TileAsset(const TileAsset&)            = delete;
    TileAsset& operator=(const TileAsset&) = delete;

    // Reads the fields above. `outError` names the field, because the person
    // reading it has the file open.
    //
    // THE SHEET IS LOADED AND MEASURED HERE, which is what makes the derived
    // tile size possible and what catches a sheet of the wrong shape at load
    // time rather than as a screen of scrambled pixels.
    bool Deserialize(const Json& node, std::string& outError);
    bool Serialize(Json& out) const;

    // Where this asset itself came from, e.g. "tiles/grass.otile". Set by
    // TileLibrary; empty for one built by hand in a test.
    const std::string& Path() const { return m_path; }
    void               SetPath(std::string_view path) { m_path.assign(path); }

    const std::string& SheetPath() const { return m_sheetPath; }
    const TextureRef&  Sheet() const { return m_sheet; }

    // False only for a connector, which is invisible and so needs no art.
    // Everything that draws has to check this before reaching for the sheet.
    bool HasSheet() const { return !m_sheetPath.empty(); }

    int TileSize()   const { return m_tileSize; }
    int FrameCount() const { return m_frameCount; }

    TileFrameMode FrameMode()        const { return m_frameMode; }
    float         AnimationMinSpeed() const { return m_animationMinSpeed; }
    float         AnimationMaxSpeed() const { return m_animationMaxSpeed; }

    TileCollision Collision() const { return m_collision; }
    bool          IsSolid()   const { return m_collision != TileCollision::None; }

    // ---- connecting -------------------------------------------------------

    // A connector is INVISIBLE but counts as present to everything around it.
    //
    // What it is for: two tilemaps side by side - two rooms, or a map split so
    // it can be streamed - have nothing of each other to see, so both grow a
    // finished edge down the seam and the join is obvious. A line of connector
    // tiles along the edge of each makes the other side's tiles believe the
    // material continues, and the seam disappears.
    //
    // It is also the answer to "my map has a border of corners all the way
    // round and I wanted it to run off the screen".
    bool IsConnector() const { return m_connector; }

    // Other tile assets this one joins up with, by path. Grass that connects
    // to dirt will round itself off against dirt instead of ending at it.
    //
    // THE RELATION IS ONE-WAY, as it is in Fang. Grass connecting to dirt does
    // not make dirt connect to grass - which is what you want when a path
    // should blend into a lawn but the lawn should still have an edge. Name
    // each other in both files for a mutual join.
    const std::vector<std::string>& ConnectsTo() const { return m_connectsTo; }

    // Does a tile of this material treat `other` as a continuation of itself?
    //
    // Compares by PATH, and is therefore not free. Nothing calls it per cell:
    // a tilemap resolves its whole palette into a lookup table once, and the
    // renderer reads that. See TilemapComponent::RefreshPalette.
    bool ConnectsToAsset(const TileAsset& other) const;

    // ---- drawing ----------------------------------------------------------

    // Which column to draw for a cell. Deterministic in the cell position, so
    // Random mode never shimmers and Animation mode is stable across a reload.
    int FrameForCell(int cellX, int cellY, double seconds) const;

    // Shown over this tile in the editor and nowhere else, to mark out tiles
    // that are otherwise invisible or indistinguishable - a connector, most
    // obviously. Fully transparent means "do not tint", which is the default.
    Color EditorTint() const { return m_editorTint; }

private:
    // The fields a connector shares with an ordinary tile: everything that is
    // not about art. Called from both paths through Deserialize.
    bool DeserializeConnections(const Json& node, std::string& outError);

    std::string m_path;
    std::string m_sheetPath;
    TextureRef  m_sheet;

    int m_tileSize   = 0;
    int m_frameCount = 1;

    TileFrameMode m_frameMode         = TileFrameMode::Single;
    float         m_animationMinSpeed = 4.0f;
    float         m_animationMaxSpeed = 4.0f;

    bool                     m_connector = false;
    std::vector<std::string> m_connectsTo;

    TileCollision m_collision = TileCollision::Grid;

    Color m_editorTint{0, 0, 0, 0};   // transparent: no tint
};

// ---------------------------------------------------------------------------
//  TileLibrary - loads .otile files and hands out the one copy of each.
//
//  The same shape as ResourceManager: ask for a path, get the asset; ask again,
//  get the SAME asset. Two maps using grass share one TileAsset, so comparing
//  two tiles for "is this the same material?" is a pointer comparison.
//
//  POINTERS HANDED OUT HERE STAY VALID UNTIL Clear(). Unlike a texture, a tile
//  asset is not reference-counted: a map holds plain pointers to the assets in
//  its palette, and the library outlives every map in the scene. Clear() is
//  called when the scene is unloaded, by which point no map is left to hold
//  one.
// ---------------------------------------------------------------------------
class TileLibrary {
public:
    // Loads and caches, or returns the already-loaded one. Returns nullptr and
    // logs an error naming the file when it cannot be read - a map with a
    // missing tile asset then draws the rest of itself, rather than nothing.
    static const TileAsset* Load(std::string_view virtualPath);

    // Already loaded? Does not load. For the editor, which lists what it has
    // without wanting to pull every file off disk.
    static const TileAsset* Find(std::string_view virtualPath);

    // Re-reads a file that is already in the cache, IN PLACE, so every map
    // pointing at it picks the change up. This is what makes editing a .otile
    // and seeing the map update possible.
    static bool Reload(std::string_view virtualPath, std::string& outError);

    static std::size_t Count();
    static void        Clear();

    // Every loaded asset, for the editor's palette window.
    static std::vector<const TileAsset*> All();
};

} // namespace eng
