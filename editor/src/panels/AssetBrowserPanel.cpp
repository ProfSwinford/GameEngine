// ============================================================================
//  AssetBrowserPanel.cpp - the Assets panel. See AssetBrowserPanel.h for the
//  two-roots argument.
// ============================================================================

#include "panels/AssetBrowserPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ShellOpen.h"
#include "ScriptBuild.h"
#include "ScriptTemplate.h"
#include "panels/TilePalettePanel.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <cstdio>

namespace editor {
namespace {

namespace fs = std::filesystem;

// ONE root: assets/. Scenes, images and scripts all live in it, in whatever
// folder structure suits the game.
//
// THE ASSETS ROOT IS THE EMPTY STRING, and that is not a shortcut. Virtual
// paths are ALREADY relative to assets/ - "scenes/level1.oscene" resolves to
// <project>/assets/scenes/level1.oscene - so the virtual path OF assets/ itself
// is "". Writing "assets" here would ask for <project>/assets/assets.
constexpr const char* kRootAssets = "";

// What to show at the top of the panel. "" is a real virtual path and a
// terrible thing to display.
std::string DisplayPath(const std::string& directory) {
    return directory.empty() ? "assets" : "assets/" + directory;
}

// A folder is not a KIND of asset, so its colour does not belong in
// ColourFor's switch. It is here because two places need it: the tile itself,
// and the little preview that follows the cursor while one is being dragged.
ImVec4 FolderColour() {
    return ImVec4(0.85f, 0.75f, 0.45f, 1.0f);
}

ImVec4 ColourFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return ImVec4(0.55f, 0.78f, 0.95f, 1.0f);
        case AssetKind::Scene:   return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
        case AssetKind::Script:  return ImVec4(0.65f, 0.90f, 0.65f, 1.0f);
        case AssetKind::Font:    return ImVec4(0.88f, 0.70f, 0.95f, 1.0f);
        case AssetKind::Tile:    return ImVec4(0.55f, 0.92f, 0.85f, 1.0f);
        case AssetKind::Unknown: break;
    }
    return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
}

const char* LabelFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return "IMG";
        case AssetKind::Scene:   return "SCN";
        case AssetKind::Script:  return "CPP";
        case AssetKind::Font:    return "TTF";
        case AssetKind::Tile:    return "TILE";
        case AssetKind::Unknown: break;
    }
    return "---";
}

// Joined carefully rather than concatenated, because the assets root IS the
// empty string and "" + "/" + name would start with a slash - which means
// something quite different.
std::string JoinPath(const std::string& directory, const std::string& leaf) {
    return directory.empty() ? leaf : directory + "/" + leaf;
}

std::string ParentOf(const std::string& directory) {
    const std::size_t slash = directory.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : directory.substr(0, slash);
}

std::string LeafOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------------------
//  COULD THIS MOVE ACTUALLY HAPPEN? Asked before the target lights up.
//
//  FileSystem::Move is the authority and refuses all of this for real, so
//  nothing here is load-bearing. It exists because a target that highlights
//  invitingly and then declines on release looks exactly like a broken
//  feature - worse than one that never lit up at all, because the user now
//  believes they did it wrong. Same argument as HierarchyPanel's
//  SelectionReparentIsLegal.
//
//  The Exists() call is affordable because ImGui only reaches a drop target
//  for the ONE item the cursor is actually over. This is not run per tile.
// ---------------------------------------------------------------------------
bool MoveLooksLegal(const std::string& from, const std::string& toDirectory) {
    if (from.empty()) {
        return false;   // the assets root itself
    }

    // A folder cannot go inside itself or inside its own children. The
    // separator test is what makes this exact: "art" is a prefix of both
    // "art/ui" and "artwork", and only the first is inside it.
    if (toDirectory == from ||
        (toDirectory.size() > from.size() && toDirectory.starts_with(from) &&
         toDirectory[from.size()] == '/')) {
        return false;
    }

    if (ParentOf(from) == toDirectory) {
        return false;   // already in there; the drag would achieve nothing
    }

    // Moving onto a name that is taken would mean overwriting it, which is
    // never what dragging something somewhere is asking for.
    return !eng::FileSystem::Exists(JoinPath(toDirectory, LeafOf(from)));
}

} // namespace

AssetBrowserPanel::AssetBrowserPanel() {
    Refresh();
}

void AssetBrowserPanel::Navigate(const std::string& virtualDirectory) {
    m_directory = virtualDirectory;
    Refresh();
}

void AssetBrowserPanel::Refresh() {
    // Clearing the old thumbnails lets go of this panel's share of those
    // images. Any that no sprite in the scene is also using will unload
    // themselves at this point - see render/Texture.h.
    m_thumbnails.clear();

    m_valid = eng::FileSystem::ListDirectory(m_directory, m_entries);
    if (!m_valid) {
        m_entries.clear();
    }

    m_thumbnails.resize(m_entries.size());
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].isDirectory ||
            ClassifyAsset(m_entries[i].virtualPath) != AssetKind::Texture) {
            continue;
        }
        m_thumbnails[i] = eng::ResourceManager::LoadTexture(m_entries[i].virtualPath);
    }
}

