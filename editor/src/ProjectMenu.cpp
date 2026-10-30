// ============================================================================
//  ProjectMenu.cpp - the File menu's project items. See ProjectMenu.h for why
//  switching projects restarts the editor.
// ============================================================================

#include "ProjectMenu.h"

#include "EditorApp.h"
#include "Relaunch.h"

#include <engine/Engine.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/fs/UserPaths.h>
#include <engine/platform/FolderDialog.h>
#include <engine/project/EngineBuild.h>
#include <engine/project/Project.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace editor {
namespace {

// What the folder picker was opened for. There is one dialog and one pending
// answer, so without this, picking a folder for New would be acted on by
// Open. See FolderDialog::Result::purpose.
constexpr int kPurposeOpen = 1;
constexpr int kPurposeNew  = 2;

ProjectMenu::Exit g_exit = ProjectMenu::Exit::Quit;
std::string       g_openNext;

// ---------------------------------------------------------------------------
//  A PROJECT CHANGE THAT IS WAITING TO BE CONFIRMED.
//
//  Switching projects ends the process, so it throws away unsaved work -
//  which makes it the one action in the editor that has to ask first. The
//  request is therefore recorded rather than acted on, and only becomes real
//  when either there was nothing to lose or somebody said so.
//
//  The folder is kept here rather than being re-picked after the question,
//  because asking somebody to find the same folder twice is its own small
//  insult.
// ---------------------------------------------------------------------------
struct Waiting {
    enum class Kind { None, Open, Close } kind = Kind::None;
    std::string root;
    std::string name;   // for the question, which should name the project
};
Waiting g_waiting;
bool    g_openUnsavedPopup = false;

// Carries a project change out: records what main() should do after the
// editor has shut down, and asks the frame loop to stop.
//
// RequestQuit is the SAME flag the window's close button sets, deliberately,
// so the shutdown that follows is the path that is already exercised every
// time anybody closes the editor rather than a second one that resembles it.
void Commit(const Waiting& change) {
    g_waiting = Waiting{};

    if (change.kind == Waiting::Kind::Open) {
        g_openNext = change.root;
        g_exit     = ProjectMenu::Exit::OpenProject;
        ENGINE_LOG_INFO(eng::Channels::kEditor, "opening '{}' - the editor will restart",
                        change.name);
    } else {
        g_openNext.clear();

        // BACK TO THE LAUNCHER IF THERE IS ONE. Closing a project should land
        // somewhere you can open another, and that is what the launcher is
        // for. With no launcher installed there is nowhere to land, so
        // closing the project is closing the editor - which is the honest
        // answer rather than a menu item that implies a list that does not
        // exist.
        const bool toHub = HubIsInstalled();
        g_exit = toHub ? ProjectMenu::Exit::OpenHub : ProjectMenu::Exit::Quit;

        ENGINE_LOG_INFO(eng::Channels::kEditor, "closing '{}'{}", change.name,
                        toHub ? " - back to the launcher" : "");
    }

    eng::Engine::Get().RequestQuit();
}

// The name typed into the New Project popup. Kept here rather than as a local
// static inside Draw so that cancelling and reopening starts clean.
char g_newName[96] = "";
std::string g_newParent;      // the folder picked to create it in
std::string g_newError;       // why the name is unusable, shown live

std::string g_titleShown;     // what the window title already says

// ---------------------------------------------------------------------------
//  THE RECENT-PROJECTS LIST
//
//  Kept with the USER, not with any project - a project cannot sensibly hold
//  a list that includes other projects, and this is one person's history on
//  one machine. See UserPaths.h.
//
//  The same file a launcher reads and writes, deliberately: a project opened
//  from the editor should appear in the launcher's list and the other way
//  round, and two lists that are nearly the same are worse than one.
// ---------------------------------------------------------------------------
constexpr const char* kRecentsFile = "projects.json";
constexpr std::size_t kMaxRecents  = 12;

struct Recent {
    std::string root;
    std::string name;
};

std::vector<Recent> ReadRecents() {
    std::vector<Recent> out;

    const std::string path = eng::UserPaths::File(kRecentsFile);
    if (path.empty()) {
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;   // no list yet. Not a problem worth saying anything about.
    }
    std::ostringstream all;
    all << in.rdbuf();

    std::string error;
    const eng::Json document = eng::ParseJson(all.str(), error);
    if (!error.empty() || !document.is_object() || !document.contains("projects") ||
        !document["projects"].is_array()) {
        return out;
    }

    for (const eng::Json& entry : document["projects"]) {
        if (!entry.is_object()) {
            continue;
        }
        Recent recent;
        recent.root = eng::ReadString(entry, "root", "", kRecentsFile);
        recent.name = eng::ReadString(entry, "name", "", kRecentsFile);
        if (!recent.root.empty()) {
            out.push_back(recent);
        }
    }
    return out;
}

void WriteRecents(const std::vector<Recent>& recents) {
    const std::string path = eng::UserPaths::File(kRecentsFile);
    if (path.empty()) {
        return;   // nowhere to keep it; UserPaths has already said so once
    }

    eng::Json list = eng::Json::array();
    for (const Recent& recent : recents) {
        eng::Json entry   = eng::Json::object();
        entry["root"]     = recent.root;
        entry["name"]     = recent.name;
        list.push_back(entry);
    }

    eng::Json document   = eng::Json::object();
    document["_comment"] = "Projects you have opened, newest first. Shared by the "
                           "editor's File -> Recent Projects and the launcher. Safe to "
                           "delete; it is only a list.";
    document["projects"] = list;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) {
        out << document.dump(2) << "\n";
    }
}

