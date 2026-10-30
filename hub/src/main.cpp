// ============================================================================
//  hub - the launcher. Picks a project, picks an engine, starts the editor.
//
//  ==========================================================================
//  WHY A SEPARATE PROGRAM AND NOT A SCREEN IN THE EDITOR
//
//  Because of the one thing it has to do that the editor cannot: choose WHICH
//  ENGINE to run. An editor is built against one engine.dll and loads it at
//  start-up; it cannot offer to open a project with a different one, because
//  by the time it could ask, it has already loaded its own. A launcher that
//  is a separate executable just starts a different editor.
//
//  That is also why switching projects restarts the editor rather than
//  re-rooting in place - see editor/src/ProjectMenu.h. The two decisions hold
//  each other up: because launching a project is already "start an executable
//  with an argument", engine version management costs no engine code at all.
//
//  ==========================================================================
//  IT HAS NO PROJECT, AND THAT SHAPES EVERYTHING
//
//  This is the one program in the tree that runs with no project open, which
//  means FileSystem has no root and FileSystem::Resolve returns nothing. So:
//
//    It does NOT call Engine::Init. The engine's ordered start-up brings up
//    twelve subsystems, most of which exist to run a game out of a project
//    folder. The Hub brings up the three it actually needs - the log, a
//    window and a renderer - by hand, exactly as editor --build-scripts
//    brings up the two IT needs. Those are objects with Init and Shutdown;
//    there is no privileged path being used here.
//
//    Every path it touches is a REAL path. eng::Project, eng::EngineBuild and
//    eng::UserPaths were all written to work this way for precisely this
//    reason - see the long note at the top of Project.h.
//
//    Its ImGui layout goes in the user's settings folder rather than a
//    project's .editor/, because there is no project to put it in.
//
//  ==========================================================================
//  WHAT IT DELIBERATELY DOES NOT DO
//
//  It does not download or install engine versions. An engine "install" here
//  is a folder somebody points it at that contains an editor and an
//  engine-version.json - which a developer's own build/debug/bin already is.
//  Downloading implies a release server and an install layout that this
//  project does not have yet, and inventing half of one now would be
//  something to tear up later.
//
//  It does not manage addons yet either. The Addons tab says so rather than
//  being absent, because a tab that explains what is coming is more use than
//  a surprise. The SEAM is already fixed: a project names its addons in
//  project.oproj and they live at <project>/addons/<name>/, so they travel
//  with the project in version control.
// ============================================================================

#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/fs/UserPaths.h>
#include <engine/platform/FolderDialog.h>
#include <engine/platform/Window.h>
#include <engine/project/EngineBuild.h>
#include <engine/project/Project.h>
#include <engine/render/Renderer.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// What the folder picker is being asked for. One dialog, one pending answer -
// see FolderDialog.h.
constexpr int kPurposeAddProject = 1;
constexpr int kPurposeNewProject = 2;
constexpr int kPurposeAddEngine  = 3;

// ---------------------------------------------------------------------------
//  THE TWO LISTS, both kept with the user rather than with any project.
//
//  projects.json is THE SAME FILE the editor's File -> Recent Projects reads
//  and writes. Deliberately: a project opened in the editor should appear
//  here, and one opened here should appear there. Two lists that are nearly
//  the same is worse than one.
// ---------------------------------------------------------------------------
constexpr const char* kProjectsFile = "projects.json";
constexpr const char* kEnginesFile  = "engines.json";

struct ProjectRow {
    std::string root;
    std::string name;

    // Worked out when the list is loaded, not stored: a folder can be moved
    // or deleted between runs, and a list that cannot say so sends somebody
    // to an editor that fails to start.
    bool        missing = false;
    std::string engineVersion;
};

struct EngineRow {
    std::string directory;         // holds editor.exe and engine-version.json
    eng::EngineBuildInfo build;
    bool                 missing = false;
};

std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

bool WriteWholeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

// ---- projects -------------------------------------------------------------

