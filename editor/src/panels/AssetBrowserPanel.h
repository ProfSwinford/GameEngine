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
//  ==========================================================================
//  TWO ROOTS, and the split is not cosmetic:
//
//    assets/       data the game ships with - images and scenes. Loaded while
//                  the game runs, and packaged with it.
//    gamescripts/  C++ SOURCE that the build compiles. Never shipped and never
//                  loaded - it becomes part of the program itself.
//
//  Showing them as two separate roots rather than one tree is what keeps that
//  distinction visible. A browser that showed scripts nested inside assets/
//  would be quietly teaching that source code is data, and the day somebody
//  writes a packaging step, that mistake ships the game's own source with it.
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

private:
    void Navigate(const std::string& virtualDirectory);
    void Refresh();

    void DrawBreadcrumb();
    void DrawEntry(const eng::FileSystem::DirEntry& entry, int index);
    void DrawNewScriptPopup();
    void DrawNewFolderPopup();

    // Writes gamescripts/<name>.cpp from the template. Returns false with a
    // reason - it refuses to overwrite, because silently replacing somebody's
    // script with a blank template would be unforgivable.
    bool CreateScript(const std::string& name, std::string& outError);

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
    char  m_newFolderName[64] = {};
    bool  m_openNewScript     = false;
    bool  m_openNewFolder     = false;
    char  m_status[256]       = {};
    float m_iconSize          = 72.0f;
};

} // namespace editor
