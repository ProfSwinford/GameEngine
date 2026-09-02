// ============================================================================
//  AssetBrowserPanel.cpp - the Assets panel. See AssetBrowserPanel.h for the
//  two-roots argument.
// ============================================================================

#include "panels/AssetBrowserPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ScriptBuild.h"
#include "ScriptTemplate.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace editor {
namespace {

// ONE root: assets/. Scenes, images and scripts all live in it, in whatever
// folder structure suits the game.
//
// THE ASSETS ROOT IS THE EMPTY STRING, and that is not a shortcut. Virtual
// paths are ALREADY relative to assets/ - "scenes/level1.json" resolves to
// <project>/assets/scenes/level1.json - so the virtual path OF assets/ itself
// is "". Writing "assets" here would ask for <project>/assets/assets.
constexpr const char* kRootAssets = "";

// What to show at the top of the panel. "" is a real virtual path and a
// terrible thing to display.
std::string DisplayPath(const std::string& directory) {
    return directory.empty() ? "assets" : "assets/" + directory;
}

ImVec4 ColourFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return ImVec4(0.55f, 0.78f, 0.95f, 1.0f);
        case AssetKind::Scene:   return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
        case AssetKind::Script:  return ImVec4(0.65f, 0.90f, 0.65f, 1.0f);
        case AssetKind::Unknown: break;
    }
    return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
}

const char* LabelFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return "IMG";
        case AssetKind::Scene:   return "SCN";
        case AssetKind::Script:  return "CPP";
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
    ImGui::BeginDisabled(m_directory == kRootAssets);
    if (ImGui::SmallButton("assets")) {
        Navigate(kRootAssets);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

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
        const ScriptBuild::Result result = ScriptBuild::BuildAndReload();
        std::snprintf(m_status, sizeof(m_status), "%s", result.summary.c_str());
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

    ImGui::TextDisabled("%s", DisplayPath(m_directory).c_str());
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
        const ImVec4 colour =
            entry.isDirectory ? ImVec4(0.85f, 0.75f, 0.45f, 1.0f) : ColourFor(kind);
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
    // Folders are not draggable: there is nothing sensible to do with a folder
    // dropped onto an entity.
    if (!entry.isDirectory) {
        if (const char* payloadId = PayloadIdFor(kind); payloadId != nullptr) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                // The virtual path is what gets carried, including its
                // terminating zero so the receiving end can use it directly as
                // a C string. ImGui takes a COPY of it, which matters: passing
                // a pointer into m_entries would dangle the moment a refresh
                // happened mid-drag.
                ImGui::SetDragDropPayload(payloadId, entry.virtualPath.c_str(),
                                          entry.virtualPath.size() + 1);

                // What the cursor carries while dragging. Worth the four
                // lines: a drag with no feedback feels broken even when it is
                // working perfectly.
                ImGui::TextColored(ColourFor(kind), "%s", LabelFor(kind));
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.name.c_str());
                if (kind == AssetKind::Script) {
                    ImGui::TextDisabled("drop on an entity to attach it");
                } else if (kind == AssetKind::Texture) {
                    ImGui::TextDisabled("drop in the Scene view to place it");
                }
                ImGui::EndDragDropSource();
            }
        }
    }

    // The name under the tile, wrapped to the tile's width so that one long
    // filename does not push the whole grid apart.
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + m_iconSize);
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    if (iconHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.virtualPath.c_str());
        if (!entry.isDirectory) {
            ImGui::TextDisabled("%llu bytes", entry.byteSize);
        }
        switch (kind) {
            case AssetKind::Texture:
                ImGui::TextDisabled("drag into the Scene view, or onto an entity");
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
            case AssetKind::Unknown:
                break;
        }
        ImGui::EndTooltip();
    }

    if (iconClicked) {
        if (entry.isDirectory) {
            Navigate(entry.virtualPath);
        } else if (kind == AssetKind::Scene) {
            // Recorded rather than loaded here - loading destroys every
            // entity, and other panels are still using them this frame. See
            // EditorState::requestedScene.
            EditorState::Get().requestedScene = entry.virtualPath;
        }
    }

    ImGui::PopID();
}

void AssetBrowserPanel::Draw() {
    DrawBreadcrumb();
    ImGui::Separator();

    if (!m_valid) {
        // A folder can legitimately be missing - it may have been deleted from
        // outside the editor while you were looking at it - so this is an
        // instruction rather than an error.
        ImGui::TextDisabled("'%s' does not exist yet.", DisplayPath(m_directory).c_str());
        ImGui::TextDisabled("Press Up to go back, or Refresh.");
        DrawNewScriptPopup();
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
    ImGui::EndChild();

    DrawNewScriptPopup();
    DrawNewFolderPopup();

    if (m_status[0] != '\0') {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_status);
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

} // namespace editor