// Puts a project at the top of the list, removing any older entry for the
// same folder.
//
// COMPARED AS NORMALISED PATHS, so the same project reached through a
// different spelling - a trailing slash, different capitalisation on Windows
// - does not appear twice.
void RememberProject(const std::string& root, const std::string& name) {
    std::vector<Recent> recents = ReadRecents();

    const fs::path wanted = fs::path(root).lexically_normal();
    recents.erase(std::remove_if(recents.begin(), recents.end(),
                                 [&wanted](const Recent& recent) {
                                     return fs::path(recent.root).lexically_normal() ==
                                            wanted;
                                 }),
                  recents.end());

    recents.insert(recents.begin(), Recent{root, name});
    if (recents.size() > kMaxRecents) {
        recents.resize(kMaxRecents);
    }
    WriteRecents(recents);
}

// The project this editor currently has open, read once at start-up.
const eng::ProjectInfo& CurrentProject() {
    static const eng::ProjectInfo info = [] {
        eng::ProjectInfo read;
        std::string      error;
        if (!eng::FileSystem::AssetRoot().empty()) {
            eng::Project::Read(eng::FileSystem::AssetRoot(), read, error);
        }
        return read;
    }();
    return info;
}

} // namespace

bool ProjectMenu::RequestOpen(const std::string& realRoot) {
    // ======================================================================
    //  CHECKED BEFORE ANYTHING IS TORN DOWN.
    //
    //  Recording the request first and validating it after the editor has
    //  closed would mean a mistyped path, or a recents entry whose folder has
    //  been moved, closes the editor and then opens nothing. From the outside
    //  that is indistinguishable from a crash.
    // ======================================================================
    if (realRoot.empty()) {
        return false;
    }

    eng::ProjectInfo info;
    std::string      error;
    if (!eng::Project::Read(realRoot, info, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "cannot open that project: {}", error);
        return false;
    }

    // ALREADY OPEN. Restarting to arrive where you already are is a second of
    // flicker for nothing, and it would lose unsaved work on the way.
    if (fs::path(info.realRoot).lexically_normal() ==
        fs::path(eng::FileSystem::AssetRoot()).lexically_normal()) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "'{}' is already open", info.name);
        return false;
    }

    // ---- ASK BEFORE THROWING WORK AWAY -----------------------------------
    //
    // The restart ends the process, so anything unsaved goes with it. This is
    // the only action in the editor that can lose an edit without the edit
    // being the subject, which is exactly when a confirmation earns its
    // interruption.
    if (EditorState::Get().dirty) {
        g_waiting = Waiting{Waiting::Kind::Open, info.realRoot, info.name};
        g_openUnsavedPopup = true;
        return true;   // accepted; it is waiting on an answer
    }

    Commit(Waiting{Waiting::Kind::Open, info.realRoot, info.name});
    return true;
}

bool ProjectMenu::RequestClose() {
    if (EditorState::Get().dirty) {
        g_waiting          = Waiting{Waiting::Kind::Close, {}, CurrentProject().name};
        g_openUnsavedPopup = true;
        return true;
    }

    Commit(Waiting{Waiting::Kind::Close, {}, CurrentProject().name});
    return true;
}

ProjectMenu::Exit ProjectMenu::WhatNext() {
    return g_exit;
}

const std::string& ProjectMenu::ProjectToOpen() {
    return g_openNext;
}

void ProjectMenu::UpdateWindowTitle() {
    const eng::Scene& scene = eng::Engine::Get().GetScene();

    // The project first, because that is the thing that identifies the window
    // when several are open. The asterisk is the usual mark for unsaved work.
    std::string title = CurrentProject().name.empty() ? "no project"
                                                      : CurrentProject().name;
    if (!scene.SourcePath().empty()) {
        title += " - " + scene.SourcePath();
    }
    if (EditorState::Get().dirty) {
        title += "*";
    }
    title += " - Engine2D";

    // ONLY WHEN IT CHANGED. Setting a window title is a system call, and this
    // is reached every frame.
    if (title == g_titleShown) {
        return;
    }
    g_titleShown = title;
    eng::Engine::Get().GetWindow().SetTitle(title.c_str());
}