void AssetBrowserPanel::DrawBreadcrumb() {
    // "Up" is greyed out AT the root rather than hidden, so the toolbar keeps
    // its shape as you navigate and the buttons stay where your hand expects.
    //
    // Being "at the root" is a comparison against the root itself, not a test
    // for an empty parent - because the assets root IS the empty string, and
    // an empty-parent test would grey Up out inside every first-level folder.
    ImGui::BeginDisabled(m_directory == kRootAssets);
    if (ImGui::SmallButton("Up")) {
        Navigate(ParentOf(m_directory));
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        Refresh();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("+ Script")) {
        m_openNewScript = true;
        std::snprintf(m_newScriptName, sizeof(m_newScriptName), "%s", "NewScript");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Tile")) {
        m_openNewTile = true;

        // Prefilled from whatever is selected, because the usual way to reach
        // this button is "I have just imported a sheet and I want a tile for
        // it". Having to retype the path you are looking at is the kind of
        // small friction that makes a tool feel like paperwork.
        m_newTileSheet[0] = '\0';
        m_newTileName[0]  = '\0';
        if (ClassifyAsset(m_selectedPath) == AssetKind::Texture) {
            std::snprintf(m_newTileSheet, sizeof(m_newTileSheet), "%s",
                          m_selectedPath.c_str());

            const std::size_t slash = m_selectedPath.find_last_of('/');
            std::string       stem  = (slash == std::string::npos)
                                          ? m_selectedPath
                                          : m_selectedPath.substr(slash + 1);
            if (const std::size_t dot = stem.find_last_of('.');
                dot != std::string::npos) {
                stem.erase(dot);
            }
            std::snprintf(m_newTileName, sizeof(m_newTileName), "%s", stem.c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Make a .otile from a five-band tile sheet.\n"
                          "Select the sheet first and its path is filled in.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("+ Folder")) {
        m_openNewFolder    = true;
        m_newFolderName[0] = '\0';
    }

    // Building scripts by hand. It normally happens on its own when the editor
    // regains focus, but having a button matters: it makes the step visible
    // rather than magic, and it gives you a way to retry after fixing a
    // compile error without alt-tabbing out and back.
    ImGui::SameLine();
    ImGui::BeginDisabled(!ScriptBuild::HasCompiler());
    if (ImGui::SmallButton("Build Scripts")) {
        // Started, not waited for - the editor keeps drawing while the
        // compiler runs. EditorApp collects the result once it is ready and
        // puts the summary in the menu bar. See ScriptBuild.h.
        if (ScriptBuild::BeginAsync()) {
            std::snprintf(m_status, sizeof(m_status), "building scripts...");
        } else if (ScriptBuild::IsBuilding()) {
            std::snprintf(m_status, sizeof(m_status), "already building");
        }
        Refresh();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ScriptBuild::HasCompiler()
                                    ? ScriptBuild::CompilerDescription().c_str()
                                    : "no C++ compiler was found on this machine");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("##icon", &m_iconSize, 40.0f, 128.0f, "icon %.0f");

    DrawPathTrail();
}

void AssetBrowserPanel::DrawPathTrail() {
    // ========================================================================
    //  THE PATH, AS BUTTONS. This line used to be plain grey text saying
    //  where you were. It now says the same thing and does two more jobs:
    //  every step jumps straight to that folder, and every step ACCEPTS A
    //  DROP.
    //
    //  The second one is what makes moving something back OUT of a folder
    //  possible at all. Without it the only route out is to navigate up
    //  first - and by then the thing you wanted to move is no longer on
    //  screen to pick up. A one-way reorganiser is not a reorganiser.
    // ========================================================================
    if (ImGui::SmallButton("assets")) {
        Navigate(kRootAssets);
    }
    AcceptMoveInto(kRootAssets);

    std::string walked;
    std::size_t start = 0;
    while (start < m_directory.size()) {
        const std::size_t slash = m_directory.find('/', start);
        const std::string part  = (slash == std::string::npos)
                                      ? m_directory.substr(start)
                                      : m_directory.substr(start, slash - start);
        walked = walked.empty() ? part : walked + "/" + part;

        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("/");
        ImGui::SameLine(0.0f, 4.0f);

        // PushID by depth, because a label is an identity in ImGui and a
        // project with ui/ inside enemies/ui/ would otherwise have two
        // buttons that are the same button.
        ImGui::PushID(static_cast<int>(start));
        if (ImGui::SmallButton(part.c_str())) {
            Navigate(walked);
        }
        AcceptMoveInto(walked);
        ImGui::PopID();

        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
}

void AssetBrowserPanel::DrawEntry(const eng::FileSystem::DirEntry& entry, int index) {
    const AssetKind kind =
        entry.isDirectory ? AssetKind::Unknown : ClassifyAsset(entry.virtualPath);

    // PushID(index) makes every widget in this tile unique, so twenty tiles
    // drawn by the same code are twenty separate things as far as ImGui is
    // concerned. BeginGroup makes the icon and its label behave as one item
    // for layout purposes.
    ImGui::PushID(index);
    ImGui::BeginGroup();

    const ImVec2 iconSize(m_iconSize, m_iconSize);

    // The thumbnail, or a coloured tile with a three-letter type on it. A tile
    // rather than blank space, so every entry is the same size and the grid
    // does not go ragged.
    bool drewImage = false;
    if (index < static_cast<int>(m_thumbnails.size()) && m_thumbnails[index] &&
        m_thumbnails[index]->native != nullptr) {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_thumbnails[index]->native), iconSize);
        drewImage = true;
    }
    if (!drewImage) {
        const ImVec4 colour = entry.isDirectory ? FolderColour() : ColourFor(kind);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(colour.x * 0.35f, colour.y * 0.35f,
                                     colour.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::Button(entry.isDirectory ? "DIR" : LabelFor(kind), iconSize);
        ImGui::PopStyleColor(2);
    }

    const bool iconHovered = ImGui::IsItemHovered();
    const bool iconClicked = iconHovered && ImGui::IsMouseDoubleClicked(0);

    // ---- this tile is a DRAG SOURCE --------------------------------------
    //
    // EVERYTHING IS DRAGGABLE, folders included. It used to be only the kinds
    // that meant something to an entity, which was the right rule while a
    // drag could only ever end ON an entity. A drag can now also end on a
    // folder, and something that cannot be picked up cannot be reorganised -
    // which would have left folders, .json files and .txt files as the only
    // things in the project that could not be tidied away.
    const char* payloadId = entry.isDirectory ? kPayloadAssetItem : PayloadIdFor(kind);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        // The virtual path is what gets carried, including its terminating
        // zero so the receiving end can use it directly as a C string. ImGui
        // takes a COPY of it, which matters: passing a pointer into m_entries
        // would dangle the moment a refresh happened mid-drag.
        ImGui::SetDragDropPayload(payloadId, entry.virtualPath.c_str(),
                                  entry.virtualPath.size() + 1);

        // What the cursor carries while dragging. Worth the few lines: a drag
        // with no feedback feels broken even when it is working perfectly.
        ImGui::TextColored(entry.isDirectory ? FolderColour() : ColourFor(kind), "%s",
                           entry.isDirectory ? "DIR" : LabelFor(kind));
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.name.c_str());
        if (!entry.isDirectory && kind == AssetKind::Script) {
            ImGui::TextDisabled("drop on an entity to attach it");
        } else if (!entry.isDirectory && kind == AssetKind::Texture) {
            ImGui::TextDisabled("drop in the Scene view to place it");
        }
        ImGui::TextDisabled("drop on a folder, or on the path above, to move it");
        ImGui::EndDragDropSource();
    }

    // The name under the tile, wrapped to the tile's width so that one long
    // filename does not push the whole grid apart.
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + m_iconSize);
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    // ---- where this tile ended up ----------------------------------------
    //
    // Recorded for the one job ImGui cannot help with: a file dragged in from
    // Explorer is reported by the operating system as a position, and working
    // out what was under that position means having the positions. Taken
    // after EndGroup so it covers the icon AND its label, which is what the
    // user is aiming at.
    //
    // ONLY IF IT IS ACTUALLY ON SCREEN. A tile scrolled past the bottom of
    // the grid is still submitted to ImGui and still has a position - it is
    // simply clipped - so recording it unconditionally would let a file
    // dropped near the panel's bottom edge be filed into a folder the user
    // cannot even see. That is a wrong answer delivered with confidence,
    // which is the worst kind.
    if (ImGui::IsItemVisible()) {
        const ImVec2 tileMin = ImGui::GetItemRectMin();
        const ImVec2 tileMax = ImGui::GetItemRectMax();
        m_rows.push_back(Row{entry.virtualPath, entry.isDirectory, tileMin.x, tileMin.y,
                             tileMax.x, tileMax.y});
    }

    // ---- and a FOLDER is a drop target -----------------------------------
    //
    // Only a folder. Dropping one file onto another has no meaning here, and
    // a target that accepts everything and acts on almost nothing is how a
    // panel starts feeling unpredictable.
    if (entry.isDirectory) {
        AcceptMoveInto(entry.virtualPath);
    }

    if (iconHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.virtualPath.c_str());
        if (entry.isDirectory) {
            ImGui::TextDisabled("double-click to open, drag things onto it to move "
                                "them in");
        } else {
            ImGui::TextDisabled("%llu bytes", entry.byteSize);
        }
        switch (kind) {
            case AssetKind::Texture:
                ImGui::TextDisabled("drag into the Scene view, or onto an entity");
                break;
            case AssetKind::Font:
                ImGui::TextDisabled("drag onto the Font field of a UI Text");
                break;
            case AssetKind::Script:
                ImGui::TextDisabled("drag onto an entity in the Hierarchy or Inspector");
                if (!eng::ScriptRegistry::IsRegistered(
                        ScriptNameFromPath(entry.virtualPath))) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                                       "not compiled into this build yet");
                }
                break;
            case AssetKind::Scene:
                ImGui::TextDisabled("double-click to open");
                break;
            case AssetKind::Tile:
                ImGui::TextDisabled("double-click to edit it in the Tile Palette");
                break;
            case AssetKind::Unknown:
                break;
        }
        ImGui::EndTooltip();
    }

    // A SINGLE CLICK SELECTS, which is what gives the Delete key something to
    // act on. Taken on mouse-DOWN rather than release, so a click that turns
    // into a drag still selects what it started from.
    if (iconHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_selectedPath = entry.virtualPath;
    }

    // The selected tile gets a border. A selection you cannot see is worse
    // than none at all, because the Delete key would then act on something
    // invisible.
    if (m_selectedPath == entry.virtualPath) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                            ImGui::GetItemRectMax(),
                                            IM_COL32(120, 190, 250, 255), 3.0f, 0, 2.0f);
    }

    DrawEntryContextMenu(entry);

    if (iconClicked) {
        if (entry.isDirectory) {
            Navigate(entry.virtualPath);
        } else if (kind == AssetKind::Scene) {
            // Recorded rather than loaded here - loading destroys every
            // entity, and other panels are still using them this frame. See
            // EditorState::requestedScene.
            EditorState::Get().requestedScene = entry.virtualPath;
        } else if (kind == AssetKind::Tile) {
            // A .otile is JSON, so the system editor WOULD open it - but the
            // Tile Palette can show the sheet, every one of the 47 shapes it
            // produces, and the settings as checkboxes. Opening Notepad on it
            // instead would be the least useful of the available answers.
            TilePaintState& tiles = TilePaintState::Get();
            tiles.tilePath        = entry.virtualPath;
            tiles.revealRequested = true;
        } else {
            // EVERYTHING ELSE OPENS IN WHATEVER THE SYSTEM USES FOR IT - a
            // .cpp in your C++ editor, a .png in your image viewer. That is
            // what a double-click means everywhere else, and a panel where
            // it works on folders and scenes and does nothing at all on a
            // script reads as broken rather than as a deliberate limit.
            OpenInSystemEditor(entry.virtualPath);
        }
    }

    ImGui::PopID();
}

