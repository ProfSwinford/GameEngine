#pragma once
// =============================================================================
//  THE ASSET BROWSER - files on disk, not resources in memory.
//
//  The Resource panel from Week 9 lists what is LOADED: handles, refcounts,
//  the M3 evidence. That is a debug view of the resource manager, and it can
//  only ever show you the things a scene already asked for. It cannot answer
//  "what textures do I have" - the question you ask before authoring anything.
//
//  This panel walks the DIRECTORY. It is the difference between an engine you
//  can inspect and an engine you can author in.
//
//  ---------------------------------------------------------------------------
//  TWO ROOTS, and the split is not cosmetic:
//
//    assets/    shipped data - textures, scenes. Loaded at runtime by virtual
//               path, packaged with the game.
//    scripts/   C++ SOURCE that the build compiles. Never shipped, never
//               loaded - it becomes part of the executable.
//
//  Presenting them as two roots rather than one tree is what keeps that
//  distinction visible. A browser that showed scripts/ nested inside assets/
//  would be quietly teaching that source is data, and the day someone writes a
//  packaging step that mistake ships the game's source with the game.
//
//  ---------------------------------------------------------------------------
//  THE LISTING IS CACHED AND REFRESHED ON DEMAND, not read every frame. A
//  directory scan per frame for a panel that changes when the user creates a
//  file is exactly the cost a debug tool must not impose on the thing it is
//  observing - the same argument the Open Scene menu makes for scanning on
//  open rather than continuously. Refresh happens on navigation, on create,
//  and on the Refresh button.
//
//  ---------------------------------------------------------------------------
//  THUMBNAILS ARE REAL LOADS, and that has a consequence worth stating: the
//  browser ACQUIRES a texture handle for every image it shows, so those
//  textures appear in the Resource panel with a refcount the scene did not
//  ask for. That is honest - they really are resident - and the panel releases
//  them when it navigates away. Anyone reading M3's refcount table with this
//  panel open should know why the numbers are higher.
// =============================================================================

#include "Panel.h"

#include <engine/Engine.h>

#include <string>
#include <vector>

namespace editor {

class AssetBrowserPanel final : public Panel {
public:
    AssetBrowserPanel();
    ~AssetBrowserPanel() override;

    const char* Title() const override { return "Assets"; }
    void        Draw() override;

private:
    void Navigate(const std::string& virtualDirectory);
    void Refresh();
    void ReleaseThumbnails();

    void DrawBreadcrumb();
    void DrawEntry(const eng::FileSystem::DirEntry& entry, int index);
    void DrawNewScriptPopup();
    void DrawNewFolderPopup();

    // Creates scripts/<name>.cpp from the template. Returns false with a
    // reason - it refuses to overwrite, because silently replacing someone's
    // script with a fresh template would be unforgivable.
    bool CreateScript(const std::string& name, std::string& outError);

    // The assets root is the EMPTY virtual path - see the note in the .cpp.
    std::string                            m_directory;
    std::vector<eng::FileSystem::DirEntry> m_entries;
    bool                                   m_valid = false;

    // Handles acquired for thumbnails, released on navigation. Parallel to
    // m_entries by index; a null handle means "not an image".
    std::vector<eng::Handle<eng::Texture>> m_thumbnails;

    char m_newScriptName[64] = {};
    char m_newFolderName[64] = {};
    bool m_openNewScript     = false;
    bool m_openNewFolder     = false;

    char m_status[256] = {};
    float m_iconSize   = 72.0f;
};

} // namespace editor