void ProjectMenu::CollectPickerResult() {
    const eng::FolderDialog::Result picked = eng::FolderDialog::Take();

    switch (picked.choice) {
        case eng::FolderDialog::Choice::None:
            return;

        case eng::FolderDialog::Choice::Cancelled:
            // Said out loud. A dialog that was dismissed and a dialog that
            // never opened look identical from the outside, and silence about
            // either is how a menu item comes to seem broken.
            ENGINE_LOG_INFO(eng::Channels::kEditor, "no folder was picked");
            return;

        case eng::FolderDialog::Choice::Failed:
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "the folder picker could not run: {}",
                             picked.error);
            return;

        case eng::FolderDialog::Choice::Chosen:
            break;
    }

    if (picked.purpose == kPurposeOpen) {
        // WALKS UP. Picking assets/scenes/ inside a project opens the
        // project, rather than refusing because the folder handed over is not
        // itself the root. People pick the folder they can see.
        const std::string root = eng::Project::FindAbove(picked.path);
        if (root.empty()) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "'{}' is not a project and neither is anything above it - "
                             "a project is a folder with a '{}' or an 'assets' folder",
                             picked.path, eng::Project::kFileName);
            return;
        }
        if (RequestOpen(root)) {
            eng::ProjectInfo info;
            std::string      error;
            if (eng::Project::Read(root, info, error)) {
                RememberProject(info.realRoot, info.name);
            }
        }
        return;
    }

    if (picked.purpose == kPurposeNew) {
        // The folder is only half the answer; the name comes from a popup
        // that is opened now that there is somewhere to put it.
        g_newParent = picked.path;
        g_newError.clear();
        g_newName[0] = '\0';
        ImGui::OpenPopup("New Project");
    }
}

