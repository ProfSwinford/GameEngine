// ============================================================================
//  EditorApp.cpp - the editor's shell. See EditorApp.h.
//
//  The interesting part of this file is Run(), which decides the order things
//  happen in during one frame of the editor. That order is not obvious and
//  every step of it is there for a reason.
// ============================================================================

#include "EditorApp.h"

#include <algorithm>

#include "EditorGui.h"
#include "ObjectMenu.h"
#include "EditorSession.h"
#include "GameExport.h"
#include "ScriptBuild.h"
#include "ProjectMenu.h"
#include "Relaunch.h"

#include "panels/AssetBrowserPanel.h"
#include "panels/BuildPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/GamePanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ScenePanel.h"
#include "panels/TilePalettePanel.h"

#include <imgui.h>

#include <cstdio>

namespace editor {

EditorState& EditorState::Get() {
    static EditorState state;
    return state;
}

// ---------------------------------------------------------------------------
//  The selection. See the note at the top of EditorApp.h.
// ---------------------------------------------------------------------------

bool EditorState::IsSelected(eng::EntityId id) const {
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

void EditorState::SelectOnly(eng::EntityId id) {
    selection.clear();
    if (!id.IsNull()) {
        selection.push_back(id);
    }
    selected = id;
}

void EditorState::AddToSelection(eng::EntityId id) {
    if (id.IsNull()) {
        return;
    }
    if (!IsSelected(id)) {
        selection.push_back(id);
    }
    // Active even when it was already in the list, because a Shift-click
    // onto something already selected still means "this is where I am now",
    // and the next range should measure from here.
    selected = id;
}

void EditorState::ToggleSelection(eng::EntityId id) {
    if (id.IsNull()) {
        return;
    }
    const auto it = std::find(selection.begin(), selection.end(), id);
    if (it == selection.end()) {
        selection.push_back(id);
        selected = id;
        return;
    }

    selection.erase(it);

    // Ctrl-clicking the ACTIVE entity removes it, so something else has to
    // become active - otherwise the Inspector would keep showing an entity
    // that is no longer selected.
    if (selected == id) {
        selected = selection.empty() ? eng::EntityId{} : selection.back();
    }
}

void EditorState::ClearSelection() {
    selection.clear();
    selected = eng::EntityId{};
}

void EditorState::PruneSelection() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::erase_if(selection, [&scene](eng::EntityId id) {
        return scene.Get(id) == nullptr;
    });

    // The active entity has to be one of the survivors. Falling back to the
    // last of them rather than to nothing keeps a Shift-range working after
    // one of its members is deleted, which is the common case.
    if (scene.Get(selected) == nullptr) {
        selected = selection.empty() ? eng::EntityId{} : selection.back();
    }
}

bool EditorApp::Init(const std::string& projectRoot) {
    // The engine first, then the panels. The interface needs a window, so the
    // window has to exist before it - which is why the editor's interface is a
    // subsystem inside the engine's ordered start-up rather than something the
    // editor starts on its own.
    eng::Engine::Options options;

    // WHICH PROJECT. Empty is the normal case and means "look upwards from
    // this executable", which is what opens the project the editor is
    // installed inside. Anything else came from --project, and a folder that
    // is not a project makes Init fail rather than silently opening a
    // different one. See eng::FileSystem::UseRoot.
    options.projectRoot = projectRoot;

    // The engine does not know what ImGui is. It is handed two functions and
    // calls them at the right moment in its ordered start-up - after the
    // window and renderer exist, and before they are destroyed.
    options.guiInit     = [] { return EditorGui::Init(eng::Engine::Get().GetWindow()); };
    options.guiShutdown = [] { EditorGui::Shutdown(); };

    if (!eng::Engine::Get().Init(options)) {
        return false;
    }

    // The editor opens in EDIT MODE, like Unity: the clock starts paused, so
    // nothing simulates until Play is pressed. Without this the scene would be
    // running the moment the window appeared and every edit would be fighting
    // whatever system owns that value.
    eng::Engine::Get().Clock().SetPaused(true);

    // Find a C++ compiler, and bring the project's scripts up to date if they
    // have been edited since they were last built - which is the usual case
    // after somebody has been working on them outside the editor.
    //
    // This happens AFTER the engine has started so that the log, the file
    // system and the script loader all exist. The engine's start-up will
    // already have loaded whatever library was there; this replaces it when it
    // is out of date, using exactly the same reload path a later edit uses.
    ScriptBuild::Init();
    RebuildScriptsIfChanged();

    // Adding a panel is one line. The two VIEWS are also kept by pointer,
    // because EditorApp has to call RenderView on them after every panel has
    // drawn - see Run().
    auto scene = std::make_unique<ScenePanel>();
    auto game  = std::make_unique<GamePanel>();
    m_scenePanel = scene.get();
    m_gamePanel  = game.get();

    AddPanel(std::move(scene));                        // Scene   - edit the world
    AddPanel(std::move(game));                         // Game    - play the world
    AddPanel(std::make_unique<ToolbarPanel>());        // Play / Pause / Step
    AddPanel(std::make_unique<HierarchyPanel>());      // every entity
    AddPanel(std::make_unique<InspectorPanel>());      // the selected entity
    AddPanel(std::make_unique<AssetBrowserPanel>());   // files on disk
    auto tiles         = std::make_unique<TilePalettePanel>();
    m_tilePalettePanel = tiles.get();
    AddPanel(std::move(tiles));                        // tiles, and the brush
    AddPanel(std::make_unique<BuildPanel>());          // what ships, and in what order
    AddPanel(std::make_unique<ConsolePanel>());        // the log

    // LAST, because it may open a different scene and move the Scene view's
    // camera - so it needs the engine running and every panel already built.
    EditorSession::Restore(m_scenePanel);

    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor ready with {} panels",
                    m_panels.size());
    m_ready = true;
    return true;
}

