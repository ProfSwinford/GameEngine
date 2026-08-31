// WEEK 2 - the IDE shell. See EditorApp.h.

#include "EditorApp.h"

#include "panels/AssetBrowserPanel.h"
#include "panels/CVarPanel.h"
#include "panels/EventInspectorPanel.h"
#include "panels/GamePanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/JobPanel.h"
#include "panels/LogPanel.h"
#include "panels/MemoryPanel.h"
#include "panels/ProfilerPanel.h"
#include "panels/ScenePanel.h"
#include "panels/ViewportPanel.h"

#include <imgui.h>

namespace editor {

EditorState& EditorState::Get() {
    static EditorState state;
    return state;
}

bool EditorApp::Init() {
    // ORDERING: the engine first, then the panels. The GUI needs a window, so
    // the window comes first - the same lesson as everywhere else in this
    // course, and the reason EditorGui is a subsystem inside the engine's
    // ordered boot rather than something the editor starts on its own.
    eng::Engine::Options options;
    options.withEditorGui = true;

    if (!eng::Engine::Get().Init(options)) {
        return false;
    }

    // The editor opens in EDIT MODE, like Unity: the clock starts paused, so
    // nothing simulates until Play is pressed. Without this the scene would be
    // running the moment the window appeared, and every edit would be fighting
    // the systems.
    eng::Engine::Get().Clock().SetPaused(true);

    // Adding a panel is one line. This is what that promise looks like after
    // nine weeks of keeping it.
    //
    // The two VIEWS are held by pointer as well, because EditorApp has to call
    // RenderView on them after every panel has drawn - see Run().
    auto scene = std::make_unique<ScenePanel>();
    auto game  = std::make_unique<GamePanel>();
    m_scenePanel = scene.get();
    m_gamePanel  = game.get();

    AddPanel(std::move(scene));                          // the Scene view
    AddPanel(std::move(game));                           // the Game view
    AddPanel(std::make_unique<ToolbarPanel>());          // Week 10
    AddPanel(std::make_unique<HierarchyPanel>());        // Week 9
    AddPanel(std::make_unique<InspectorPanel>());        // Week 10
    AddPanel(std::make_unique<LogPanel>());              // Week 3
    AddPanel(std::make_unique<ProfilerPanel>());         // Week 4
    AddPanel(std::make_unique<MemoryPanel>());           // Week 7
    AddPanel(std::make_unique<ResourcePanel>());         // Week 9
    AddPanel(std::make_unique<AssetBrowserPanel>());     // files on disk
    AddPanel(std::make_unique<CVarPanel>());             // Week 8
    AddPanel(std::make_unique<ViewportPanel>());         // Week 6
    AddPanel(std::make_unique<DebugDrawPanel>());        // Week 6
    AddPanel(std::make_unique<JobPanel>());              // Week 5
    AddPanel(std::make_unique<EventInspectorPanel>(eng::Engine::Get().Events()));  // Week 2

    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor up with {} panels", m_panels.size());
    return true;
}

void EditorApp::AddPanel(std::unique_ptr<Panel> panel) {
    m_panels.push_back(std::move(panel));
}

void EditorApp::RefreshSceneList() {
    if (!eng::FileSystem::ListFiles("scenes", ".json", m_sceneList)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "could not list assets/scenes/ - the Open Scene menu will be "
                        "empty");
    }
}