void ProjectMenu::Draw() {
    if (ImGui::MenuItem("New Project...")) {
        eng::FolderDialog::Ask(kPurposeNew);
    }
    if (ImGui::MenuItem("Open Project...")) {
        eng::FolderDialog::Ask(kPurposeOpen);
    }
    // Closing a project closes the editor, because the editor is always open
    // ON a project - there is no project-less state for it to fall back to,
    // and inventing one would mean every panel learning to draw nothing.
    //
    // With a launcher installed it reopens that instead of just exiting, so
    // closing a project lands somewhere you can open another one. The
    // launcher closes itself when it starts an editor, so it genuinely does
    // have to be started again - see Commit.
    if (ImGui::MenuItem("Close Project")) {
        RequestClose();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            HubIsInstalled()
                ? "Closes this project and goes back to the launcher. Your place in it - "
                  "the open scene, the selection and the layout - is remembered."
                : "Closes the editor. Your place in this project - the open scene, the "
                  "selection and the layout - is remembered.");
    }

    // ---- recent projects --------------------------------------------------
    //
    // In the editor as well as the launcher because the common reason to
    // leave a project is to go to ANOTHER project, not to a list of them.
    if (ImGui::BeginMenu("Recent Projects")) {
        const std::vector<Recent> recents = ReadRecents();
        const fs::path            open =
            fs::path(eng::FileSystem::AssetRoot()).lexically_normal();

        if (recents.empty()) {
            ImGui::TextDisabled("nothing yet");
        }

        for (const Recent& recent : recents) {
            const bool isOpen = fs::path(recent.root).lexically_normal() == open;

            // The folder is shown under the name, because two projects can
            // share a name and the folder is what tells them apart.
            const std::string label =
                (recent.name.empty() ? recent.root : recent.name);

            if (ImGui::MenuItem(label.c_str(), nullptr, isOpen, !isOpen)) {
                if (RequestOpen(recent.root)) {
                    RememberProject(recent.root, recent.name);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", recent.root.c_str());
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    // ---- writing a manifest for a project that has none -------------------
    //
    // Offered rather than done. A project with an assets/ folder and no
    // manifest works perfectly; the only thing it lacks is a name of its own.
    // Writing a file into somebody's folder because they opened it is how a
    // tool stops being trusted.
    if (!CurrentProject().hasManifest && !eng::FileSystem::AssetRoot().empty()) {
        if (ImGui::MenuItem("Add a Project File")) {
            eng::ProjectInfo info = CurrentProject();
            info.engineVersion    = eng::EngineBuild::Running().id;

            std::string error;
            if (eng::Project::Write(info, error)) {
                ENGINE_LOG_INFO(eng::Channels::kEditor,
                                "wrote {} - this project is now called '{}'",
                                eng::Project::kFileName, info.name);
            } else {
                ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not write {}: {}",
                                 eng::Project::kFileName, error);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("This folder has no %s, so it is open under its folder "
                              "name. Adding one gives it a name of its own.",
                              eng::Project::kFileName);
        }
    }
}

// ---------------------------------------------------------------------------
//  The New Project popup.
//
//  Drawn from EditorApp outside the menu, because a popup opened from inside
//  a menu closes with the menu.
// ---------------------------------------------------------------------------
void DrawNewProjectPopup() {
    if (!ImGui::BeginPopupModal("New Project", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("A new project will be created in:");
    ImGui::TextWrapped("%s", g_newParent.c_str());
    ImGui::Separator();

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered = ImGui::InputText("Name", g_newName, sizeof(g_newName),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    // CHECKED AS IT IS TYPED, so the reason a name will not do is on screen
    // before the button is pressed rather than after.
    std::string nameError;
    const bool  usable = eng::Project::NameIsUsable(g_newName, nameError);
    if (!usable && g_newName[0] != '\0') {
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "%s", nameError.c_str());
    } else if (!g_newError.empty()) {
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "%s", g_newError.c_str());
    } else {
        ImGui::TextDisabled("assets/, config/ and a project file will be created.");
    }

    ImGui::BeginDisabled(!usable);
    const bool create = ImGui::Button("Create") || (entered && usable);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    if (create) {
        eng::ProjectInfo made;
        std::string      error;
        if (!eng::Project::Create(g_newParent, g_newName, made, error)) {
            // STAYS OPEN with the reason, rather than closing and leaving the
            // explanation in the Console where somebody has to go looking.
            g_newError = error;
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not create the project: {}",
                             error);
        } else {
            made.engineVersion = eng::EngineBuild::Running().id;
            std::string writeError;
            eng::Project::Write(made, writeError);

            RememberProject(made.realRoot, made.name);
            ImGui::CloseCurrentPopup();

            // Opening it restarts the editor, which is also what makes the
            // new project's own config/engine.json take effect.
            ProjectMenu::RequestOpen(made.realRoot);
        }
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
//  "You have unsaved changes" - asked only when there are some.
//
//  SWITCHING PROJECTS ENDS THE PROCESS, so unlike every other action in the
//  editor it cannot be undone and it is not itself about the thing being
//  lost. That is what earns the interruption: a confirmation that appears
//  when nothing is at stake trains people to dismiss the one that matters.
//
//  THREE ANSWERS, NOT TWO. "Save" and "Cancel" alone would mean somebody who
//  deliberately wants to abandon an experiment has to save it first. Discard
//  is the honest third option, and it is the one that is NOT the default.
// ---------------------------------------------------------------------------
void DrawUnsavedProjectPopup() {
    if (g_openUnsavedPopup) {
        ImGui::OpenPopup("Unsaved Changes");
        g_openUnsavedPopup = false;
    }

    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const eng::Scene& scene = eng::Engine::Get().GetScene();

    // NAMES THE FILE. "You have unsaved changes" on its own leaves somebody
    // working out which of several things they have been editing it means.
    if (scene.SourcePath().empty()) {
        ImGui::TextUnformatted("The scene has not been saved anywhere yet.");
    } else {
        ImGui::Text("%s has unsaved changes.", scene.SourcePath().c_str());
    }

    ImGui::TextDisabled(g_waiting.kind == Waiting::Kind::Close
                            ? "Closing the project will discard them."
                            : "Opening another project will discard them.");
    ImGui::Separator();

    // A scene with nowhere to be saved to cannot be saved from here - Save
    // Scene As... needs a name, and asking for one inside this dialog would
    // be a second question stacked on the first.
    const bool canSave = !scene.SourcePath().empty() && scene.IsLoaded();

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save and continue")) {
        std::string error;
        if (eng::Engine::Get().SaveScene({}, error)) {
            EditorState::Get().dirty = false;
            const Waiting change     = g_waiting;
            ImGui::CloseCurrentPopup();
            Commit(change);
        } else {
            // STAYS OPEN. Carrying on after a failed save would discard
            // exactly the work this dialog exists to protect.
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "not switching project: the scene could not be "
                             "saved: {}", error);
        }
    }
    ImGui::EndDisabled();
    if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This scene has no file yet - use File -> Save Scene "
                          "As... first.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard and continue")) {
        const Waiting change = g_waiting;
        ImGui::CloseCurrentPopup();
        Commit(change);
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Nothing happens at all, including to the request - the editor is
        // left exactly as it was.
        g_waiting = Waiting{};
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace editor