void EditorApp::AddPanel(std::unique_ptr<Panel> panel) {
    m_panels.push_back(std::move(panel));
}

void EditorApp::RefreshSceneList() {
    if (!eng::FileSystem::ListFiles("scenes", eng::kSceneExtension, m_sceneList)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "could not read assets/scenes/, so the Open Scene menu will be "
                        "empty");
    }
}

void EditorApp::SaveScene(const std::string& virtualPath) {
    std::string error;
    if (eng::Engine::Get().SaveScene(virtualPath, error)) {
        EditorState::Get().dirty = false;
        std::snprintf(m_status, sizeof(m_status), "saved %s",
                      eng::Engine::Get().GetScene().SourcePath().c_str());
        m_sceneList.clear();   // the list may have gained a file
    } else {
        std::snprintf(m_status, sizeof(m_status), "save failed: %s", error.c_str());
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "save failed: {}", error);
    }
}

void EditorApp::DrawSaveAsPopup() {
    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        m_openSaveAsPopup = false;
    }

    // Centred, because a dialog that opens under the mouse in a docked editor
    // is as likely to be half off the screen as not.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Save Scene As", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("File name, relative to assets/");
    ImGui::SetNextItemWidth(420.0f);
    const bool submitted =
        ImGui::InputText("##saveaspath", m_saveAsPath, sizeof(m_saveAsPath),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("for example: scenes/my_level%s", eng::kSceneExtension);

    if (submitted || ImGui::Button("Save", ImVec2(110, 0))) {
        // A NAME TYPED WITHOUT THE EXTENSION STILL SAVES A SCENE.
        //
        // Typing "scenes/boss" used to write a file called exactly that, with
        // no extension at all - which then failed every test for "is this a
        // scene?". So the save succeeded, said so in the status bar, and the
        // file never appeared in the Open Scene menu again. Work that has been
        // saved and cannot be found is worse than a save that refuses, and the
        // person it happens to has no reason to suspect the file name.
        //
        // An EMPTY box is left alone rather than turned into ".oscene".
        // SaveScene({}) already means "save where it came from", which is the
        // sane reading of clearing the box and pressing Save.
        std::string path(m_saveAsPath);
        if (!path.empty() && !eng::FileSystem::HasExtension(path, eng::kSceneExtension)) {
            path += eng::kSceneExtension;
        }
        SaveScene(path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::LoadScene(const std::string& virtualPath) {
    // THE SELECTION IS CLEARED FIRST, and it matters. Loading destroys every
    // entity, and a selection surviving into the new scene would name a slot
    // that a DIFFERENT entity now occupies. The id would be recognised as out
    // of date - which is the mechanism working - but showing nothing at all is
    // better than showing a warning about a selection nobody made.
    EditorState::Get().ClearSelection();

    std::string error;
    if (!eng::Engine::Get().LoadScene(virtualPath, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", virtualPath,
                         error);
    }
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        eng::Scene& scene = eng::Engine::Get().GetScene();

        // THE PROJECT ITEMS FIRST, above the scene ones, because a project
        // contains scenes rather than the other way round and the menu should
        // read in that order.
        ProjectMenu::Draw();
        ImGui::Separator();

        // The list of scenes is DISCOVERED, not written down. Dropping a
        // .oscene into assets/scenes/ puts it in this menu with no rebuild.
        if (ImGui::BeginMenu("Open Scene")) {
            // Read when the menu is opened rather than every frame: it touches
            // the disk, and scanning a folder sixty times a second for a menu
            // almost nobody has open is exactly the cost a tool should not
            // impose on the thing it is helping with.
            if (m_sceneList.empty()) {
                RefreshSceneList();
            }

            if (m_sceneList.empty()) {
                ImGui::TextDisabled("no scenes found in assets/scenes/");
            }
            for (const std::string& path : m_sceneList) {
                const bool current = (path == scene.SourcePath());
                if (ImGui::MenuItem(path.c_str(), nullptr, current)) {
                    LoadScene(path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rescan")) {
                RefreshSceneList();
            }
            ImGui::EndMenu();
        } else {
            m_sceneList.clear();   // read it again next time the menu opens
        }

        const bool hasPath = !scene.SourcePath().empty();

        // Greyed out rather than hidden when there is nothing to reload, so
        // the menu does not change shape underneath somebody.
        ImGui::BeginDisabled(!hasPath);
        if (ImGui::MenuItem("Reload Scene", nullptr, false, hasPath)) {
            LoadScene(scene.SourcePath());
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        ImGui::BeginDisabled(!hasPath || !scene.IsLoaded());
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, hasPath && scene.IsLoaded())) {
            SaveScene({});
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Save Scene As...", nullptr, false, scene.IsLoaded())) {
            // Pre-filled with the current name, so "save a variant of this" is
            // an edit rather than retyping the whole path.
            if (hasPath) {
                std::snprintf(m_saveAsPath, sizeof(m_saveAsPath), "%s",
                              scene.SourcePath().c_str());
            } else {
                std::snprintf(m_saveAsPath, sizeof(m_saveAsPath), "scenes/untitled%s",
                              eng::kSceneExtension);
            }
            m_openSaveAsPopup = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Close Scene", nullptr, false, scene.IsLoaded())) {
            EditorState::Get().ClearSelection();
            scene.Unload();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Separator();

        // BUILD THE GAME. Asks for a folder and copies a standalone game into
        // it - see GameExport.h for what that folder contains and why.
        //
        // The dialog is opened here but the export is NOT run here. The folder
        // picker answers later, possibly on another thread, and exporting
        // rebuilds the scripts - which replaces the library the running
        // scene's script objects live in. Both reasons point the same way: the
        // work happens at the top of a frame, in Run(). It is the same
        // argument requestedScene makes, and the editor does not get an
        // exemption from its own rules.
        if (ImGui::MenuItem("Build Game...")) {
            GameExport::AskForDestination();
            std::snprintf(m_status, sizeof(m_status), "choose a folder to build into");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Builds the scripts and copies a standalone game - the "
                              "player, the engine, your assets and your settings - into "
                              "a folder you choose.");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit")) {
            eng::Engine::Get().RequestQuit();
        }
        ImGui::EndMenu();
    }

    // ---- Objects -----------------------------------------------------------
    //
    // The same items the Hierarchy's right-click menu offers, drawn from the
    // one list so the two cannot drift apart. See ObjectMenu.h.
    if (ImGui::BeginMenu("Objects")) {
        if (const eng::EntityId created = DrawObjectMenuItems(); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().dirty    = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        // The loop that means this menu never has to be edited again. Every
        // panel ever added appears here for free.
        for (const std::unique_ptr<Panel>& panel : m_panels) {
            ImGui::MenuItem(panel->Title(), nullptr, panel->OpenFlag());
        }
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &EditorState::Get().showImGuiDemo);
        ImGui::EndMenu();
    }

    // A permanent frame-rate readout, because the number you most want is the
    // one you would otherwise have to open a panel to see.
    ImGui::Separator();
    ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));

    if (EditorState::Get().dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "| UNSAVED");
    }
    if (m_scriptBuildFailed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "| SCRIPTS DID NOT BUILD");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The compiler's messages are in the Console. Your scripts "
                              "are not running until this is fixed.");
        }
    }
    if (m_status[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_status);
    }

    ImGui::EndMenuBar();
}