void EditorApp::SaveScene(const std::string& virtualPath) {
    std::string error;
    if (eng::Engine::Get().SaveScene(virtualPath, error)) {
        EditorState::Get().dirty = false;
        std::snprintf(m_status, sizeof(m_status), "saved %s",
                      eng::Engine::Get().GetScene().SourcePath().c_str());
        // The scene list may have gained a file.
        m_sceneList.clear();
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

    // Centred, because a modal that opens under the mouse in a docked IDE is
    // as likely to be off-screen as not.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Save Scene As", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Virtual path, relative to assets/");
    ImGui::SetNextItemWidth(420.0f);
    const bool submitted = ImGui::InputText("##saveaspath", m_saveAsPath,
                                            sizeof(m_saveAsPath),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("e.g. scenes/my_level.json");

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
    // SELECTION IS CLEARED FIRST, and it matters. Loading destroys every
    // entity, and a selection surviving into the new scene would name a slot a
    // DIFFERENT entity now occupies. The handle would be caught as stale and
    // reported - which is the mechanism working - but showing nothing is
    // better than showing a warning about a selection the user did not make.
    EditorState::Get().selected = eng::EntityHandle{};

    std::string error;
    if (!eng::Engine::Get().LoadScene(virtualPath, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not load '{}': {}", virtualPath,
                         error);
    }
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        eng::Scene& scene = eng::Engine::Get().GetScene();

        // OPEN SCENE - the scenes are DISCOVERED, not listed in code. Adding a
        // .json to assets/scenes/ puts it in this menu with no rebuild, which
        // is the same property the scene format itself has and would be an odd
        // one for the editor to break.
        if (ImGui::BeginMenu("Open Scene")) {
            // Refreshed when the menu is opened rather than every frame: it
            // touches the disk, and a directory scan per frame for a menu
            // almost nobody has open is exactly the kind of cost a debug tool
            // should not impose on the thing it is observing.
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
            m_sceneList.clear();   // rescan next time it is opened
        }

        const bool hasPath = !scene.SourcePath().empty();

        // Disabled rather than hidden when there is nothing to reload, so the
        // menu does not change shape underneath someone.
        ImGui::BeginDisabled(!hasPath);
        if (ImGui::MenuItem("Reload Scene", nullptr, false, hasPath)) {
            LoadScene(scene.SourcePath());
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        // SAVE. Disabled when there is nowhere to save to, rather than failing
        // after the click.
        ImGui::BeginDisabled(!hasPath || !scene.IsLoaded());
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, hasPath && scene.IsLoaded())) {
            SaveScene({});
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Save Scene As...", nullptr, false, scene.IsLoaded())) {
            // Pre-filled with the current path so "save a variant" is an edit
            // rather than retyping.
            std::snprintf(m_saveAsPath, sizeof(m_saveAsPath), "%s",
                          hasPath ? scene.SourcePath().c_str() : "scenes/untitled.json");
            m_openSaveAsPopup = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Unload Scene", nullptr, false, scene.IsLoaded())) {
            // The Milestone 3 demonstration, one click: watch the Resources
            // panel's total refcount go to zero. Reload Scene stays enabled
            // afterwards, because Scene::SourcePath() survives an unload.
            EditorState::Get().selected = eng::EntityHandle{};
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
        // THE LOOP THAT MEANS THE MENU CODE IS NEVER TOUCHED AGAIN. Written
        // in Week 2; every panel added since appeared in this menu for free.
        for (const std::unique_ptr<Panel>& panel : m_panels) {
            ImGui::MenuItem(panel->Title(), nullptr, panel->OpenFlag());
        }
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &EditorState::Get().showImGuiDemo);
        ImGui::EndMenu();
    }

    // A permanent frame-time readout, because the number you most want is the
    // one you have to open a panel to see.
    const eng::TimerStats frame = eng::TimerRegistry::Get("Engine::RenderFrame");
    ImGui::Separator();
    ImGui::Text("%.1f FPS | render %.2f ms | tick %llu", static_cast<double>(ImGui::GetIO().Framerate),
                frame.AverageMs(),
                static_cast<unsigned long long>(eng::Engine::Get().Clock().TickCount()));

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
    // The Begin/End pair lives HERE, not in Draw(). Decided in Week 2 and
    // applied to every panel since - see the note in Panel.h.
    for (const std::unique_ptr<Panel>& panel : m_panels) {
        if (!panel->IsOpen()) {
            panel->OnHidden();
            continue;
        }
        // Begin returns FALSE for a collapsed window or a background tab in a
        // dock node - the panel is open but not visible this frame. Telling the
        // panel so is what keeps cached per-frame state from going stale; see
        // Panel::OnHidden.
        if (ImGui::Begin(panel->Title(), panel->OpenFlag())) {
            panel->Draw();
        } else {
            panel->OnHidden();
        }
        ImGui::End();
    }

    if (EditorState::Get().showImGuiDemo) {
        // In the build on purpose. It is the best ImGui reference there is:
        // every widget on screen, live, with a "show source" button beside
        // each. Better than any tutorial, most of which are written against
        // much older versions and hand you obsoleted functions.
        ImGui::ShowDemoWindow(&EditorState::Get().showImGuiDemo);
    }
}

void EditorApp::FocusGameViewIfRequested() {
    if (!EditorState::Get().focusGameView || m_gamePanel == nullptr) {
        return;
    }

    // A panel that was CLOSED has never been submitted to ImGui, so there is no
    // window of that name to focus yet. Open it and try again next frame, once
    // DrawPanels has created it - one frame of delay on a case almost nobody
    // hits, rather than a focus call that silently does nothing.
    if (!m_gamePanel->IsOpen()) {
        m_gamePanel->SetOpen(true);
        return;
    }

    EditorState::Get().focusGameView = false;

    // BY NAME rather than through the panel, because when Play is pressed the
    // Game view is the BACKGROUND TAB - which is precisely the case where the
    // panel's own Draw() does not run. See the note in GamePanel.h.
    ImGui::SetWindowFocus(m_gamePanel->Title());
}

