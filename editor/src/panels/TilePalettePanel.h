#pragma once

// ============================================================================
//  TilePalettePanel.h - choosing what to paint, and with which tool.
//
//  The panel itself is small. What matters is TilePaintState below, which is
//  the one piece of information the Scene view needs in order to know that a
//  click means "paint here" rather than "select this".
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
//  THERE IS NO UNDO, because this editor has none to hook into.
//
//  Right-dragging erases, which is the usual way of taking a stroke back, and
//  the Scene view says so. That is a stop-gap and it is worth being plain
//  about it rather than letting somebody discover it after a long session.
// ============================================================================

#include "Panel.h"

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
    std::string tilePath;

    // The map being painted into. Held as an ENTITY ID and not a pointer,
    // because the entity can be deleted between one frame and the next and an
    // id can say so; see scene/EntityId.h.
    eng::EntityId tilemap{};

    bool showGrid = true;

    // A rectangle drag in progress.
    // Set by the Assets panel when a .otile is double-clicked or created, so
    // the Tile Palette can bring itself to the front and open that tile for
    // editing. A flag rather than a direct call, so neither panel needs a
    // pointer to the other - the same arrangement EditorState::focusGameView
    // uses for the Toolbar and the Game view.
    bool revealRequested = false;

    bool           dragging  = false;
    bool           dragErase = false;
    eng::TileCoord dragStart{};

    // The map, or null when the id names nothing or that entity has no
    // tilemap on it any more.
    eng::TilemapComponent* TargetMap() const;

    bool IsPainting() const { return tool != TileTool::Select; }

    // Picks a sensible map when none has been chosen: the selected entity's,
    // or the only one in the scene. Returns true if it now has one.
    bool EnsureTarget();

    static TilePaintState& Get();
};

class TilePalettePanel final : public Panel {
public:
    const char* Title() const override { return "Tile Palette"; }
    void        Draw() override;

private:
    void DrawToolRow();
    void DrawTargetRow();
    void DrawTileGrid();
    void DrawMapStats(eng::TilemapComponent& map);

    // ---- editing the chosen .otile ---------------------------------------
    //
    // THE FILE IS THE EDIT BUFFER. The widgets change keys in `m_editJson`,
    // which is the .otile exactly as it was read, and saving writes that back
    // out. Nothing is rebuilt from the loaded TileAsset.
    //
    // That is what preserves everything this panel does not know about -
    // "_comment" keys above all, which every asset in this project uses to
    // explain itself. An editor that silently deleted the explanation from a
    // file each time somebody ticked a checkbox would be worse than no editor.
    void DrawTileEditor();
    void DrawBandStrip(const eng::TileAsset& asset);
    void DrawCombinations(const eng::TileAsset& asset);

    bool LoadEditTarget(const std::string& virtualPath);
    bool SaveEditTarget(std::string& outError);

    // Makes every map using this tile read it again. Changing "connectsTo" or
    // "collision" changes how cells behave without any cell changing, so
    // nothing would otherwise notice.
    static void RefreshTilemapsUsing(const std::string& otilePath);

    void RescanTiles();

    std::vector<std::string> m_tiles;
    bool                     m_scanned = false;
    char                     m_status[256] = {};

    std::string m_editPath;
    eng::Json   m_editJson;
    bool        m_editLoaded = false;
    bool        m_editDirty  = false;
    std::string m_editMessage;

    // The frame the previews show, for an animated or random sheet.
    int  m_previewFrame = 0;
    bool m_showCombinations = false;
};

} // namespace editor