void EditorApp::DrawPanels() {
    // BEFORE ANY PANEL READS IT. An entity destroyed last frame leaves its id
    // behind in the selection, and a panel that found nothing there would
    // have to cope with a hole. One sweep here means none of them do.
    EditorState::Get().PruneSelection();

    // The Begin/End pair lives HERE, not inside each panel's Draw. See the
    // note in Panel.h.
    for (const std::unique_ptr<Panel>& panel : m_panels) {
        if (!panel->IsOpen()) {
            panel->OnHidden();
            continue;
        }
        // Begin returns FALSE for a collapsed window or a background tab - the
        // panel is open but not visible this frame. Telling the panel so is
        // what keeps it from acting on stale information; see Panel::OnHidden.
        if (ImGui::Begin(panel->Title(), panel->OpenFlag())) {
            panel->Draw();
        } else {
            panel->OnHidden();
        }
        ImGui::End();
    }

    if (EditorState::Get().showImGuiDemo) {
        // Left in on purpose. It is the best reference for ImGui there is:
        // every widget on screen, live, with a "show source" button beside
        // each one. Better than any tutorial, most of which are written
        // against much older versions.
        ImGui::ShowDemoWindow(&EditorState::Get().showImGuiDemo);
    }
}

void EditorApp::FocusGameViewIfRequested() {
    if (!EditorState::Get().focusGameView || m_gamePanel == nullptr) {
        return;
    }

    // A panel that is CLOSED has never been shown to ImGui, so there is no
    // window of that name to focus yet. Open it and try again next frame -
    // one frame of delay in a case almost nobody hits, rather than a focus
    // call that silently does nothing.
    if (!m_gamePanel->IsOpen()) {
        m_gamePanel->SetOpen(true);
        return;
    }

    EditorState::Get().focusGameView = false;

    // Focused BY NAME rather than through the panel, because when Play is
    // pressed the Game view is the BACKGROUND TAB - which is precisely the
    // case where its own Draw does not run. See GamePanel.h.
    ImGui::SetWindowFocus(m_gamePanel->Title());
}