void AssetBrowserPanel::DrawEntryContextMenu(const eng::FileSystem::DirEntry& entry) {
    // BeginPopupContextItem opens on a right-click of the PREVIOUS item, so
    // this has to come straight after the tile's group.
    if (!ImGui::BeginPopupContextItem("asset-actions")) {
        return;
    }

    // Right-clicking selects first. Without this the menu would act on
    // whatever was selected BEFORE, so right-clicking one file and choosing
    // Delete would delete a different one - about as bad as a context menu
    // can get.
    m_selectedPath = entry.virtualPath;

    ImGui::TextDisabled("%s", entry.name.c_str());
    ImGui::Separator();

    if (entry.isDirectory) {
        if (ImGui::MenuItem("Open Folder")) {
            Navigate(entry.virtualPath);
        }
    } else if (ClassifyAsset(entry.virtualPath) == AssetKind::Scene) {
        // A scene opens IN THE EDITOR, because that is obviously what you
        // want - the system would hand it to a text editor.
        if (ImGui::MenuItem("Open Scene")) {
            EditorState::Get().requestedScene = entry.virtualPath;
        }
        if (ImGui::MenuItem("Open File")) {
            OpenInSystemEditor(entry.virtualPath);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens the scene's JSON in a text editor, if this "
                              "machine has one set up for .oscene files.");
        }
    } else {
        if (ImGui::MenuItem("Open File")) {
            OpenInSystemEditor(entry.virtualPath);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens it in whatever this machine uses for that kind "
                              "of file - the same as double-clicking it.");
        }
    }

    if (ImGui::MenuItem("View in File Explorer")) {
        RevealAsset(entry.virtualPath);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Always works, even for file types nothing is set up to "
                          "open.");
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Rename...", "F2")) {
        RequestRename(entry);
    }
    if (ImGui::MenuItem("Delete...", "Del")) {
        RequestDelete(entry);
    }

    ImGui::EndPopup();
}

