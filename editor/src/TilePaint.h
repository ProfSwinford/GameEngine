#pragma once

// ============================================================================
//  TilePaint.h - what the Scene view needs in order to know that a click means
//  "paint here" rather than "select this", plus the two drawing helpers the
//  Inspector's palette and the tile editor both want.
//
//  THIS USED TO LIVE IN TilePalettePanel.h, and moved out when the palette
//  moved into the Inspector. Nothing here is a panel: ScenePanel and UndoStack
//  included that header only for the state below, which is why it is now its
//  own file and neither of them has to know a palette UI exists at all.
//
//  ==========================================================================
//  THE MAP BEING PAINTED IS THE SELECTED ONE, AND IT IS NOT STORED
//
//  There was a `tilemap` id here, chosen with a combo box in the old panel and
//  guessed by an EnsureTarget() when it went stale. Both are gone. The map is
//  whichever tilemap the SELECTED entity carries, resolved on every ask, which
//  means:
//
//    there is one answer to "which map am I painting into" rather than two
//    that can disagree - the Inspector shows the selected entity, so the
//    palette you are looking at is always the palette you are painting from;
//
//    selecting something that is not a tilemap stops painting, which is the
//    behaviour the feature was asked for by name ("only a selected TileMap
//    should be editable");
//
//    and an entity deleted mid-stroke simply stops resolving, the same way a
//    stale EntityId always has - see scene/EntityId.h.
//
//  ==========================================================================
//  WHY A PAINT TOOL HAS TO BE CHOSEN EXPLICITLY
//
//  The obvious design turns painting on whenever a tilemap is selected. It is
//  also the design that makes the Scene view unpredictable: clicking to select
//  something near a map would paint instead, and the only way to find that out
//  is by doing it.
//
//  So the default tool is Select, which is the editor behaving exactly as it
//  did before tilemaps existed, and painting happens only while a paint tool
//  is actually chosen. The tool is shown over the Scene view while it is
//  active, because a mode you cannot see is a mode you forget you are in.
//
//  ==========================================================================
//  UNDO COVERS PAINTING, AND NOTHING HERE HAD TO BE DONE TO MAKE IT
//
//  Painting works by snapshotting the whole scene document, and a tilemap's
//  cells are part of that document - editor/src/UndoStack.h. No paint tool
//  records anything, and none of them has to: a stroke is one step because the
//  snapshot is taken when the mouse comes up rather than per cell.
// ============================================================================

#include <engine/Engine.h>

#include <string>
#include <vector>

namespace editor {

enum class TileTool {
    Select,      // the editor's ordinary behaviour: click to select, drag handles
    Paint,       // left-drag lays the chosen tile down
    Erase,       // left-drag clears cells
    Rectangle,   // drag a box and fill it on release
    Fill,        // flood-fill the connected region of whatever was clicked
    Pick,        // take the tile under the cursor as the chosen one
};

const char* TileToolName(TileTool tool);

// ---------------------------------------------------------------------------
//  What the Scene view needs to know. One of these for the whole editor.
// ---------------------------------------------------------------------------
struct TilePaintState {
    TileTool tool = TileTool::Select;

    // The .otile being painted with. Empty means nothing is chosen, which the
    // paint tools treat as "do nothing" rather than as "paint a hole".
    //
    // IT IS A PATH AND NOT A TileId, deliberately, even though the brush can
    // only ever paint something already in the map's palette. An id is a
    // POSITION, and removing an earlier entry renumbers every later one - so a
    // remembered id would quietly become a different material. A path cannot
    // be renumbered, and resolving it costs one small walk per stroke.
    std::string tilePath;

    bool showGrid = true;

    // A rectangle drag in progress.
    bool           dragging  = false;
    bool           dragErase = false;
    eng::TileCoord dragStart{};

    // The SELECTED entity's tilemap, or null when nothing selected carries
    // one. Resolved fresh on every call; see the note at the top.
    eng::TilemapComponent* TargetMap() const;

    bool IsPainting() const { return tool != TileTool::Select; }

    static TilePaintState& Get();
};

// ---------------------------------------------------------------------------
//  Shared drawing, used by the Inspector's palette and by the tile editor.
// ---------------------------------------------------------------------------

// A preview of one tile: for an auto-tile the Fill band, which is the tile as
// it looks in the middle of a solid area and so the one that reads as "this
// material"; for an image tile, the picture, which is all there is; and for a
// tile with no art at all, its editor tint as a flat swatch.
void DrawTilePreview(const eng::TileAsset& asset, float size);

// The last part of a path without its extension: "tiles/grass.otile" -> "grass".
std::string TileShortName(const std::string& virtualPath);

// Every .otile under `directory`, found by walking the whole tree. Pass "" for
// the whole project. FileSystem::ListDirectory reads one folder and tiles live
// in sub-folders like everything else, so the walk is done here.
void CollectTiles(const std::string& directory, std::vector<std::string>& out);

} // namespace editor