void EditorApp::RevealTilePaletteIfRequested() {
    TilePaintState& tiles = TilePaintState::Get();
    if (!tiles.revealRequested || m_tilePalettePanel == nullptr) {
        return;
    }

    // Opened first and focused on the NEXT frame, exactly as the Game view is
    // and for the same reason: a closed panel has never been shown to ImGui,
    // so there is no window of that name to focus yet.
    if (!m_tilePalettePanel->IsOpen()) {
        m_tilePalettePanel->SetOpen(true);
        return;
    }

    tiles.revealRequested = false;
    ImGui::SetWindowFocus(m_tilePalettePanel->Title());
}
void EditorApp::RebuildScriptsIfChanged() {
    if (!ScriptBuild::NeedsRebuild()) {
        return;   // the usual case, and it costs only a few timestamp checks
    }

    eng::Engine& engine = eng::Engine::Get();

    // Play mode is stopped first, the way Unity does when it recompiles.
    //
    // It is not optional here. Reloading destroys every running script object,
    // so a play session cannot survive it - and stopping restores the scene to
    // how it was authored, which is a far better place to come back to than a
    // half-played one with its behaviours missing.
    if (engine.IsInPlayMode()) {
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "scripts changed, so play mode was stopped before rebuilding");
        engine.ExitPlayMode();
    }

    // STARTED, not waited for. The editor keeps drawing while the compiler
    // runs; the answer is collected in Run() once it is ready.
    //
    // BeginAsync refuses when one is already in flight, which is what stops
    // a rebuild being started again on every frame that asks - see
    // ScriptBuild.h.
    if (ScriptBuild::BeginAsync()) {
        std::snprintf(m_status, sizeof(m_status), "building scripts...");
    }

    // Push the log to disk now rather than letting it sit in the buffer.
    //
    // A build is exactly the kind of thing you want a record of afterwards,
    // and the most interesting case - a script that compiled and then brought
    // the editor down - is the one where an unflushed buffer would be lost.
    eng::Log::Flush();
}