std::vector<ProjectRow> LoadProjects() {
    std::vector<ProjectRow> rows;

    const std::string path = eng::UserPaths::File(kProjectsFile);
    if (path.empty()) {
        return rows;
    }

    std::string error;
    const eng::Json document = eng::ParseJson(ReadWholeFile(path), error);
    if (!error.empty() || !document.is_object() || !document.contains("projects") ||
        !document["projects"].is_array()) {
        return rows;
    }

    for (const eng::Json& entry : document["projects"]) {
        if (!entry.is_object()) {
            continue;
        }
        ProjectRow row;
        row.root = eng::ReadString(entry, "root", "", kProjectsFile);
        row.name = eng::ReadString(entry, "name", "", kProjectsFile);
        if (row.root.empty()) {
            continue;
        }

        // CHECKED NOW, every time the list is shown. The alternative is a row
        // that looks fine, launches an editor, and the editor exits with
        // "that is not a project" - which reads as the editor being broken.
        eng::ProjectInfo info;
        std::string      readError;
        if (eng::Project::Read(row.root, info, readError)) {
            if (!info.name.empty()) {
                row.name = info.name;   // the manifest is the authority
            }
            row.engineVersion = info.engineVersion;
        } else {
            row.missing = true;
        }
        rows.push_back(row);
    }
    return rows;
}

void SaveProjects(const std::vector<ProjectRow>& rows) {
    const std::string path = eng::UserPaths::File(kProjectsFile);
    if (path.empty()) {
        return;
    }

    eng::Json list = eng::Json::array();
    for (const ProjectRow& row : rows) {
        eng::Json entry = eng::Json::object();
        entry["root"]   = row.root;
        entry["name"]   = row.name;
        list.push_back(entry);
    }

    eng::Json document   = eng::Json::object();
    document["_comment"] = "Projects you have opened, newest first. Shared by the hub "
                           "and the editor's File -> Recent Projects. Safe to delete; "
                           "it is only a list.";
    document["projects"] = list;
    WriteWholeFile(path, document.dump(2) + "\n");
}

void RememberProject(std::vector<ProjectRow>& rows, const std::string& root,
                     const std::string& name) {
    const fs::path wanted = fs::path(root).lexically_normal();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&wanted](const ProjectRow& row) {
                                  return fs::path(row.root).lexically_normal() == wanted;
                              }),
               rows.end());

    ProjectRow row;
    row.root = root;
    row.name = name;
    rows.insert(rows.begin(), row);
    SaveProjects(rows);
    rows = LoadProjects();   // re-read so the freshness checks are filled in
}

// ---- engines --------------------------------------------------------------

std::vector<EngineRow> LoadEngines() {
    std::vector<EngineRow> rows;

    const std::string path = eng::UserPaths::File(kEnginesFile);
    if (!path.empty()) {
        std::string error;
        const eng::Json document = eng::ParseJson(ReadWholeFile(path), error);
        if (error.empty() && document.is_object() && document.contains("engines") &&
            document["engines"].is_array()) {
            for (const eng::Json& entry : document["engines"]) {
                if (!entry.is_string()) {
                    continue;
                }
                EngineRow row;
                row.directory = entry.get<std::string>();
                row.build     = eng::EngineBuild::ReadFrom(row.directory);

                std::error_code ec;
#if defined(_WIN32)
                row.missing = !fs::exists(fs::path(row.directory) / "editor.exe", ec);
#else
                row.missing = !fs::exists(fs::path(row.directory) / "editor", ec);
#endif
                rows.push_back(row);
            }
        }
    }

    // ---- THE ENGINE THIS HUB SHIPPED WITH, ALWAYS PRESENT ----------------
    //
    // The Hub sits beside an editor and an engine.dll, so there is always at
    // least one engine available and it never has to be registered. A
    // launcher whose list is empty on first run looks broken.
    const std::string here = [] {
        if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
            fs::path path(base);
            if (path.filename().empty() && path.has_parent_path()) {
                path = path.parent_path();
            }
            return path.generic_string();
        }
        return std::string{};
    }();

    const bool alreadyListed =
        std::any_of(rows.begin(), rows.end(), [&here](const EngineRow& row) {
            return fs::path(row.directory).lexically_normal() ==
                   fs::path(here).lexically_normal();
        });

    if (!here.empty() && !alreadyListed) {
        EngineRow row;
        row.directory = here;
        row.build     = eng::EngineBuild::ReadFrom(here);
        rows.insert(rows.begin(), row);
    }
    return rows;
}

