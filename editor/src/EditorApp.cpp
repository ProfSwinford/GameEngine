// ============================================================================
//  EditorApp.cpp - the editor's shell. See EditorApp.h.
//
//  The interesting part of this file is Run(), which decides the order things
//  happen in during one frame of the editor. That order is not obvious and
//  every step of it is there for a reason.
// ============================================================================

#include "EditorApp.h"

#include "panels/AssetBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/GamePanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ScenePanel.h"

#include <imgui.h>

#include <cstdio>

namespace editor {

EditorState& EditorState::Get() {
    static EditorState state;
    return state;
}

bool EditorApp::Init() {
    // The engine first, then the panels. The interface needs a window, so the
    // window has to exist before it - which is why the editor's interface is a
    // subsystem inside the engine's ordered start-up rather than something the
    // editor starts on its own.
    eng::Engine::Options options;
    options.withEditorGui = true;

    if (!eng::Engine::Get().Init(options)) {
        return false;
    }

    // The editor opens in EDIT MODE, like Unity: the clock starts paused, so
    // nothing simulates until Play is pressed. Without this the scene would be
    // running the moment the window appeared and every edit would be fighting
    // whatever system owns that value.
    eng::Engine::Get().Clock().SetPaused(true);

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
    AddPanel(std::make_unique<ConsolePanel>());        // the log

    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor ready with {} panels",
                    m_panels.size());
    return true;
}

void EditorApp::AddPanel(std::unique_ptr<Panel> panel) {
    m_panels.push_back(std::move(panel));
}

void EditorApp::RefreshSceneList() {
    if (!eng::FileSystem::ListFiles("scenes", ".json", m_sceneList)) {
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
    ImGui::TextDisabled("for example: scenes/my_level.json");

    if (submitted || ImGui::Button("Save", ImVec2(110, 0))) {
        SaveScene(m_saveAsPath);
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
    EditorState::Get().selected = eng::EntityId{};

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

        // The list of scenes is DISCOVERED, not written down. Dropping a .json
        // into assets/scenes/ puts it in this menu with no rebuild.
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
            std::snprintf(m_saveAsPath, sizeof(m_saveAsPath), "%s",
                          hasPath ? scene.SourcePath().c_str() : "scenes/untitled.json");
            m_openSaveAsPopup = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Close Scene", nullptr, false, scene.IsLoaded())) {
            EditorState::Get().selected = eng::EntityId{};
            scene.Unload();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            eng::Engine::Get().RequestQuit();
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
    if (m_status[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_status);
    }

    ImGui::EndMenuBar();
}

void EditorApp::DrawPanels() {
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

void EditorApp::Run() {
    eng::Engine& engine = eng::Engine::Get();

    while (engine.BeginFrame()) {
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
        eng::EditorGui::BeginFrame();

        engine.Simulate();

        eng::EditorGui::BeginDockspace();
        DrawMenuBar();
        eng::EditorGui::EndDockspace();
        DrawPanels();
        DrawSaveAsPopup();
        FocusGameViewIfRequested();

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
        eng::EditorGui::SetGameInputFocus(m_gamePanel != nullptr &&
                                          m_gamePanel->HasFocus() &&
                                          engine.IsInPlayMode());

        // Step 5: the interface draws, on top of the world.
        eng::EditorGui::EndFrame();
        engine.PresentFrame();
    }
}

void EditorApp::Shutdown() {
    // Panels before the engine. A panel's destructor may still reach engine
    // state, so it has to go first - the same reverse-order rule the engine
    // applies to its own subsystems.
    m_panels.clear();
    eng::Engine::Get().Shutdown();
}

} // namespace editor