void AssetBrowserPanel::OpenInSystemEditor(const std::string& virtualPath) {
    std::string error;
    if (OpenExternally(virtualPath, error)) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "opened %s", virtualPath.c_str());
        return;
    }

    // REPORTED TWICE, AND AT ERROR RATHER THAN WARNING.
    //
    // This is the whole fix for "double-clicking a file does nothing": the
    // project's log threshold throws warnings away, so a warning here would
    // reach neither the Console nor the log file. See ShellOpen.h.
    m_statusIsError = true;
    std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
    ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", virtualPath,
                     error);
}

void AssetBrowserPanel::RevealAsset(const std::string& virtualPath) {
    std::string error;
    if (RevealInFolder(virtualPath, error)) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "showed %s in the file explorer",
                      virtualPath.c_str());
        return;
    }
    m_statusIsError = true;
    std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
    ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not show '{}': {}", virtualPath,
                     error);
}

void AssetBrowserPanel::RequestRename(const eng::FileSystem::DirEntry& entry) {
    m_renamePath      = entry.virtualPath;
    m_renameOriginal  = entry.name;
    m_openRenamePopup = true;
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", entry.name.c_str());
}

void AssetBrowserPanel::RequestDelete(const eng::FileSystem::DirEntry& entry) {
    m_deletePath        = entry.virtualPath;
    m_deleteName        = entry.name;
    m_deleteIsDirectory = entry.isDirectory;
    m_openDeletePopup   = true;
    m_deleteCount       = 0;

    // HOW MUCH IS IN THERE, counted up front so the question can be specific.
    // "Delete this folder and the 23 files in it?" can be answered; "are you
    // sure?" cannot.
    if (entry.isDirectory) {
        std::error_code ec;
        const fs::path  real = eng::FileSystem::Resolve(entry.virtualPath);
        for (const auto& item : fs::recursive_directory_iterator(real, ec)) {
            if (!item.is_directory(ec)) {
                ++m_deleteCount;
            }
        }
    }
}