void SaveEngines(const std::vector<EngineRow>& rows) {
    const std::string path = eng::UserPaths::File(kEnginesFile);
    if (path.empty()) {
        return;
    }

    eng::Json list = eng::Json::array();
    for (const EngineRow& row : rows) {
        list.push_back(row.directory);
    }

    eng::Json document   = eng::Json::object();
    document["_comment"] = "Folders holding an editor and an engine-version.json. The "
                           "one the hub itself sits in is always available and is not "
                           "listed here.";
    document["engines"]  = list;
    WriteWholeFile(path, document.dump(2) + "\n");
}

// ---- launching ------------------------------------------------------------

bool StartEditor(const std::string& engineDirectory, const std::string& projectRoot,
                 std::string& outError) {
#if defined(_WIN32)
    const fs::path exe = fs::path(engineDirectory) / "editor.exe";
#else
    const fs::path exe = fs::path(engineDirectory) / "editor";
#endif

    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        outError = "'" + exe.string() + "' is not there";
        return false;
    }

    const std::string exePath = exe.string();
    const char*       args[]  = {exePath.c_str(), "--project", projectRoot.c_str(),
                                 nullptr};

    SDL_Process* child = SDL_CreateProcess(args, false);
    if (child == nullptr) {
        const char* reason = SDL_GetError();
        outError           = (reason != nullptr && reason[0] != '\0')
                                 ? reason
                                 : "the operating system did not say why";
        return false;
    }

    // Let go without waiting and without killing it - the editor outlives the
    // Hub that started it. See editor/src/Relaunch.cpp, which does the same
    // thing for the same reason.
    SDL_DestroyProcess(child);
    return true;
}

// ---------------------------------------------------------------------------
//  State the window needs between frames.
// ---------------------------------------------------------------------------
struct Hub {
    std::vector<ProjectRow> projects;
    std::vector<EngineRow>  engines;

    int         chosenEngine = 0;
    std::string status;
    std::string newProjectParent;
    char        newProjectName[96] = "";
    std::string newProjectError;
    bool        openNewProjectPopup = false;

    // Set when an editor has been started. The Hub then closes: two windows
    // for one task is one window too many, and the editor is the one being
    // worked in. Reopening the Hub is how you come back.
    bool launched = false;
};

const std::string& EngineDirectoryFor(const Hub& hub) {
    static const std::string none;
    if (hub.engines.empty()) {
        return none;
    }
    const int index =
        std::clamp(hub.chosenEngine, 0, static_cast<int>(hub.engines.size()) - 1);
    return hub.engines[static_cast<std::size_t>(index)].directory;
}

void Open(Hub& hub, const ProjectRow& row) {
    if (row.missing) {
        hub.status = "'" + row.root + "' is not there any more";
        return;
    }
    if (hub.engines.empty()) {
        hub.status = "there is no engine to open it with";
        return;
    }

    std::string error;
    if (!StartEditor(EngineDirectoryFor(hub), row.root, error)) {
        hub.status = "could not start the editor: " + error;
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not start the editor: {}", error);
        return;
    }

    RememberProject(hub.projects, row.root, row.name);
    hub.launched = true;
}

