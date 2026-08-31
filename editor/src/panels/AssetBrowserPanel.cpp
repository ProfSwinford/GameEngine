// THE ASSET BROWSER. See AssetBrowserPanel.h for the two-roots argument.

#include "panels/AssetBrowserPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ScriptTemplate.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace editor {
namespace {

// The two roots. Not discovered, because "which directories are meaningful" is
// a project decision rather than something to infer from what happens to be on
// disk - listing the repository root would show build/, engine/ and .git.
//
// THE ASSETS ROOT IS THE EMPTY STRING, and that is not a shortcut. The virtual
// path space is ALREADY rooted at assets/ - "scenes/orbit_test.json" resolves
// to <root>/assets/scenes/orbit_test.json - so the virtual path OF assets/ is
// "". Using "assets" here asked for <root>/assets/assets, which does not
// exist, and the panel correctly reported that it did not.
//
// gamescripts/ is the exception FileSystem::Resolve already knows about, so it
// keeps its name.
constexpr const char* kRootAssets  = "";
constexpr const char* kRootScripts = "gamescripts";

bool IsScriptsRoot(const std::string& directory) {
    return directory == kRootScripts || directory.starts_with("gamescripts/");
}

// What to show the user. "" is a real virtual path and a terrible label.
std::string DisplayPath(const std::string& directory) {
    if (IsScriptsRoot(directory)) {
        return directory;
    }
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

std::string ParentOf(const std::string& directory) {
    const eng::usize slash = directory.find_last_of('/');
    return slash == std::string::npos ? std::string() : directory.substr(0, slash);
}

} // namespace

AssetBrowserPanel::AssetBrowserPanel() { Refresh(); }

AssetBrowserPanel::~AssetBrowserPanel() {
    // EVERY Acquire has a matching Release, and the destructor is one of the
    // places that has to be true. A panel that leaked a handle per navigation
    // would show up as a refcount that never returns to zero on unload - and
    // would then be blamed on the scene.
    ReleaseThumbnails();
}

void AssetBrowserPanel::ReleaseThumbnails() {
    for (eng::Handle<eng::Texture>& handle : m_thumbnails) {
        if (!handle.IsNull()) {
            eng::ResourceManager::Release(handle);
        }
    }
    m_thumbnails.clear();
}

void AssetBrowserPanel::Navigate(const std::string& virtualDirectory) {
    m_directory = virtualDirectory;
    Refresh();
}

void AssetBrowserPanel::Refresh() {
    ReleaseThumbnails();

    m_valid = eng::FileSystem::ListDirectory(m_directory, m_entries);
    if (!m_valid) {
        m_entries.clear();
    }

    m_thumbnails.resize(m_entries.size());
    for (eng::usize i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].isDirectory ||
            ClassifyAsset(m_entries[i].virtualPath) != AssetKind::Texture) {
            continue;
        }
        // SYNCHRONOUS, not the async path. A thumbnail is wanted THIS frame and
        // a directory holds tens of images, not thousands; the async queue
        // exists for a scene load that must not stall, which is a different
        // problem. If this ever becomes slow the fix is AcquireTextureAsync
        // plus a placeholder, which the resource manager already supports.
        m_thumbnails[i] = eng::ResourceManager::AcquireTexture(m_entries[i].virtualPath);
    }
}

void AssetBrowserPanel::DrawBreadcrumb() {
    // Root switcher. Two buttons rather than a tree, because there are exactly
    // two roots and a tree control for two items is ceremony.
    const bool inScripts = IsScriptsRoot(m_directory);

    ImGui::BeginDisabled(!inScripts);
    if (ImGui::SmallButton("assets")) {
        Navigate(kRootAssets);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(inScripts);
    if (ImGui::SmallButton("gamescripts")) {
        Navigate(kRootScripts);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Up. Disabled AT a root rather than hidden, so the toolbar keeps its shape
    // as you navigate. "At a root" is a comparison against the root itself, not
    // an empty-parent test: the assets root IS the empty string, so an
    // empty-parent test would have disabled Up in every first-level folder.
    const std::string root   = inScripts ? kRootScripts : kRootAssets;
    const std::string parent = ParentOf(m_directory);
    ImGui::BeginDisabled(m_directory == root);
    if (ImGui::SmallButton("Up")) {
        Navigate(parent);
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
        m_openNewFolder = true;
        m_newFolderName[0] = '\0';
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("##icon", &m_iconSize, 40.0f, 128.0f, "icon %.0f");

    ImGui::TextDisabled("%s", DisplayPath(m_directory).c_str());
}

void AssetBrowserPanel::DrawEntry(const eng::FileSystem::DirEntry& entry, int index) {
    const AssetKind kind = entry.isDirectory ? AssetKind::Unknown
                                             : ClassifyAsset(entry.virtualPath);

    ImGui::PushID(index);
    ImGui::BeginGroup();

    const ImVec2 iconSize(m_iconSize, m_iconSize);

    // The thumbnail, or a coloured type tile. A tile rather than a blank space
    // so every entry is the same size and the grid does not go ragged.
    bool drewImage = false;
    if (index < static_cast<int>(m_thumbnails.size()) && !m_thumbnails[index].IsNull()) {
        if (const eng::Texture* texture = eng::ResourceManager::Get(m_thumbnails[index]);
            texture != nullptr && texture->native != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture->native), iconSize);
            drewImage = true;
        }
    }
    if (!drewImage) {
        const ImVec4 colour = entry.isDirectory ? ImVec4(0.85f, 0.75f, 0.45f, 1.0f)
                                                : ColourFor(kind);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colour.x * 0.35f, colour.y * 0.35f,
                                                      colour.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::Button(entry.isDirectory ? "DIR" : LabelFor(kind), iconSize);
        ImGui::PopStyleColor(2);
    }

    const bool iconClicked  = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    const bool iconHovered  = ImGui::IsItemHovered();

    // ---- DRAG SOURCE ------------------------------------------------------
    //
    // On the ICON, and the source is begun before the label is drawn so the
    // whole tile is grabbable. Directories are not draggable: there is nothing
    // sensible to do with a folder dropped on an entity.
    if (!entry.isDirectory) {
        if (const char* payloadId = PayloadIdFor(kind); payloadId != nullptr) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                // The VIRTUAL PATH is the payload, with its terminator, so the
                // receiving end can use it as a C string directly. Paths are
                // short and the payload is copied by ImGui - passing a pointer
                // into m_entries would dangle the moment a Refresh happened
                // mid-drag.
                ImGui::SetDragDropPayload(payloadId, entry.virtualPath.c_str(),
                                          entry.virtualPath.size() + 1);

                // The drag preview. Worth the four lines: a drag with no
                // feedback feels broken even when it works.
                ImGui::TextColored(ColourFor(kind), "%s", LabelFor(kind));
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.name.c_str());
                if (kind == AssetKind::Script) {
                    ImGui::TextDisabled("drop on an entity to attach");
                } else if (kind == AssetKind::Texture) {
                    ImGui::TextDisabled("drop in the Scene view to place it");
                }
                ImGui::EndDragDropSource();
            }
        }
    }

    // The name under the tile, wrapped to the tile width so a long file name
    // does not push the grid apart.
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + m_iconSize);
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    if (iconHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.virtualPath.c_str());
        if (!entry.isDirectory) {
            ImGui::TextDisabled("%llu bytes",
                                static_cast<unsigned long long>(entry.byteSize));
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
            EditorState::Get().requestedScene = entry.virtualPath;
        }
    }

    ImGui::PopID();
}

