#pragma once

// ============================================================================
//  AssetBrowserPanel.h - the Assets panel: the files on disk.
//
//  This is what Unity calls the Project window. It shows the folders and files
//  that make up the game, with a thumbnail for every image, and it is where
//  you drag things from:
//
//    * drag an image into the Scene view to place it
//    * drag an image onto an entity to give it that sprite
//    * drag a .cpp onto an entity to attach that script
//    * double-click a scene to open it
//    * "+ Script" writes a new script from the template
//
//  And two things that go the other way - INTO the panel rather than out of
//  it:
//
//    * drag files or folders in from the desktop to IMPORT them. They are
//      copied into whichever folder the cursor is over, or into the folder
//      being browsed. The original is never touched.
//    * drag a tile onto a folder, or onto any step of the path trail at the
//      top, to MOVE it there. Folders can be dragged too, and the path trail
//      is what makes moving something back OUT of a folder possible.
//
//  ==========================================================================
//  ONE ROOT: assets/, and everything in it is yours to arrange.
//
//  Scenes, images and scripts all live in the same tree, in whatever folders
//  suit the game - enemies/, player/, ui/. There is no designated scripts
//  folder, because the editor compiles every .cpp and .h it finds anywhere
//  under assets/, so a script works wherever you decide to put it.
//
//  That is the point of the arrangement: the things that belong to one part of
//  your game stay together, rather than being split across the project by what
//  language they happen to be written in.
//
//  The compiled result does NOT go here - it is written to .build/ beside
//  assets/, so the tree this panel shows contains only files you wrote.
//
//  ==========================================================================
//  THE LISTING IS CACHED AND REFRESHED ON DEMAND, not read every frame.
//  Scanning a folder sixty times a second, for a panel whose contents only
//  change when somebody creates a file, is exactly the sort of cost a tool
//  must not impose on the thing it is meant to be helping with. It refreshes
//  when you navigate, when you create something, and when you press Refresh.
// ============================================================================

#include "Panel.h"

#include <engine/Engine.h>

#include <string>
#include <vector>

namespace editor {

class AssetBrowserPanel final : public Panel {
public:
    AssetBrowserPanel();

    const char* Title() const override { return "Assets"; }
    void        Draw() override;

    // The recorded tile positions are thrown away when the panel stops being
    // visible. They are used to work out which folder a file dragged in from
    // the desktop landed on, and a layout from the last time this panel was
    // on screen - possibly a different folder, possibly a different window
    // size - would answer that question confidently and wrongly.
    void OnHidden() override;

private:
    void Navigate(const std::string& virtualDirectory);
    void Refresh();

    void DrawBreadcrumb();

    // The folder path as a row of buttons - assets / textures / enemies -
    // every one of which can be clicked to go there AND dropped onto to move
    // something there.
    void DrawPathTrail();

    void DrawEntry(const eng::FileSystem::DirEntry& entry, int index);
    void DrawNewScriptPopup();
    void DrawNewTilePopup();
    void DrawNewFolderPopup();

    // Right-click: Open, Show in File Explorer, Rename, Delete.
    void DrawEntryContextMenu(const eng::FileSystem::DirEntry& entry);

    // Both of these are MODAL and both are opened from Draw rather than from
    // inside a tile's id scope - a popup opened while a PushID is active
    // belongs to that id and never appears. Same reason the New Script popup
    // works this way.
    void DrawRenamePopup();
    void DrawDeletePopup();

    // Records what to act on and raises the flag Draw watches. Called from
    // the context menu and from the Delete key.
    void RequestRename(const eng::FileSystem::DirEntry& entry);
    void RequestDelete(const eng::FileSystem::DirEntry& entry);

    // Hands a file to the operating system, reporting either way. See
    // ShellOpen.h for why the reporting matters more than it sounds.
    void OpenInSystemEditor(const std::string& virtualPath);
    void RevealAsset(const std::string& virtualPath);

    // ======================================================================
    //  MOVING THINGS BETWEEN FOLDERS - drag a tile onto a folder, or onto any
    //  step of the path trail above the grid.
    //
    //  THE MOVE IS DEFERRED to the top of the next Draw, for exactly the
    //  reason the engine defers destroying an entity. It is discovered half
    //  way through the loop that is drawing m_entries, and carrying it out on
    //  the spot would rebuild that list while the loop still held a reference
    //  into it. The editor does not get an exemption from the engine's own
    //  rules - see EditorState::requestedScene for the same argument about
    //  loading a scene.
    // ======================================================================
    struct PendingMove {
        std::string from;
        std::string toDirectory;
        bool        requested = false;
    };
    PendingMove m_pendingMove;