void EditorApp::Run(int frameLimit) {
    eng::Engine& engine = eng::Engine::Get();

    int frames = 0;

    while (engine.BeginFrame()) {
        if (frameLimit > 0 && frames++ >= frameLimit) {
            break;
        }

        // ==================================================================
        //  COLLECT A FINISHED BUILD, once a frame.
        //
        //  This is where the library is actually loaded and every script
        //  rebound - on the main thread, between frames, with nothing
        //  half way through walking the script list. The worker only ever
        //  ran the compiler. See ScriptBuild.h.
        // ==================================================================
        if (ScriptBuild::Result built; ScriptBuild::TakeResult(built)) {
            std::snprintf(m_status, sizeof(m_status), "%s", built.summary.c_str());
            m_scriptBuildFailed = !built.ok;

            // Flushed HERE rather than when the build started, because the
            // interesting case - a script that compiled and then brought the
            // editor down - produces its output during the build, not before.
            eng::Log::Flush();
        }

        // The user alt-tabbed back to the editor. That is the moment to check
        // whether any script was edited while it was in the background - see
        // ScriptBuild.h for why this is the natural trigger.
        if (engine.Events().FocusGainedThisFrame()) {
            RebuildScriptsIfChanged();
        }

        // The folder picker from File -> Build Game... answers whenever the
        // user gets round to it, so it is COLLECTED here rather than waited
        // for. This spot is deliberate: it is the same point in the frame
        // where a script rebuild is already known to be safe, and exporting
        // rebuilds the scripts.
        // EVERY OUTCOME IS REPORTED, including the two that used to be silent.
        // A Build that does nothing and says nothing is the worst way for this
        // to fail, and the failure is invisible precisely when somebody most
        // needs to know - see GameExport::Choice.
        //
        // These go to the log at ERROR rather than Info or Warning on purpose.
        // config/engine.json sets a threshold, and when it is "Error" - as it
        // is in this project right now - every Info and Warning line is
        // dropped before it reaches the Console panel OR the log file. A
        // problem with something the user just asked for has to survive that.
        // A folder picked for New or Open Project, collected at the same
        // point and for the same reason as the export's: it is the spot in
        // the frame where touching the scene and the engine is already known
        // to be safe.
        ProjectMenu::CollectPickerResult();

        switch (const GameExport::Pending pending = GameExport::TakePending();
                pending.choice) {
            case GameExport::Choice::None:
                break;

            case GameExport::Choice::Cancelled:
                std::snprintf(m_status, sizeof(m_status), "build cancelled");
                break;

            case GameExport::Choice::Failed:
                std::snprintf(m_status, sizeof(m_status), "the folder picker failed");
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "the folder picker could not run: {}", pending.error);
                break;

            case GameExport::Choice::Chosen: {
                const GameExport::Result exported = GameExport::ExportTo(pending.path);
                std::snprintf(m_status, sizeof(m_status), "%s", exported.summary.c_str());
                if (!exported.ok) {
                    ENGINE_LOG_ERROR(eng::Channels::kEditor, "build failed: {}",
                                     exported.summary);
                } else {
                    ENGINE_LOG_INFO(eng::Channels::kEditor, "build finished: {} -> {}",
                                     exported.summary, pending.path);
                }
                break;
            }
        }

        // ==================================================================
        //  THE ORDER OF ONE EDITOR FRAME, and every step is here for a reason.
        //
        //  1. Start the interface's frame. This has to come after the platform
        //     events have been handed to it, which happened inside BeginFrame.
        //
        //  2. Simulate. The panels are drawn AFTER this, not before. Drawing
        //     them first would mean every panel showed the state of the world
        //     BEFORE this frame's simulation - last frame's world with this
        //     frame's label on it.
        //
        //  3. Draw the panels.
        //
        //  4. THEN render the two views, because each one sizes its picture
        //     from the panel's content area, which ImGui only knows once the
        //     panel has been laid out.
        //
        //  5. Finish the interface's frame, which is when it actually draws -
        //     so the pictures filled in at step 4 are current rather than one
        //     frame behind.
        // ==================================================================
        EditorGui::BeginFrame();

        engine.Simulate();

        EditorGui::BeginDockspace();
        DrawMenuBar();
        EditorGui::EndDockspace();
        DrawPanels();
        DrawSaveAsPopup();

        // OUTSIDE the menu, because a modal opened inside a BeginMenu block
        // closes the moment the menu does - it would appear for one frame and
        // vanish. See ProjectMenu.h.
        DrawNewProjectPopup();
        DrawUnsavedProjectPopup();

        FocusGameViewIfRequested();
        RevealTilePaletteIfRequested();

        // The title carries the project, the scene and whether there is
        // unsaved work, all three of which change while the editor runs. It
        // only touches the window when the string is actually different.
        ProjectMenu::UpdateWindowTitle();

        // Ctrl+S. Checked here rather than going through InputMap, because a
        // tool shortcut is not a game action - binding it through the game's
        // input system would put an editor concern in the game's settings file.
        // WantTextInput stops it firing while somebody is typing in a box.
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.WantTextInput) {
            if (!engine.GetScene().SourcePath().empty()) {
                SaveScene({});
            }
        }

        // Step 4: the two views draw the world into their own pictures.
        if (m_scenePanel != nullptr && m_scenePanel->IsOpen()) {
            m_scenePanel->RenderView();
        }
        if (m_gamePanel != nullptr && m_gamePanel->IsOpen()) {
            m_gamePanel->RenderView();
        }

        // A scene somebody asked to open, applied HERE - after every panel has
        // finished with the entities it was describing. See
        // EditorState::requestedScene.
        if (!EditorState::Get().requestedScene.empty()) {
            const std::string path = EditorState::Get().requestedScene;
            EditorState::Get().requestedScene.clear();
            LoadScene(path);
        }

        // The gizmo queue is aged ONCE, after BOTH views have drawn it. Doing
        // it inside the drawing would leave whichever view went second with
        // nothing to draw.
        eng::Gizmos::EndFrame(engine.Clock().RealDeltaSeconds());

        // Back to the window, and clear it. Everything visible is a panel now,
        // so this is just the space behind them.
        eng::Renderer::SetRenderTarget(nullptr);
        eng::Renderer::Clear(eng::Color{12, 12, 15, 255});

        // The keyboard follows focus, and focus followed Play a moment ago.
        EditorGui::SetGameInputFocus(m_gamePanel != nullptr &&
                                          m_gamePanel->HasFocus() &&
                                          engine.IsInPlayMode());

        // Step 5: the interface draws, on top of the world.
        EditorGui::EndFrame();
        engine.PresentFrame();
    }
}