void DrawProjectsTab(Hub& hub) {
    if (ImGui::Button("Add Existing...")) {
        eng::FolderDialog::Ask(kPurposeAddProject);
    }
    ImGui::SameLine();
    if (ImGui::Button("New Project...")) {
        eng::FolderDialog::Ask(kPurposeNewProject);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        hub.projects = LoadProjects();
    }

    ImGui::Separator();

    if (hub.projects.empty()) {
        ImGui::TextDisabled("No projects yet.");
        ImGui::TextDisabled("Add Existing... points at a folder you already have;");
        ImGui::TextDisabled("New Project... makes one.");
        return;
    }

    if (ImGui::BeginTable("projects", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.16f);

        for (std::size_t i = 0; i < hub.projects.size(); ++i) {
            const ProjectRow& row = hub.projects[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableNextColumn();
            if (row.missing) {
                // RED AND SAID, not hidden. A project whose folder has moved
                // is the commonest stale entry there is, and removing it
                // silently would lose the only record of where it was.
                ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "%s",
                                   row.name.empty() ? "(missing)" : row.name.c_str());
            } else {
                ImGui::TextUnformatted(row.name.c_str());
                if (!row.engineVersion.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", row.engineVersion.c_str());
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.root.c_str());

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(row.missing);
            if (ImGui::Button("Open")) {
                Open(hub, row);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Forget")) {
                // FORGETS THE ENTRY, NOT THE PROJECT. Nothing on disk is
                // touched; this is a list, and saying so in the tooltip is
                // what stops somebody being afraid of the button.
                hub.projects.erase(hub.projects.begin() + static_cast<long>(i));
                SaveProjects(hub.projects);
                ImGui::PopID();
                break;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Removes it from this list. Nothing on disk is "
                                  "deleted.");
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void DrawEnginesTab(Hub& hub) {
    ImGui::TextWrapped("An engine is a folder holding an editor and an "
                       "engine-version.json. Your own build folders already are one.");
    ImGui::Separator();

    if (ImGui::Button("Add...")) {
        eng::FolderDialog::Ask(kPurposeAddEngine);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        hub.engines = LoadEngines();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Projects open with the engine selected here.");

    for (std::size_t i = 0; i < hub.engines.size(); ++i) {
        const EngineRow& row = hub.engines[i];
        ImGui::PushID(static_cast<int>(i));

        const std::string label =
            (row.build.known ? row.build.id : std::string("unknown")) + "  -  " +
            row.directory;

        if (ImGui::RadioButton(label.c_str(), hub.chosenEngine == static_cast<int>(i))) {
            hub.chosenEngine = static_cast<int>(i);
        }
        if (row.missing) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "(no editor here)");
        } else if (!row.build.known) {
            ImGui::SameLine();
            ImGui::TextDisabled("(no %s - built before versions were recorded)",
                                eng::EngineBuild::kFileName);
        }

        ImGui::PopID();
    }
}

void DrawAddonsTab() {
    // SAYS WHAT IS COMING rather than being absent, because the decisions
    // that are expensive to reverse have already been made and are worth
    // writing down where somebody will read them.
    ImGui::TextWrapped("Addons are not implemented yet.");
    ImGui::Spacing();
    ImGui::TextWrapped("Two things about them are already fixed, because they are the "
                       "expensive ones to change later:");
    ImGui::Bullet();
    ImGui::TextWrapped("A project names the addons it uses in its project.oproj, by "
                       "name rather than by path - so nothing can point at a folder "
                       "that has been moved.");
    ImGui::Bullet();
    ImGui::TextWrapped("They live at <project>/addons/<name>/, inside the project. That "
                       "means an addon travels with the project in version control, and "
                       "the editor can compile scripts an addon provides without being "
                       "told where to look.");
}

void HandlePicker(Hub& hub) {
    const eng::FolderDialog::Result picked = eng::FolderDialog::Take();
    if (picked.choice == eng::FolderDialog::Choice::None) {
        return;
    }
    if (picked.choice == eng::FolderDialog::Choice::Cancelled) {
        hub.status = "nothing was picked";
        return;
    }
    if (picked.choice == eng::FolderDialog::Choice::Failed) {
        hub.status = "the folder picker could not run: " + picked.error;
        return;
    }

    if (picked.purpose == kPurposeAddProject) {
        // WALKS UP, so picking a folder inside a project adds the project.
        const std::string root = eng::Project::FindAbove(picked.path);
        if (root.empty()) {
            hub.status = "'" + picked.path + "' is not a project";
            return;
        }
        eng::ProjectInfo info;
        std::string      error;
        eng::Project::Read(root, info, error);
        RememberProject(hub.projects, info.realRoot, info.name);
        hub.status = "added '" + info.name + "'";
        return;
    }

    if (picked.purpose == kPurposeNewProject) {
        hub.newProjectParent     = picked.path;
        hub.newProjectName[0]    = '\0';
        hub.newProjectError.clear();
        hub.openNewProjectPopup = true;
        return;
    }

    if (picked.purpose == kPurposeAddEngine) {
        EngineRow row;
        row.directory = picked.path;
        row.build     = eng::EngineBuild::ReadFrom(picked.path);

        std::error_code ec;
#if defined(_WIN32)
        const bool hasEditor = fs::exists(fs::path(picked.path) / "editor.exe", ec);
#else
        const bool hasEditor = fs::exists(fs::path(picked.path) / "editor", ec);
#endif
        if (!hasEditor) {
            // REFUSED WITH THE REASON. Adding a folder with no editor in it
            // would produce a row that cannot open anything, and the failure
            // would arrive later and somewhere else.
            hub.status = "'" + picked.path + "' has no editor in it";
            return;
        }

        hub.engines.push_back(row);
        SaveEngines(hub.engines);
        hub.engines = LoadEngines();
        hub.status  = "added the engine in '" + picked.path + "'";
    }
}

void DrawNewProjectPopup(Hub& hub) {
    if (hub.openNewProjectPopup) {
        ImGui::OpenPopup("New Project");
        hub.openNewProjectPopup = false;
    }

    if (!ImGui::BeginPopupModal("New Project", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("A new project will be created in:");
    ImGui::TextWrapped("%s", hub.newProjectParent.c_str());
    ImGui::Separator();

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered =
        ImGui::InputText("Name", hub.newProjectName, sizeof(hub.newProjectName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    std::string nameError;
    const bool  usable = eng::Project::NameIsUsable(hub.newProjectName, nameError);
    if (!usable && hub.newProjectName[0] != '\0') {
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "%s", nameError.c_str());
    } else if (!hub.newProjectError.empty()) {
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.31f, 1.0f), "%s",
                           hub.newProjectError.c_str());
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
        if (!eng::Project::Create(hub.newProjectParent, hub.newProjectName, made,
                                  error)) {
            hub.newProjectError = error;
        } else {
            made.engineVersion = eng::EngineBuild::ReadFrom(EngineDirectoryFor(hub)).id;
            std::string writeError;
            eng::Project::Write(made, writeError);

            RememberProject(hub.projects, made.realRoot, made.name);
            ImGui::CloseCurrentPopup();

            ProjectRow row;
            row.root = made.realRoot;
            row.name = made.name;
            Open(hub, row);
        }
    }

    ImGui::EndPopup();
}

} // namespace

int main(int argc, char** argv) {
    // --frames N: run that many frames and exit normally.
    //
    // The same option the editor and the player have, for the same reason -
    // it proves the thing starts, draws and takes itself down cleanly, with
    // nobody at the keyboard. A launcher is exactly the sort of program that
    // would otherwise only ever be tested by hand.
    int frameLimit = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frameLimit = std::atoi(argv[++i]);
            continue;
        }
        const bool askedForHelp = std::strcmp(argv[i], "--help") == 0;
        std::printf("hub - pick a project and open it\n\n"
                    "  (no options)    open the launcher\n"
                    "  --frames <N>    open it, run N frames, then exit\n");
        return askedForHelp ? 0 : 1;
    }

#if defined(_WIN32)
    // The same trick the editor uses, for the same reason: a GUI program has
    // no console of its own, so anything printed goes nowhere unless it was
    // started from a terminal. See editor/src/main.cpp.
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
        FILE* reopened = nullptr;
        freopen_s(&reopened, "CONOUT$", "w", stdout);
        freopen_s(&reopened, "CONOUT$", "w", stderr);
    }
#endif

    // ---- the three subsystems this needs, by hand -------------------------
    //
    // No Engine::Init: see the header note. These are ordinary objects with
    // Init and Shutdown, which is the same way editor --build-scripts starts
    // the two IT needs.
    //
    // NOTE THERE IS NO FileSystem. The Hub has no project, so there is
    // nothing for one to be rooted at - which is also why the log below gets
    // no file and prints to the terminal only.
    eng::BootConfig config;
    config.windowTitle  = "Engine2D";
    config.windowWidth  = 900;
    config.windowHeight = 560;
    config.logFile.clear();   // nowhere to put one; see above

    eng::Log log;
    log.Init(config);

    eng::Window window;
    if (!window.Init(config)) {
        std::fprintf(stderr, "the hub could not open a window\n");
        log.Shutdown();
        return 1;
    }
    if (!eng::Renderer::Init(window)) {
        std::fprintf(stderr, "the hub could not start a renderer\n");
        window.Shutdown();
        log.Shutdown();
        return 1;
    }

    // ---- ImGui ------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // WHERE THE LAYOUT GOES. The editor keeps its ImGui ini in the project's
    // .editor/ folder, because a panel arrangement belongs to the project
    // being worked on. The Hub has no project, so its own layout goes with
    // the user's other settings.
    //
    // THE STRING HAS TO OUTLIVE THIS SCOPE: io.IniFilename is a const char*
    // that ImGui keeps rather than copies. A local would leave it reading
    // freed memory on the first save. Same note as EditorGui.cpp.
    static std::string iniPath = eng::UserPaths::File("hub-imgui.ini");
    io.IniFilename             = iniPath.empty() ? nullptr : iniPath.c_str();

    ImGui::StyleColorsDark();
    auto* sdlWindow   = static_cast<SDL_Window*>(window.NativeWindowHandle());
    auto* sdlRenderer = static_cast<SDL_Renderer*>(eng::Renderer::NativeRendererHandle());
    ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow, sdlRenderer);
    ImGui_ImplSDLRenderer3_Init(sdlRenderer);

    Hub hub;
    hub.projects = LoadProjects();
    hub.engines  = LoadEngines();

    ENGINE_LOG_INFO(eng::Channels::kCore, "hub ready: {} project(s), {} engine(s)",
                    hub.projects.size(), hub.engines.size());

    int  frames  = 0;
    bool running = true;
    while (running && !hub.launched) {
        if (frameLimit > 0 && frames++ >= frameLimit) {
            break;
        }
        // The Hub reads SDL's events directly rather than through the
        // engine's EventPump, because ImGui's backend needs the raw events
        // and there is no game here to translate them for.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(sdlWindow)) {
                running = false;
            }
        }

        HandlePicker(hub);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Engine2D", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Projects")) {
                DrawProjectsTab(hub);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Engines")) {
                DrawEnginesTab(hub);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Addons")) {
                DrawAddonsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        DrawNewProjectPopup(hub);

        // The status line, last and at the bottom. Every outcome the Hub has
        // reaches it, including the two that look like nothing happening - a
        // dismissed dialog and a folder that was not a project.
        if (!hub.status.empty()) {
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 28.0f);
            ImGui::Separator();
            ImGui::TextDisabled("%s", hub.status.c_str());
        }

        ImGui::End();
        ImGui::Render();

        SDL_SetRenderDrawColor(sdlRenderer, 22, 24, 28, 255);
        SDL_RenderClear(sdlRenderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdlRenderer);
        SDL_RenderPresent(sdlRenderer);
    }

    // Reverse of the order above, which is the rule everywhere in this engine.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    eng::Renderer::Shutdown();
    window.Shutdown();
    log.Shutdown();

    SDL_Quit();
    return 0;
}
