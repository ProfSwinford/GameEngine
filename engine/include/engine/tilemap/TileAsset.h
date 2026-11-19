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
//  A TILE IS A KIND OF THING, NOT ONE PLACED THING. Every cell painted with
//  grass is the same grass; two crates that behave differently are two .otile
//  files. That is the same bargain a prefab makes, and it is what lets one
//  cell cost a single number - see Tilemap.h.
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
//  TWO KINDS OF ART, AND THE FILE SAYS WHICH BY WHICH KEY IT USES
//
//      { "sheet": "textures/tiles/grass.png" }   five bands, auto-tiled
//      { "image": "textures/tiles/rock.png" }    one picture, drawn whole
//
//  A "sheet" is the five-band sheet AutoTile.h describes: the tile is
//  assembled from four quarters chosen by what is around it, which is what
//  makes a wall round itself off at its ends. A "image" is just a picture -
//  every cell looks the same, nothing is chosen, and one draw call does it
//  instead of four.
//
//  WHY THIS IS TWO KEYS AND NOT A "kind" FIELD. The key IS the kind, so the
//  two cannot disagree and there is no third state to handle. Naming both is
//  refused, naming neither is refused, and the error says what the difference
//  is - which is the question somebody writing their first .otile has.
//
//  ----------------------------------------------------------------------
//  AND IT IS THE ONLY HONEST FIX FOR A BUG THAT LOOKED LIKE A RENDERER BUG.
//
//  Before this, every tile was an auto-tile, so every .otile named a "sheet"
//  and the loader checked the shape: is the image five tiles tall? But the
//  tile size was DERIVED as height / 5, so that check asked "is the height
//  five times a fifth of the height?" - which is true of every image whose
//  height is a multiple of ten. A 48x80 character spritesheet was accepted as
//  a 16px tile with three frames and then drawn as scrambled quarters.
//
//  No measurement can tell a tile sheet from a spritesheet; they are the same
//  shape. What distinguishes them is INTENT, and intent needs somewhere to be
//  written down. That is what "image" is for - and pointing "sheet" at an
//  image the slicer has cut into frames is now refused outright, because
//  slicing one is a statement that it is a spritesheet.
//
//  ==========================================================================
//  THE SMALLEST USEFUL FILE IS ONE LINE
//
//      { "sheet": "textures/tiles/grass.png" }
//
//  because the tile size and the frame count are DERIVED from the image. A
//  sheet is five tiles tall by definition, so the tile size is its height
//  divided by five, and the frame count is its width divided by that. For an
//  image the tile size is simply its height. Stating them again in the file
//  would be a second description of a fact the image already carries, and the
//  two could then disagree.
//
//  They can still be given explicitly, and are then CHECKED against the image
//  rather than believed. A mismatch is reported with both numbers in it.
//
//  ==========================================================================
//  FRAMES GO SIDEWAYS, FOR BOTH KINDS
//
//  Extra columns are extra frames: a strip of squares for an image, a strip of
//  five-band sheets for an auto-tile. "frameMode" decides what they are for -
//  one picture, an animation, or one picked per cell and kept.
//
//  ==========================================================================
//  FIELDS
//
//    "sheet"             the five-band auto-tile sheet. See AutoTile.h.
//    "image"             one picture, drawn whole. Instead of "sheet".
//                        May be a sheet FRAME, e.g. "textures/atlas.png#rock",
//                        which is how one picture out of a sliced spritesheet
//                        becomes a tile without being copied out to a file.
//    "tileSize"          optional. Derived: a sheet's height / 5, an image's
//                        height.
//    "frames"            optional. Derived from the image width / tileSize.
//    "frameMode"         "Single" | "Animation" | "Random"
//    "animationMinSpeed" frames per second, slowest
//    "animationMaxSpeed" frames per second, fastest
//    "connector"         true for an invisible joining tile (see below)
//    "connectsTo"        other .otile paths this tile joins up with
//    "collision"         "None" | "Grid"
//    "components"        [ { "type": "...", ... }, ... ] - see below
//    "editorTint"        [r, g, b, a], applied in the editor only
//
//  ==========================================================================
//  AND ART IS OPTIONAL
//
//  Naming BOTH "sheet" and "image" is refused: they are the two kinds of art,
//  not two settings, and a file naming both has not decided. Naming NEITHER is
//  allowed, and is not a special case - a tile with nothing to draw is still a
//  material. An invisible wall is one (no art, "collision": "Grid"); so is a
//  trigger, a spawn marker, or a tile whose entire job is the components on
//  it.
//
//  THIS USED TO REQUIRE "connector", which confused two unrelated things. A
//  connector JOINS EVERYTHING around it, which is a statement about what its
//  neighbours believe; being invisible is a consequence of what that is for.
//  Having no art is its own fact. A connector is still forced non-solid - a
//  solid one would be an invisible wall along the seam it was added to hide -
//  and an art-less tile that is NOT a connector keeps whatever collision its
//  file asks for, which is how the invisible wall is spelled.
//
//  An art-less tile is given a default "editorTint" so that it can be seen and
//  placed in the editor: magenta for a connector, teal for anything else. A
//  key that is present and EMPTY ("sheet": "") is refused, because that is a
//  deleted path rather than a decision.
// ============================================================================