    void RequestMove(const std::string& from, const std::string& toDirectory);
    void ApplyPendingMove();

    // Opens a drop target for the item that was just drawn, and moves
    // whatever is let go over it into `targetDirectory`. Every folder tile
    // and every step of the path trail calls this one function, so the two
    // cannot end up disagreeing about what they accept.
    void AcceptMoveInto(const std::string& targetDirectory);

    // ======================================================================
    //  IMPORTING - files dragged in from the desktop or a file explorer.
    //
    //  Nothing about this goes through ImGui. ImGui only knows about drags
    //  that start inside the program; one that starts in Explorer is reported
    //  by the operating system as a position and a list of files, and that is
    //  all there is to work with. So this panel does its own hit testing
    //  against its own window, and draws its own highlight.
    // ======================================================================
    void ImportDroppedFiles();
    void DrawImportHighlight();

    // Is this point, in screen pixels, inside the Assets window? Asked of
    // every dropped file, because one window holds every panel and a file let
    // go of over the Scene view arrives here too.
    bool ContainsPoint(float screenX, float screenY) const;

    // Which folder a point on screen is over, falling back to the folder
    // being browsed when it is over no folder in particular.
    std::string FolderUnderPoint(float screenX, float screenY) const;

    // Where each tile ended up the last time the grid was drawn.
    //
    // Kept because a file dragged in from outside arrives with a POSITION and
    // nothing else: ImGui never saw that drag, so it cannot be asked what was
    // underneath it. Plain floats rather than ImVec2 so this header does not
    // have to include ImGui for four numbers.
    struct Row {
        std::string virtualPath;
        bool        isDirectory = false;
        float       minX = 0.0f;
        float       minY = 0.0f;
        float       maxX = 0.0f;
        float       maxY = 0.0f;
    };
    std::vector<Row> m_rows;

    // What the Delete key and the context menu act on. A virtual path rather
    // than an index, because a refresh reorders the list and an index would
    // then point at a different file.
    std::string m_selectedPath;

    // Writes <the folder being browsed>/<name>.cpp from the template, then
    // compiles and loads it so it can be attached immediately. Returns false
    // with a reason - it refuses to overwrite, because silently replacing
    // somebody's script with a blank template would be unforgivable.
    bool CreateScript(const std::string& name, std::string& outError);

    // Writes <the folder being browsed>/<name>.otile naming `sheet`. Refuses
    // to overwrite, like CreateScript, and for the same reason.
    bool CreateTile(const std::string& name, const std::string& sheet, bool solid,
                    std::string& outError);

    // The assets root is the EMPTY virtual path - see the note in the .cpp.
    std::string                            m_directory;
    std::vector<eng::FileSystem::DirEntry> m_entries;
    bool                                   m_valid = false;

    // The thumbnail image for each entry, in the same order as m_entries. An
    // empty one means "this file is not an image".
    //
    // These are shared_ptrs, so holding them here keeps those images loaded -
    // and letting go of them when you navigate away unloads any that nothing
    // else is using. There is nothing to release by hand.
    std::vector<eng::TextureRef> m_thumbnails;

    char  m_newScriptName[64] = {};
    char  m_newTileName[64]   = {};
    char  m_newTileSheet[256] = {};
    bool  m_newTileSolid      = true;
    bool  m_openNewTile       = false;
    char  m_newFolderName[64] = {};
    bool  m_openNewScript     = false;
    bool  m_openNewFolder     = false;
    char  m_status[256]       = {};

    // Whether m_status is a refusal rather than a confirmation, which decides
    // how it is drawn. A failure shown in the same grey as a success is a
    // failure nobody reads - see ShellOpen.h.
    bool  m_statusIsError     = false;

    // ---- the rename dialog ----------------------------------------------
    std::string m_renamePath;            // what is being renamed
    std::string m_renameOriginal;        // its current name, for the message
    char        m_renameBuffer[128] = {};
    bool        m_openRenamePopup   = false;

    // ---- the delete dialog ------------------------------------------------
    std::string m_deletePath;
    std::string m_deleteName;
    bool        m_deleteIsDirectory = false;
    std::size_t m_deleteCount       = 0;   // how many files are inside a folder
    bool        m_openDeletePopup   = false;
    float m_iconSize          = 72.0f;

};

} // namespace editor