void EditorApp::Run() {
    eng::Engine& engine = eng::Engine::Get();

    while (engine.BeginFrame()) {
        // ORDER OF THE FRAME, and it took one wrong version to get right.
        //
        // ImGui::NewFrame has to come after the platform events have been
        // handed to its backend, which happens inside engine.BeginFrame() -
        // that part is fixed.
        //
        // The PANELS are drawn AFTER Simulate rather than before. The first
        // version drew them first, and the Memory panel read zero all session:
        // BeginFrame clears the frame stack, so a panel drawn before the
        // simulation sees an allocator that nothing has used yet. A debug tool
        // that observes the frame BEFORE the frame happens is reporting last
        // frame's world with this frame's label on it.
        //
        // Then the two VIEWS render, after the panels, because a view sizes its
        // render target from the content region ImGui only knows once the panel
        // has been laid out. Which is also why the editor calls RenderWorld
        // itself rather than Engine::RenderFrame: RenderFrame draws to the
        // window with the game camera, and there is no longer anything on the
        // window to draw to.
        eng::EditorGui::BeginFrame();

        engine.Simulate();

        // MEASURE THE IDE ITSELF (Week 4). An editor costing 8 ms a frame
        // quietly corrupts every measurement taken in Weeks 5, 7 and 10, so it
        // gets a row in the profiler table like anything else. Same discipline
        // as measuring the timer's own overhead, one level up.
        {
            ENGINE_SCOPED_TIMER("Editor::Draw");

            eng::EditorGui::BeginDockspace();
            DrawMenuBar();
            eng::EditorGui::EndDockspace();
            DrawPanels();
            DrawSaveAsPopup();
            FocusGameViewIfRequested();

            // Ctrl+S. Checked here rather than through InputMap, because a
            // tool shortcut is not a game action - binding it through the
            // context stack would make it reachable from a replay and would
            // put an editor concern in the game's config file.
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) &&
                !io.WantTextInput) {
                if (!engine.GetScene().SourcePath().empty()) {
                    SaveScene({});
                }
            }
        }

        // ---- THE TWO VIEWS RENDER HERE, and the position is exact ---------
        //
        // AFTER the panels have drawn, because each view sizes its render
        // target from its own content region and submits ImGui::Image with the
        // resulting texture id. BEFORE EditorGui::EndFrame, because that is
        // when ImGui actually samples those textures - so filling them now
        // means the images are current this frame rather than one behind.
        if (m_scenePanel != nullptr && m_scenePanel->IsOpen()) {
            m_scenePanel->RenderView();
        }
        if (m_gamePanel != nullptr && m_gamePanel->IsOpen()) {
            m_gamePanel->RenderView();
        }

        // A scene open requested from the Asset Browser, applied HERE - after
        // every panel has finished with the entities it was describing. See
        // the note on EditorState::requestedScene.
        if (!EditorState::Get().requestedScene.empty()) {
            const std::string path = EditorState::Get().requestedScene;
            EditorState::Get().requestedScene.clear();
            LoadScene(path);
        }

        // The debug-draw queue is aged ONCE, after both views have drawn it.
        // Ageing inside Render - which is what it used to do - meant whichever
        // view drew second got an empty queue.
        eng::DebugDraw::EndFrame(engine.Clock().RealDeltaSeconds());

        // Back to the window, and clear it. Everything the user sees is now a
        // panel, so this is just the space behind them.
        eng::Renderer::SetRenderTarget(nullptr);
        eng::Renderer::Clear(eng::Color{12, 12, 15, 255});

        // The keyboard follows focus, and focus followed Play a moment ago.
        eng::EditorGui::SetGameInputFocus(m_gamePanel != nullptr &&
                                          m_gamePanel->HasFocus() &&
                                          engine.IsInPlayMode());

        // EndFrame renders the ImGui draw data, AFTER the world has been drawn
        // into the view textures - so the IDE lands on top of the game.
        eng::EditorGui::EndFrame();
        engine.PresentFrame();
    }
}

void EditorApp::Shutdown() {
    // Panels before the engine: a panel destructor may still reference engine
    // state, and the reverse order is the whole lesson of Week 3 and Week 7.
    m_panels.clear();
    eng::Engine::Get().Shutdown();
}

} // namespace editor