//  ==========================================================================
//  A TILE CARRIES COMPONENTS, THE SAME WAY AN ENTITY DOES
//
//      "components": [
//        { "type": "BoxCollider2DComponent",
//          "size": [1.0, 0.25], "offset": [0, -0.375], "trigger": true }
//      ]
//
//  Exactly the JSON an entity's component list holds, read by exactly the
//  same ComponentFactory. That is deliberate and not a coincidence: it means
//  a component written years after this file works on a tile the day it is
//  written, and that the Inspector can edit a tile's components with the same
//  widgets it edits an entity's.
//
//  WHAT THIS IS FOR is the difference between "solid" and "a spike". A tile
//  grid full of identical squares can describe a wall; it cannot describe a
//  hazard that hurts, a ladder that is climbable, or a vent that pushes. Those
//  are properties of the MATERIAL, and this is where a material says them.
//
//  --------------------------------------------------------------------------
//  AND THEY BELONG TO THE TILE, NOT TO THE PLACEMENT
//
//  Every cell painted with a tile gets the same components, because a tile IS
//  a kind of thing. Two spikes that do different amounts of damage are two
//  .otile files, in the same way that two enemies that do different amounts of
//  damage are two prefabs. A cell is one 16-bit palette index and has nowhere
//  to hang a difference - see Tilemap.h - and that is what keeps a map of a
//  hundred thousand cells a hundred thousand numbers.
//
//  --------------------------------------------------------------------------
//  THE ASSET HOLDS THE JSON, NOT LIVE COMPONENTS
//
//  A TileAsset is shared by every map that uses it, and a live Component
//  belongs to one entity. So this stores what was written and hands it out;
//  whatever makes it real builds from it. It is validated on load - an entry
//  must be an object naming a "type" - but an UNKNOWN type is a warning and
//  not a refusal, because which components exist depends on which game code
//  is loaded, and a tile is read by tools that load none of it.
//
#include <engine/core/Json.h>
#include <engine/tilemap/AutoTile.h>
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
//  What the art IS - which is not the same question as what it is for.
//
//  There is no ParseTileArt and there is no "kind" field to parse: the file
//  says which of these it is by writing "sheet" or "image", so this is read
//  off the key rather than off a value. See the note at the top of the file.
// ---------------------------------------------------------------------------
enum class TileArt {
    // Four quarters, chosen from five bands by what is around the cell. The
    // original kind, and still the one worth the trouble for anything that
    // forms a surface - ground, water, walls. See AutoTile.h.
    AutoTile,

    // One picture, drawn whole into the cell. Nothing is chosen and nothing
    // looks at a neighbour, so every cell of it is identical - which is what
    // you want for a crate, a bush, a sign, or a single frame lifted out of a
    // spritesheet.
    Image,
};

const char* TileArtName(TileArt art);

// ---------------------------------------------------------------------------
//  How big one tile is, given the art and the image's height.
//
//  THE WHOLE DIFFERENCE BETWEEN THE TWO KINDS, in one line: an auto-tile sheet
//  is five bands tall, an image is one tile tall.
// ---------------------------------------------------------------------------
int TileSizeFor(TileArt art, int imageHeight);

// ---------------------------------------------------------------------------
//  Is an image the right shape to be a tile's art, and how many frames does it
//  hold?
//
//  ==========================================================================
//  WHY THIS IS A FREE FUNCTION TAKING FOUR NUMBERS
//
//  Because the bug it exists to fix cannot otherwise be tested. There is no
//  renderer in the test binary, so no image loads there and nothing can be
//  measured - every shape check inside Deserialize is reached only when the
//  art loaded, which in a test it never does. The same argument AutoTile.h
//  makes for AutoTilePatterns and ImportSettings.h makes for SliceGrid.
//
//  And the case worth testing is a quiet one: a 48x80 character spritesheet
//  used to be accepted as a 16px auto-tile with three frames, because the tile
//  size was derived as height / 5 and the check then asked whether the height
//  was five times a fifth of itself. Every image whose height is a multiple of
//  ten passed. Now the arithmetic sits here, where a test can state the
//  numbers and read the answer.
//
//  `statedFrames` is the file's "frames", or 0 for "work it out from the
//  width". `artPath` appears in the messages, because the person reading one
//  has the file open.
// ---------------------------------------------------------------------------
struct TileArtShape {
    int frames = 1;   // how many sit side by side

    // Empty when the shape is right. A sentence for a person, naming both
    // numbers, because "wrong shape" sends somebody to count pixels.
    std::string problem;