void AssetBrowserPanel::DrawRenamePopup() {
    if (m_openRenamePopup) {
        ImGui::OpenPopup("Rename Asset");
        m_openRenamePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Rename Asset", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Rename '%s' to:", m_renameOriginal.c_str());

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##assetname", m_renameBuffer, sizeof(m_renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // ---- renaming a script is not just a file operation ------------------
    //
    // Said BEFORE it happens rather than discovered afterwards. The class
    // inside a .cpp keeps its old name, and the scene refers to the CLASS -
    // so renaming the file alone changes nothing about what runs.
    if (ClassifyAsset(m_renamePath) == AssetKind::Script) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "This only renames the FILE. The class inside it keeps its");
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "name, and that is what scenes refer to.");
    }

    const bool blank = m_renameBuffer[0] == '\0';
    ImGui::BeginDisabled(blank);
    if ((submitted && !blank) || ImGui::Button("Rename", ImVec2(120, 0))) {
        std::string error;
        if (eng::FileSystem::Rename(m_renamePath, m_renameBuffer, error)) {
            m_statusIsError = false;
            std::snprintf(m_status, sizeof(m_status), "renamed to %s", m_renameBuffer);
            m_selectedPath.clear();
            Refresh();
        } else {
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not rename: {}", error);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawDeletePopup() {
    if (m_openDeletePopup) {
        ImGui::OpenPopup("Delete Asset");
        m_openDeletePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Delete Asset", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // ALWAYS ASKS, and this is the one dialog in the editor that genuinely
    // has to. Deleting an entity goes through the undo stack; deleting a FILE
    // does not and cannot - it is gone from the disk.
    if (m_deleteIsDirectory) {
        ImGui::Text("Delete the folder '%s'", m_deleteName.c_str());
        ImGui::Text("and the %zu file(s) inside it?", m_deleteCount);
    } else {
        ImGui::Text("Delete '%s'?", m_deleteName.c_str());
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                       "This cannot be undone - Ctrl+Z does not reach the disk.");

    if (ClassifyAsset(m_deletePath) == AssetKind::Script) {
        ImGui::TextDisabled("Any entity using this script will show it as missing.");
    } else if (ClassifyAsset(m_deletePath) == AssetKind::Texture) {
        ImGui::TextDisabled("Any sprite using it will draw the missing-texture square.");
    }

    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
        std::size_t removed = 0;
        std::string error;
        if (eng::FileSystem::Remove(m_deletePath, removed, error)) {
            m_statusIsError = false;
            std::snprintf(m_status, sizeof(m_status), "deleted %s (%zu item(s))",
                          m_deleteName.c_str(), removed);
            ENGINE_LOG_INFO(eng::Channels::kEditor, "deleted '{}' ({} item(s))",
                            m_deletePath, removed);
            m_selectedPath.clear();
            Refresh();
        } else {
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not delete: {}", error);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::Draw() {
    // ========================================================================
    //  BOTH OF THESE COME FIRST, before anything reads m_entries.
    //
    //  Each one changes what is in the folder, and each one is discovered at
    //  a moment when that list is already being walked - a move half way
    //  through the tile loop, an import from an event that arrived before the
    //  frame began. Doing the work here, at the top, is the same deferral the
    //  engine makes for destroying an entity and the editor makes for loading
    //  a scene: find out now, act at a point where nothing is mid-sentence.
    // ========================================================================
    ApplyPendingMove();
    ImportDroppedFiles();

    DrawBreadcrumb();
    ImGui::Separator();

    if (!m_valid) {
        // A folder can legitimately be missing - it may have been deleted from
        // outside the editor while you were looking at it - so this is an
        // instruction rather than an error.
        ImGui::TextDisabled("'%s' does not exist yet.", DisplayPath(m_directory).c_str());
        ImGui::TextDisabled("Press Up to go back, or Refresh.");
        DrawNewScriptPopup();
        DrawNewTilePopup();
        DrawNewFolderPopup();
        return;
    }

    // A grid that wraps, laid out by hand from the available width. ImGui has
    // no automatic flow layout, so this is the usual way to build one:
    // work out how many fit across, then call SameLine() after each tile
    // except the last in a row.
    const float cellWidth = m_iconSize + ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int   columns   = std::max(1, static_cast<int>(available / cellWidth));

    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);

    // Rebuilt from scratch every frame, because a scroll, a resize or a
    // change to the icon slider moves every tile. A half-updated list of
    // positions is worse than none: it would send a dropped file confidently
    // into the wrong folder.
    m_rows.clear();

    int column = 0;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        DrawEntry(m_entries[i], static_cast<int>(i));
        if (++column % columns != 0 && i + 1 < m_entries.size()) {
            ImGui::SameLine();
        }
    }
    if (m_entries.empty()) {
        ImGui::TextDisabled("this folder is empty");
    }

    // ---- the keys this panel owns ---------------------------------------
    //
    // Checked INSIDE the grid's child window, because that is what has focus
    // when you have just clicked a tile.
    //
    // Three guards, each earning its place. The panel must be FOCUSED, or
    // Delete pressed while working in the Inspector would delete a file.
    // WantTextInput must be false, or Delete while typing in the rename box
    // would delete the thing being renamed - which is a genuinely alarming
    // thing for a rename box to do. And something must actually be selected.
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !io.WantTextInput && !m_selectedPath.empty()) {

        const auto selected = std::find_if(
            m_entries.begin(), m_entries.end(),
            [&](const eng::FileSystem::DirEntry& e) {
                return e.virtualPath == m_selectedPath;
            });

        if (selected != m_entries.end()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                RequestDelete(*selected);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                RequestRename(*selected);
            }
        } else {
            // The selection names something that is no longer in this folder
            // - deleted, renamed, or we navigated away. Dropped rather than
            // left pointing at nothing.
            m_selectedPath.clear();
        }
    }

    ImGui::EndChild();

    // AFTER the grid, so m_rows holds this frame's positions and the outline
    // round the folder under the cursor is drawn over the tile rather than
    // under it.
    DrawImportHighlight();

    DrawNewScriptPopup();
    DrawNewTilePopup();
    DrawNewFolderPopup();

    // OPENED FROM HERE, not from inside a tile. A modal opened while that
    // tile's PushID is active belongs to that id, and simply never appears.
    DrawRenamePopup();
    DrawDeletePopup();

    if (m_status[0] != '\0') {
        ImGui::Separator();

        // A FAILURE IS NOT GREY. The ordinary "created X" note is fine dim -
        // it confirms something you watched happen. A refusal has to catch
        // the eye, or it is only technically being reported, which is how
        // "files cannot be opened" went unexplained in the first place.
        if (m_statusIsError) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "%s", m_status);
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton("Dismiss")) {
                m_status[0]     = '\0';
                m_statusIsError = false;
            }
        } else {
            ImGui::TextDisabled("%s", m_status);
        }
    }
}

bool AssetBrowserPanel::CreateScript(const std::string& name, std::string& outError) {
    if (!IsValidScriptName(name, outError)) {
        return false;
    }

    // Into WHICHEVER FOLDER IS BEING BROWSED. The build walks all of assets/,
    // so a script is found wherever you put it - which means the right place
    // for a new one is where you were already looking, next to the scene and
    // the images it belongs with.
    const std::string path = JoinPath(m_directory, name + ".cpp");

    if (eng::FileSystem::Exists(path)) {
        outError = "'" + path + "' already exists - overwriting it would destroy "
                                "whatever is in it";
        return false;
    }

    const std::string text = DefaultScriptText(name);
    if (!eng::FileSystem::WriteTextFile(path, text, outError)) {
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created the script '{}'", path);

    // Compiled AND registered straight away, rather than waiting for the next
    // time the window regains focus.
    //
    // A script you cannot attach to anything until some unexplained later
    // moment is a script that appears not to work. Building it now means the
    // class exists by the time you have finished reading the file, and it
    // turns up in the Inspector's script list immediately.
    // SYNCHRONOUS here, on purpose. The point of building after writing a
    // new script is that the class exists by the time you have finished
    // reading the file - so this one waits. It is also the only build the
    // user did not start themselves, and a brand new file compiles fast.
    const ScriptBuild::Result build = ScriptBuild::BuildAndReload();

    if (!build.ok) {
        // Not a failure of CreateScript - the file was written and is fine.
        // The template compiles, so a failure here is almost always another
        // script that was already broken.
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "'{}' was created, but the project's scripts did not build: {}",
                        path, build.summary);
    }
    return true;
}