void EditorApp::Shutdown() {
    // ANY BUILD STILL RUNNING IS WAITED FOR, FIRST.
    //
    // A compiler writing into .build/ while the engine tears itself down
    // leaves a half-written library behind, and the worker would be touching
    // the log after the log had been shut down. Waiting is the one place in
    // this whole feature where blocking is the right answer: the editor is
    // closing, so there is nothing left to keep responsive.
    ScriptBuild::WaitForBuild();

    // THE SESSION IS SAVED FIRST, while there is still something to read.
    //
    // It needs the Scene view's camera, which lives in a panel, and the open
    // scene's path, which lives in the engine - so it has to happen before
    // either of them is taken down. Putting this one line after m_panels.clear()
    // would compile perfectly and quietly save nothing but a null camera.
    //
    // ---- AND ONLY IF THERE WAS EVER A SESSION ---------------------------
    //
    // main() calls Shutdown after a FAILED Init on purpose, so this path has
    // to be safe rather than merely unlikely. Init can now fail in the very
    // first subsystem - that is what `--project <not a project>` does - and
    // EditorSession::Save reaches Engine::GetScene(), which returns *m_scene
    // for a pointer the scene subsystem never got as far as setting. The
    // result was an access violation on the way out of a perfectly correct
    // error message.
    //
    // The same guard the engine keeps for the same reason; see the
    // m_initialised check at the top of Engine::Shutdown.
    if (m_ready) {
        EditorSession::Save(m_scenePanel);
    }

    // Panels before the engine. A panel's destructor may still reach engine
    // state, so it has to go first - the same reverse-order rule the engine
    // applies to its own subsystems.
    m_panels.clear();
    eng::Engine::Get().Shutdown();
}

} // namespace editor