void AssetBrowserPanel::Draw() {
    DrawBreadcrumb();
    ImGui::Separator();

    if (!m_valid) {
        // gamescripts/ legitimately does not exist until the first script is
        // created, so this is an instruction rather than an error.
        ImGui::TextDisabled("'%s' does not exist yet.", DisplayPath(m_directory).c_str());
        if (m_directory == kRootScripts) {
            ImGui::TextDisabled("Press + Script to create one.");
        }
        DrawNewScriptPopup();
        DrawNewFolderPopup();
        return;
    }

    // A wrapping grid, laid out by hand from the available width. ImGui has no
    // flow layout; this is the standard way to build one.
    const float cellWidth = m_iconSize + ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int   columns   = std::max(1, static_cast<int>(available / cellWidth));

    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);
    int column = 0;
    for (eng::usize i = 0; i < m_entries.size(); ++i) {
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

    // Always into gamescripts/, never into the folder currently being browsed.
    // The build globs gamescripts/*.cpp non-recursively, so a script written into
    // a subfolder would be created, listed, and never compiled - which is a
    // worse outcome than refusing to put it there.
    const std::string path = std::string(kRootScripts) + "/" + name + ".cpp";

    if (eng::FileSystem::Exists(path)) {
        outError = "'" + path + "' already exists - overwriting it would destroy whatever "
                                "is in it";
        return false;
    }

    if (!eng::FileSystem::CreateDirectory(kRootScripts, outError)) {
        return false;
    }

    const std::string text = DefaultScriptText(name);
    if (!eng::FileSystem::WriteFile(path, text.data(), text.size(), outError)) {
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created script '{}' ({} bytes)", path,
                    text.size());
    return true;
}

void AssetBrowserPanel::DrawNewScriptPopup() {
    if (m_openNewScript) {
        ImGui::OpenPopup("New Script");
        m_openNewScript = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Script name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted = ImGui::InputText("##scriptname", m_newScriptName,
                                            sizeof(m_newScriptName),
                                            ImGuiInputTextFlags_EnterReturnsTrue);

    // The name is validated AS IT IS TYPED, not on submit. Telling someone the
    // name is illegal only after they commit to it is a worse experience than
    // greying the button out while they can still see why.
    std::string     error;
    const bool      nameOk = IsValidScriptName(m_newScriptName, error);
    const std::string path = std::string(kRootScripts) + "/" + m_newScriptName + ".cpp";

    if (!nameOk) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", error.c_str());
    } else {
        ImGui::TextDisabled("writes %s", path.c_str());
    }

    ImGui::Separator();
    ImGui::TextWrapped("The file is created from the default template, with every "
                       "lifecycle hook stubbed and commented. It is COMPILED C++ - "
                       "build and relaunch before it will run. Until then the "
                       "Inspector shows it as unresolved, and it can still be "
                       "attached and saved into a scene.");
    ImGui::Separator();

    ImGui::BeginDisabled(!nameOk);
    if (submitted || ImGui::Button("Create", ImVec2(120, 0))) {
        std::string createError;
        if (CreateScript(m_newScriptName, createError)) {
            std::snprintf(m_status, sizeof(m_status), "created %s - rebuild to run it",
                          path.c_str());
            Navigate(kRootScripts);
            ImGui::CloseCurrentPopup();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", createError.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "new script: {}", createError);
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
    if (!ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Folder name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted = ImGui::InputText("##foldername", m_newFolderName,
                                            sizeof(m_newFolderName),
                                            ImGuiInputTextFlags_EnterReturnsTrue);

    const bool nameOk = m_newFolderName[0] != '\0' &&
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
        // Joined rather than concatenated, because the assets root is the empty
        // string and "" + "/" + name is an ABSOLUTE path on the way to
        // resolving somewhere nobody intended.
        const std::string path = m_directory.empty()
                                     ? std::string(m_newFolderName)
                                     : m_directory + "/" + m_newFolderName;
        std::string       error;
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