    // Something true and not fatal - the rightmost strip of an image that is
    // not a whole number of tiles wide. Logged by the caller.
    std::string note;

    bool Ok() const { return problem.empty(); }
};

TileArtShape CheckTileArtShape(TileArt art, int tileSize, int imageWidth,
                               int imageHeight, int statedFrames,
                               std::string_view artPath);

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

    const std::string& ArtPath() const { return m_artPath; }
    const TextureRef&  ArtTexture() const { return m_artTexture; }

    // False for a tile that names neither "sheet" nor "image" - a connector,
    // an invisible wall, a marker, a carrier for components. Legal and not
    // rare, so everything that draws has to check this before reaching for the
    // texture. What the editor draws instead is EditorTint, which such a tile
    // is always given one of.
    bool HasArt() const { return !m_artPath.empty(); }

    // Which kind the art is. Asked by everything that draws a tile, because
    // the two are drawn differently - four quarters against one picture.
    TileArt Art() const { return m_art; }

    // The same question, spelled the way the draw loops ask it. A tile with no
    // art at all reports AutoTile, because the kind is read off which key is
    // present and neither is - so this is never the only test, and HasArt
    // comes first everywhere.
    bool IsAutoTile() const { return m_art == TileArt::AutoTile; }

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

    // ---- components -------------------------------------------------------

    // What the file's "components" said, verbatim and in order, each still
    // carrying its own "type". Empty for the great majority of tiles.
    const std::vector<Json>& Components() const { return m_components; }

    bool HasComponents() const { return !m_components.empty(); }

    // The FIRST entry of that type, or nullptr. First and not "the only one",
    // because AllowsSeveral exists and a tile is free to carry two of
    // something; the callers that ask this way want the one that decides
    // something, and the first is the one that does.
    //
    // The pointer is into Components() and lives as long as the asset does -
    // which is until TileLibrary::Clear, or until this tile is reloaded.
    const Json* FindComponent(std::string_view typeName) const;

    // ---- drawing ----------------------------------------------------------
    // ---- drawing ----------------------------------------------------------

    // Which column to draw for a cell. Deterministic in the cell position, so
    // Random mode never shimmers and Animation mode is stable across a reload.
    int FrameForCell(int cellX, int cellY, double seconds) const;

    // Shown over this tile in the editor and nowhere else, to mark out tiles
    // that are otherwise invisible or indistinguishable - a connector, most
    // obviously. Fully transparent means "do not tint", which is the default.
    Color EditorTint() const { return m_editorTint; }

private:
    // The fields a tile with art shares with one without: everything that is
    // not about art. Called from both paths through Deserialize.
    bool DeserializeConnections(const Json& node, std::string& outError);

    // Reads and checks "components". Part of the shared tail above, because a
    // tile with nothing to draw is exactly the one most likely to be carrying
    // them - an invisible trigger is components and nothing else.
    bool DeserializeComponents(const Json& node, std::string& outError);

    std::string m_path;
    std::string m_artPath;
    TextureRef  m_artTexture;
    TileArt     m_art = TileArt::AutoTile;   // what a file with "sheet" means

    int m_tileSize   = 0;
    int m_frameCount = 1;

    TileFrameMode m_frameMode         = TileFrameMode::Single;
    float         m_animationMinSpeed = 4.0f;
    float         m_animationMaxSpeed = 4.0f;

    bool                     m_connector = false;
    std::vector<std::string> m_connectsTo;

    TileCollision m_collision = TileCollision::Grid;

    // Verbatim, in file order. See the section at the top of this file for
    // why this is JSON and not a list of Components.
    std::vector<Json> m_components;

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

    // ======================================================================
    //  DROPS ONE ENTRY, for a file that has been renamed, moved or deleted.
    //
    //  NOTHING WATCHES THE DISK. Load caches by path and hands the cached
    //  copy back for the rest of the session, so a .otile that is no longer
    //  at that path goes on resolving perfectly well from memory - the editor
    //  looks completely fine, and the stale path is written back into the
    //  scene file on the next save. The breakage then surfaces on somebody
    //  else's machine, or on the next launch, with nothing to connect it to
    //  the rename that caused it.
    //
    //  SAFE TO CALL FOR A PATH THAT IS NOT CACHED, and for one that is not a
    //  tile at all: the caller is usually the Assets panel, which does not
    //  know or care what kind of file it has just moved.
    //
    //  A map still holding the dropped TileAsset* keeps working - it is the
    //  CACHE entry that goes, not the object - until the map next refreshes
    //  its palette, which is when it picks up the new path.
    // ======================================================================
    static void Forget(std::string_view virtualPath);

    static std::size_t Count();
    static void        Clear();

    // Every loaded asset, for the editor's palette window.
    static std::vector<const TileAsset*> All();
};

} // namespace eng
