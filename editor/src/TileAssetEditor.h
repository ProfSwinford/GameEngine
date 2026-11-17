#pragma once

// ============================================================================
//  TileAssetEditor.h - editing one .otile, in the Inspector.
//
//  Select a .otile in the Assets panel and this is what the Inspector shows.
//  It used to be a collapsing section at the bottom of the Tile Palette
//  window, reading whichever tile that window's brush happened to be on; the
//  palette moved onto the tilemap that owns it (editor/src/TilePaint.h and
//  InspectorPanel's DrawTilemap), and editing a tile came here, where every
//  other asset in this project is already edited.
//
//  ==========================================================================
//  THE FILE IS THE EDIT BUFFER
//
//  The widgets change keys in `m_editJson`, which is the .otile exactly as it
//  was read, and saving writes that back out. Nothing is rebuilt from the
//  loaded TileAsset, and nothing routes through TileAsset::Serialize.
//
//  That is what preserves everything this editor does not know about -
//  "_comment" keys above all, which every asset in this project uses to
//  explain itself. An editor that silently deleted the explanation from a file
//  each time somebody ticked a checkbox would be worse than no editor.
//  TileAsset::Serialize rebuilds the document from scratch, so it would do
//  exactly that; it has no caller outside the tests for this reason.
//
//  ==========================================================================
//  WHAT IS ON SCREEN IS THE EDIT, NOT THE FILE
//
//  Every frame the buffer is deserialized into a throwaway TileAsset, so the
//  error line and the previews describe what you have typed rather than what
//  was last saved. Typing a wrong sheet path turns the preview red
//  immediately instead of after a save.
// ============================================================================

#include <engine/Engine.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace editor {

class TileAssetEditor {
public:
    // Draws the FIELDS of one of the tile's components - the Inspector's own
    // per-component editors, handed in rather than written here.
    //
    // WHY IT IS A CALLBACK: those widgets belong to the Inspector, and a
    // tile's BoxCollider has to be edited by exactly the same code as an
    // entity's. Writing a second set here would mean every field, tooltip and
    // unit added to one of them quietly missing from the other. Handing the
    // function in also keeps this file from depending on the panel that draws
    // it. See DrawComponentBody in panels/InspectorPanel.h.
    using ComponentBodyDrawer = std::function<void(eng::Component&)>;

    // Draws the editor for one .otile. An empty path says so and draws
    // nothing; a path different from the one in hand is loaded first.
    void Draw(const std::string& virtualPath,
              const ComponentBodyDrawer& drawComponentBody);

    // The tile currently in the buffer, which is not always the one last
    // asked for - a file that would not read leaves the path set and the
    // buffer empty, so that the error can be shown against it.
    const std::string& Path() const { return m_editPath; }

private:
    void DrawImagePreview(const eng::TileAsset& asset);
    void DrawBandStrip(const eng::TileAsset& asset);
    void DrawCombinations(const eng::TileAsset& asset);

    // The tile's "components": the list, the Add menu, and the remove.
    void DrawComponents(const ComponentBodyDrawer& drawComponentBody);

    // One cell, drawn as it will be: the art, the editor tint if there is no
    // art, the solid square a "Grid" tile fills, and every collider component
    // where it will actually sit. THE POINT OF IT is that a collider offset
    // typed as a pair of numbers is unreadable - a picture of where the box
    // lands in the cell is the only way to get a spike's hitbox right without
    // playing the game after every nudge.
    void DrawCellPreview(const eng::TileAsset& asset);

    // Makes one live component per entry in the buffer, so that the
    // Inspector's editors have something to edit. Null where the type is not
    // registered in this build - see TileAsset.h on why that is kept rather
    // than refused.
    void RebuildLiveComponents();

    std::vector<std::unique_ptr<eng::Component>> m_live;

    // What each live component serialized to when it was last written back.
    //
    // WITHOUT THIS the comparison would be against the FILE, and a sparse
    // hand-written entry differs from a full Serialize on the very first
    // frame - so merely selecting a tile would mark it unsaved and expand
    // every entry in it. Comparing against the last snapshot instead means
    // nothing is written until a widget is actually moved.
    std::vector<eng::Json> m_liveSnapshot;

    bool LoadEditTarget(const std::string& virtualPath);
    bool SaveEditTarget(std::string& outError);

    // Makes every map using this tile read it again. Changing "connectsTo" or
    // "collision" changes how cells behave without any cell changing, so
    // nothing would otherwise notice.
    static void RefreshTilemapsUsing(const std::string& otilePath);

    void DrawStatus();

    // Every .otile in the project, for the "connects to" picker only - the
    // one question in this editor whose answer is another tile. Scanned on
    // first use rather than per frame; the Rescan button beside the picker
    // is how a tile added since then gets noticed.
    const std::vector<std::string>& KnownTiles();
    std::vector<std::string>        m_knownTiles;
    bool                            m_knownScanned = false;

    // The last thing a Save or a failure said, kept so it survives the frame
    // the button was pressed in.
    std::string m_status;

    std::string m_editPath;
    eng::Json   m_editJson;
    bool        m_editLoaded = false;
    bool        m_editDirty  = false;
    std::string m_editMessage;

    // The frame the previews show, for an animated or random sheet.
    int  m_previewFrame     = 0;
    bool m_showCombinations = false;
};

} // namespace editor