void AssetBrowserPanel::DrawNewScriptPopup() {
    if (m_openNewScript) {
        ImGui::OpenPopup("New Script");
        m_openNewScript = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Script", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Script name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##scriptname", m_newScriptName, sizeof(m_newScriptName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // The name is checked AS IT IS TYPED rather than when Create is pressed.
    // Telling somebody their name is illegal only after they commit to it is a
    // worse experience than greying the button out while they can still see
    // the reason.
    std::string       error;
    const bool        nameOk = IsValidScriptName(m_newScriptName, error);
    const std::string path = JoinPath(m_directory, std::string(m_newScriptName) + ".cpp");

    if (!nameOk) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", error.c_str());
    } else {
        ImGui::TextDisabled("writes assets/%s", path.c_str());
    }

    ImGui::Separator();
    ImGui::TextWrapped("The file is created from a template with the lifecycle hooks "
                       "written out and explained, and it is compiled and loaded "
                       "straight away - so you can attach it to an entity as soon as "
                       "this window closes.");
    ImGui::Separator();

    ImGui::BeginDisabled(!nameOk);
    if (submitted || ImGui::Button("Create", ImVec2(120, 0))) {
        std::string createError;
        if (CreateScript(m_newScriptName, createError)) {
            std::snprintf(m_status, sizeof(m_status), "created and built %s",
                          path.c_str());
            Refresh();
            ImGui::CloseCurrentPopup();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", createError.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}", createError);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
//  New Tile
//
//  THE VALIDATION HERE IS THE LOADER'S OWN, not a second copy of it. The
//  dialog builds the .otile it is about to write and hands it to a scratch
//  TileAsset::Deserialize - so whatever the dialog accepts is exactly what the
//  engine will accept, and the derived tile size it shows is the one the
//  engine derived. A separate "is this image the right shape" check here would
//  be a second description of the same rule, and the two would drift.
// ---------------------------------------------------------------------------
bool AssetBrowserPanel::CreateTile(const std::string& name, const std::string& sheet,
                                   bool solid, std::string& outError) {
    if (name.empty()) {
        outError = "a tile needs a name";
        return false;
    }

    const std::string path = JoinPath(m_directory, name + ".otile");
    if (eng::FileSystem::Exists(path)) {
        outError = "'" + path + "' already exists";
        return false;
    }

    // Written WITHOUT tileSize or frames. They are measured from the image, so
    // stating them would be a second description of something the file already
    // carries - and a sheet later resized would then disagree with its own
    // tile rather than simply being re-measured. See TileAsset.h.
    eng::Json node      = eng::Json::object();
    node["_comment"]    = "A tile material. The sheet is ONE TILE WIDE and FIVE TILES "
                          "TALL: outer corner / vertical edge / horizontal edge / "
                          "inner corner / fill, top to bottom. Extra frames go "
                          "sideways. tileSize and frames are measured from the image.";
    node["sheet"]       = sheet;
    node["collision"]   = solid ? "Grid" : "None";

    const std::string text = node.dump(2) + "\n";
    if (!eng::FileSystem::WriteTextFile(path, text, outError)) {
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created the tile '{}'", path);

    // Chosen as the brush straight away, and the Tile Palette asked to show
    // it. A new tile you then have to go and find is a new tile you have to
    // go and find.
    TilePaintState& tiles = TilePaintState::Get();
    tiles.tilePath        = path;
    tiles.revealRequested = true;
    return true;
}

void AssetBrowserPanel::DrawNewTilePopup() {
    if (m_openNewTile) {
        ImGui::OpenPopup("New Tile");
        m_openNewTile = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Tile", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Tile sheet");
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("##tilesheet", m_newTileSheet, sizeof(m_newTileSheet));
    ImGui::TextDisabled("a path under assets/, e.g. textures/tiles/stone.png");

    // Every image in the folder being browsed, as a shortcut past typing one.
    if (ImGui::BeginCombo("##picksheet", "pick from this folder")) {
        int offered = 0;
        for (const eng::FileSystem::DirEntry& entry : m_entries) {
            if (entry.isDirectory ||
                ClassifyAsset(entry.virtualPath) != AssetKind::Texture) {
                continue;
            }
            ++offered;
            if (ImGui::Selectable(entry.name.c_str())) {
                std::snprintf(m_newTileSheet, sizeof(m_newTileSheet), "%s",
                              entry.virtualPath.c_str());
            }
        }
        if (offered == 0) {
            ImGui::TextDisabled("no images in this folder");
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Tile name");
    ImGui::SetNextItemWidth(420.0f);
    const bool submitted =
        ImGui::InputText("##tilename", m_newTileName, sizeof(m_newTileName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Checkbox("Solid - things collide with it", &m_newTileSolid);
    ImGui::TextDisabled("needs a TilemapColliderComponent on the map as well");

    ImGui::Separator();

    // ---- what the engine makes of this, live -------------------------------
    std::string       sheetError;
    eng::TileAsset    probe;
    bool              sheetOk = false;
    if (m_newTileSheet[0] != '\0') {
        eng::Json node  = eng::Json::object();
        node["sheet"]   = m_newTileSheet;
        sheetOk         = probe.Deserialize(node, sheetError);
    } else {
        sheetError = "name the sheet this tile draws from";
    }

    if (sheetOk) {
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.60f, 1.0f),
                           "%dpx tiles, %d frame(s)", probe.TileSize(),
                           probe.FrameCount());

        // The five bands, so the sheet can be checked at a glance before the
        // file is written. A sheet whose bands are in the wrong order is the
        // one mistake nothing else catches.
        if (const eng::TextureRef& sheet = probe.Sheet(); sheet && sheet->native) {
            const float width  = 56.0f;
            const float height = width * static_cast<float>(eng::kSheetBands);
            const float frame  = static_cast<float>(probe.TileSize()) /
                                static_cast<float>(sheet->width);
            ImGui::Image(reinterpret_cast<ImTextureID>(sheet->native),
                         ImVec2(width, height), ImVec2(0.0f, 0.0f),
                         ImVec2(frame, 1.0f));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextDisabled("outer corner");
            ImGui::TextDisabled("vertical edge");
            ImGui::TextDisabled("horizontal edge");
            ImGui::TextDisabled("inner corner");
            ImGui::TextDisabled("fill");
            ImGui::EndGroup();
        }
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", sheetError.c_str());
        ImGui::TextWrapped("A sheet is tileSize wide (times the number of frames) and "
                           "tileSize x 5 tall, and tileSize must be even.");
    }

    const bool nameOk = (m_newTileName[0] != '\0');
    const std::string path =
        JoinPath(m_directory, std::string(m_newTileName) + ".otile");
    if (nameOk) {
        ImGui::TextDisabled("writes assets/%s", path.c_str());
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!sheetOk || !nameOk);
    if ((submitted && sheetOk && nameOk) || ImGui::Button("Create", ImVec2(120, 0))) {
        std::string createError;
        if (CreateTile(m_newTileName, m_newTileSheet, m_newTileSolid, createError)) {
            std::snprintf(m_status, sizeof(m_status), "created %s", path.c_str());
            m_statusIsError = false;
            Refresh();
            ImGui::CloseCurrentPopup();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", createError.c_str());
            m_statusIsError = true;
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}", createError);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawNewFolderPopup() {
    if (m_openNewFolder) {
        ImGui::OpenPopup("New Folder");
        m_openNewFolder = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Folder", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Folder name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##foldername", m_newFolderName, sizeof(m_newFolderName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    const bool nameOk =
        m_newFolderName[0] != '\0' &&
        std::string_view(m_newFolderName).find_first_of("/\\:*?\"<>|") ==
            std::string_view::npos;

    if (!nameOk) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "a folder name cannot be empty or contain / \\ : * ? \" < > |");
    } else {
        ImGui::TextDisabled("inside %s", DisplayPath(m_directory).c_str());
    }

    ImGui::BeginDisabled(!nameOk);
    if (submitted || ImGui::Button("Create", ImVec2(120, 0))) {
        // Joined carefully rather than just concatenated, because the assets
        // root is the empty string and "" + "/" + name would start with a
        // slash - which means something quite different.
        const std::string path = m_directory.empty()
                                     ? std::string(m_newFolderName)
                                     : m_directory + "/" + m_newFolderName;
        std::string error;
        if (eng::FileSystem::CreateDirectory(path, error)) {
            std::snprintf(m_status, sizeof(m_status), "created %s", path.c_str());
            Refresh();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ===========================================================================
//  MOVING THINGS BETWEEN FOLDERS
// ===========================================================================

void AssetBrowserPanel::AcceptMoveInto(const std::string& targetDirectory) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }

    // LOOKED AT BEFORE IT IS ACCEPTED. GetDragDropPayload reports what is
    // being dragged without claiming it, so a target that could not carry the
    // move out declines to light up at all - rather than highlighting and
    // then refusing when the button comes up, which reads as a broken
    // feature. Same reasoning as HierarchyPanel::DrawEntityDropTarget.
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    for (const char* id : kAllAssetPayloads) {
        if (peek == nullptr || !peek->IsDataType(id)) {
            continue;
        }

        // THE TYPE IS CHECKED BEFORE THE DATA IS READ, and that order is not
        // incidental. An ENTITY dragged out of the Hierarchy and over this
        // panel carries an EntityId - raw bytes with no terminating zero -
        // and reading those as a C string would walk off the end of the
        // payload looking for one.
        const std::string from(static_cast<const char*>(peek->Data));
        if (MoveLooksLegal(from, targetDirectory) &&
            ImGui::AcceptDragDropPayload(id) != nullptr) {
            RequestMove(from, targetDirectory);
        }
        break;
    }

    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::RequestMove(const std::string& from,
                                    const std::string& toDirectory) {
    m_pendingMove.from        = from;
    m_pendingMove.toDirectory = toDirectory;
    m_pendingMove.requested   = true;
}

void AssetBrowserPanel::ApplyPendingMove() {
    if (!m_pendingMove.requested) {
        return;
    }

    // Taken and CLEARED FIRST, so that a failure below cannot leave a request
    // standing that tries again on every frame for the rest of the session -
    // which would mean one impossible drag producing the same error message
    // sixty times a second.
    const PendingMove request = m_pendingMove;
    m_pendingMove             = {};

    std::string error;
    if (!eng::FileSystem::Move(request.from, request.toDirectory, error)) {
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not move '{}': {}", request.from,
                         error);
        return;
    }

    m_statusIsError = false;
    std::snprintf(m_status, sizeof(m_status), "moved %s to %s", request.from.c_str(),
                  DisplayPath(request.toDirectory).c_str());
    ENGINE_LOG_INFO(eng::Channels::kEditor, "moved '{}' into '{}'", request.from,
                    DisplayPath(request.toDirectory));

    // What was selected is at a different path now. The selection is dropped
    // rather than left naming somewhere nothing lives, because the Delete key
    // acts on it - and a Delete aimed at a path that no longer exists is at
    // best confusing and at worst aimed at whatever has since taken the name.
    if (m_selectedPath == request.from) {
        m_selectedPath.clear();
    }

    Refresh();
}

// ===========================================================================
//  IMPORTING FILES DRAGGED IN FROM OUTSIDE
// ===========================================================================

bool AssetBrowserPanel::ContainsPoint(float screenX, float screenY) const {
    // Asked of the window AS IT IS RIGHT NOW rather than remembered from the
    // last frame. Begin() has already run by the time Draw is called, so the
    // position and size are current even though nothing has been drawn into
    // the window yet.
    const ImVec2 min  = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    return screenX >= min.x && screenY >= min.y && screenX <= min.x + size.x &&
           screenY <= min.y + size.y;
}

std::string AssetBrowserPanel::FolderUnderPoint(float screenX, float screenY) const {
    for (const Row& row : m_rows) {
        if (!row.isDirectory) {
            continue;
        }
        if (screenX >= row.minX && screenX <= row.maxX && screenY >= row.minY &&
            screenY <= row.maxY) {
            return row.virtualPath;
        }
    }
    // Over no folder in particular, which is the ordinary case: it lands in
    // whatever folder is being browsed, which is the one you are looking at.
    return m_directory;
}

void AssetBrowserPanel::ImportDroppedFiles() {
    const eng::EventPump& events = eng::Engine::Get().Events();
    if (events.DroppedFiles().empty()) {
        return;
    }

    std::size_t imported = 0;
    std::size_t refused  = 0;
    std::string lastLanded;
    std::string lastError;
    std::string lastDestination;

    for (const eng::DroppedFile& file : events.DroppedFiles()) {
        // NOT OURS - and this is the entire reason a drop carries a position.
        // One window holds every panel, so a file let go of over the Scene
        // view is reported to this code as well and has to be left alone.
        if (!ContainsPoint(file.x, file.y)) {
            continue;
        }

        const std::string destination = FolderUnderPoint(file.x, file.y);
        std::string       landed;
        std::string       error;
        if (eng::FileSystem::Import(file.path, destination, landed, error)) {
            ++imported;
            lastLanded      = landed;
            lastDestination = destination;
            ENGINE_LOG_INFO(eng::Channels::kEditor, "imported '{}' as '{}'", file.path,
                            landed);
        } else {
            ++refused;
            lastError = error;
            // AT ERROR rather than Warning. config/engine.json sets the
            // threshold to Error in this project, so a warning would reach
            // neither the Console nor the log file - and a refusal to import
            // something the user has just dropped is exactly the kind of
            // thing that must not disappear. See ShellOpen.h.
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not import '{}': {}",
                             file.path, error);
        }
    }

    if (imported == 0 && refused == 0) {
        return;   // every one of them landed on some other panel
    }

    if (refused == 0 && imported == 1) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "imported %s", lastLanded.c_str());
    } else if (refused == 0) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "imported %zu item(s) into %s",
                      imported, DisplayPath(lastDestination).c_str());
    } else if (imported == 0) {
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "%s", lastError.c_str());
    } else {
        // BOTH NUMBERS AND THE REASON. "imported 3 of 5" with no explanation
        // is a message nobody can act on.
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "imported %zu, refused %zu - %s",
                      imported, refused, lastError.c_str());
    }

    Refresh();

    // ---- a script that has just arrived is not compiled yet ----------------
    //
    // Scripts normally rebuild when the editor regains focus, which is the
    // right trigger for somebody editing one in another program. It is the
    // wrong trigger here: having dropped a .cpp in, the obvious next thing to
    // do is attach it, and finding it missing from the list reads as the
    // import having failed.
    //
    // NeedsRebuild answers exactly the right question, which is why it is
    // asked rather than testing the file extension: an imported image costs
    // nothing, and a dropped FOLDER with scripts inside it is noticed just as
    // readily as a single file would be.
    if (imported > 0 && ScriptBuild::NeedsRebuild() && ScriptBuild::BeginAsync()) {
        const std::string note = std::string(m_status) + " - building scripts...";
        std::snprintf(m_status, sizeof(m_status), "%s", note.c_str());
    }
}

void AssetBrowserPanel::DrawImportHighlight() {
    const eng::EventPump& events = eng::Engine::Get().Events();
    if (!events.DragHovering() || !ContainsPoint(events.DragX(), events.DragY())) {
        return;
    }

    const std::string destination = FolderUnderPoint(events.DragX(), events.DragY());

    // ON THE FOREGROUND DRAW LIST, which is unusual enough to be worth saying
    // why. ImGui knows nothing at all about this drag - it started in another
    // program - so none of its own highlighting happens and there is no
    // hovered item to hang an outline off. Drawing straight onto the
    // foreground puts the feedback above the grid child window, which would
    // otherwise clip it.
    ImDrawList*  draw = ImGui::GetForegroundDrawList();
    const ImVec2 min  = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    draw->AddRect(min, max, IM_COL32(120, 190, 250, 255), 4.0f, 0, 3.0f);

    char label[320];
    std::snprintf(label, sizeof(label), "drop to import into %s",
                  DisplayPath(destination).c_str());

    // Behind a filled box, because this is drawn over whatever the panel
    // happens to contain and pale text on top of a thumbnail is unreadable.
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 at(min.x + (size.x - textSize.x) * 0.5f, max.y - textSize.y - 10.0f);
    draw->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 4.0f),
                        ImVec2(at.x + textSize.x + 8.0f, at.y + textSize.y + 4.0f),
                        IM_COL32(18, 28, 42, 235), 4.0f);
    draw->AddText(at, IM_COL32(190, 220, 255, 255), label);

    // And the folder the cursor is actually over, when it is over one. The
    // message above names it, but a name is something to read and an outline
    // is something to see.
    if (destination == m_directory) {
        return;
    }
    for (const Row& row : m_rows) {
        if (row.virtualPath != destination) {
            continue;
        }
        draw->AddRect(ImVec2(row.minX - 2.0f, row.minY - 2.0f),
                      ImVec2(row.maxX + 2.0f, row.maxY + 2.0f),
                      IM_COL32(120, 190, 250, 255), 3.0f, 0, 2.0f);
        break;
    }
}

void AssetBrowserPanel::OnHidden() {
    // See the note in the header: positions recorded while this panel was on
    // screen would go on answering "which folder is under this point" with a
    // layout that is no longer true.
    m_rows.clear();
}

} // namespace editor
