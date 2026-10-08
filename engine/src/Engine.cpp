// =============================================================================
//  Engine.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Engine.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/Engine.h>
#include <SDL3/SDL.h>

namespace eng {

// Returns the one and only engine. Created the first time it is asked for, so
// it is guaranteed to exist before anything tries to use it.
Engine& Engine::Get() {
    static Engine instance;
    return instance;
}

// Hands back the game window, so the editor can attach its interface to it.
Window& Engine::GetWindow() {
    return m_window;
}

bool Engine::RendererSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    if (!Renderer::Init(engine.m_window)) {
        return false;
    }

    engine.m_camera.SetViewportSize(Renderer::OutputSize());

    return true;
}

void Engine::RendererSubsystem::Shutdown() 
{
    Renderer::Shutdown();
}

bool Engine::GuiSubsystem::Init(const BootConfig&) {
    return m_init ? m_init() : true; // return true by default if no editor is present
}

void Engine::GuiSubsystem::Use(std::function<bool()> init, std::function<void()> shutdown) {
    m_init = std::move(init);
    m_shutdown = std::move(shutdown);
}

void Engine::GuiSubsystem::Shutdown() {
    if (m_shutdown)
        m_shutdown();
}

bool Engine::InputSubsystem::Init(const BootConfig&) {
    const Json& document = Engine::Get().m_configDocument;

    std::string warnings;
    if (document.contains("input")) {
        InputMap::LoadBindings(document["input"], warnings);
    } else {
        ENGINE_LOG_WARN(Channels::kInput,
                        "the settings file has no \"input\" section, so no controls "
                        "are bound.");
    }

    InputMap::PushContext("gameplay");
    return true;
}

void Engine::InputSubsystem::Shutdown() {
    InputMap::ClearBindings();
}

bool Engine::SceneSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    ComponentFactory::RegisterBuiltins();
    CollisionSystem::RegisterComponentTypes();
    SpinSystem::RegisterComponentTypes();
    ScriptSystem::RegisterComponentTypes();

    engine.m_spinSystem = std::make_unique<SpinSystem>();
    engine.m_scriptSystem = std::make_unique<ScriptSystem>();
    SystemScheduler::Register(engine.m_spinSystem.get());
    SystemScheduler::Register(engine.m_scriptSystem.get());

    engine.m_scene = std::make_unique<Scene>();
    Scene::SetActive(engine.m_scene.get());
    return true;
}

void Engine::SceneSubsystem::Shutdown() {
    Engine& engine = Engine::Get();

    if (engine.m_scene != nullptr) {
        engine.m_scene->Unload();
    }
    if (engine.m_spinSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_spinSystem.get());
        engine.m_spinSystem.reset();
    }
    if (engine.m_scriptSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_scriptSystem.get());
        engine.m_scriptSystem.reset();
    }
    SpinSystem::Clear();
    ScriptSystem::Clear();
    SpriteRenderSystem::Clear();
    Scene::SetActive(nullptr);
    engine.m_scene.reset();
}

bool Engine::CollisionSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    engine.m_collisionSystem = std::make_unique<CollisionSystem>();
    SystemScheduler::Register(engine.m_collisionSystem.get());

    ScriptSystem::SubscribeToCollisions();
    return true;
}

void Engine::CollisionSubsystem::Shutdown() {
    Engine& engine = Engine::Get();

    if (engine.m_collisionSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_collisionSystem.get());
    }
    CollisionSystem::Clear();
    engine.m_collisionSystem.reset();
}


// Builds the ordered list of subsystems. Registration order IS dependency
// order, and shutdown runs it in reverse: Log, FileSystem, Window, Renderer,
// EditorGui, Input, Resources, Gizmos, Messaging, Scripts, Scene, Collision.
void Engine::RegisterBuiltinSubsystems(const Options& options) { 

    m_subsystems.Add("Log", m_log);
    m_subsystems.Add("FileSystem", m_fileSystem);
    m_subsystems.Add("Window", m_window);
    m_subsystems.Add("Renderer", m_renderer);

    if (options.guiInit) {
        m_gui.Use(options.guiInit, options.guiShutdown);
        m_subsystems.Add("EditorGui", m_gui);
    }

    m_subsystems.Add("Input", m_input);
    m_subsystems.Add("Resources", m_resources);
    m_subsystems.Add("Gizmos", m_gizmos);
    m_subsystems.Add("Messaging", m_messaging);
    m_subsystems.Add("Scripts", m_scripts);
    m_subsystems.Add("Scene", m_sceneSubsystem);
    m_subsystems.Add("Collision", m_collisionSubsystem);
}

// Starts everything: reads the settings file, brings the subsystems up in
// order, sets the clock, and loads the starting scene. Returns false if the
// engine cannot run at all.
bool Engine::Init(const Options& options) {
    m_fileSystem.Init(m_config);
    std::string configError;

    if (!LoadBootConfig(options.configPath, m_config, m_configDocument, configError)) {
        std::fprintf(stderr, "settings error: %s\n", configError.c_str());
        return false;
    }

    RegisterBuiltinSubsystems(options);

    ENGINE_LOG_INFO(Channels::kCore, "starting {} subsystems in order.", m_subsystems.Count());

    if (!m_subsystems.InitAll(m_config)) {
        return false;
    }

    m_clock.Init();
    m_clock.SetFixedStepSeconds(m_config.fixedTimestepSeconds);
    m_clock.SetMaxStepsPerFrame(m_config.maxStepsPerFrame);

    SystemScheduler::LogOrder();

    const std::string scene =
        options.sceneOverride.empty() ? m_config.startupScene : options.sceneOverride;

    if (!scene.empty()) {
        std::string sceneError;
        if (!LoadScene(scene, sceneError)) {
            ENGINE_LOG_ERROR(Channels::kScene, "the starting scene '{}' did not load: {}", scene,
                             sceneError);
        }
    }

    m_lastFrameTicks = static_cast<double>(SDL_GetPerformanceCounter());
    m_initialized = true;
    ENGINE_LOG_INFO(Channels::kCore, "Engine Ready.");

    return true;
}

// Stops everything, in the exact reverse of the order it was started in.
void Engine::Shutdown() {
    if (!m_initialized) {
        m_subsystems.ShutdownAll();
        return;
    }

    ENGINE_LOG_INFO(Channels::kCore, "shutting engine down");

    SystemScheduler::Clear();
    m_subsystems.ShutdownAll();
    m_initialized = false;

    SDL_Quit();
}

// ---------------------------------------------------------------------------
//  Scenes
// ---------------------------------------------------------------------------

bool Engine::LoadScene(std::string_view virtualPath, std::string& outError) {
    if (m_scene == nullptr) {
        outError = "the scene subsystem is not running";
        return false;
    }
    if (!m_scene->Load(virtualPath, outError)) {
        return false;
    }
    m_camera.SetPosition(m_scene->InitialCameraPosition());
    m_camera.SetZoom(m_scene->InitialCameraZoom());
    return true;
}

bool Engine::SaveScene(std::string_view virtualPath, std::string& outError) {
    if (m_scene == nullptr) {
        outError = "the scene subsystem is not running";
        return false;
    }

    const std::string target =
        virtualPath.empty() ? m_scene->SourcePath() : std::string(virtualPath);
    if (target.empty()) {
        outError = "this scene has never been saved anywhere; use Save Scene As";
        return false;
    }

    // The live camera goes in FIRST, so that framing a shot in the editor and
    // pressing save keeps the framing. Doing it here rather than inside
    // Scene::Save means the scene does not have to know a camera exists.
    m_scene->SetCameraState(m_camera.Position(), m_camera.Zoom());

    return m_scene->Save(target, outError);
}

// ---------------------------------------------------------------------------
//  Play mode
// ---------------------------------------------------------------------------

bool Engine::EnterPlayMode(std::string& outError) {
    if (m_inPlayMode || m_scene == nullptr) {
        return m_inPlayMode;
    }
    if (!m_scene->SaveToString(m_playModeSnapshot, outError)) {
        // Refuse rather than play unsafely. Entering play mode without a
        // snapshot means Stop cannot put the scene back, and silently turning
        // a safe action into a destructive one is the worst possible failure
        // for this feature.
        ENGINE_LOG_ERROR(Channels::kEditor,
                         "cannot enter play mode, because the scene could not be "
                         "snapshotted: {}",
                         outError);
        return false;
    }
    m_inPlayMode = true;
    m_clock.SetPaused(false);
    ENGINE_LOG_INFO(Channels::kEditor, "play mode started");
    return true;
}

void Engine::ExitPlayMode() {
    if (!m_inPlayMode) {
        return;
    }
    m_inPlayMode = false;
    m_clock.SetPaused(true);

    // Anything still queued belongs to the play session and must not be
    // applied to the restored scene - a destroy queued on the last frame of
    // play would otherwise delete an entity in the freshly restored one.
    DeferredOps::Clear();
    MessageBus::Clear();

    if (m_scene != nullptr && !m_playModeSnapshot.empty()) {
        std::string error;
        if (!m_scene->LoadFromString(m_playModeSnapshot, error)) {
            ENGINE_LOG_ERROR(Channels::kEditor,
                             "play mode ended but the scene could not be restored: {}", error);
        } else {
            ENGINE_LOG_INFO(Channels::kEditor, "play mode stopped; scene restored");
        }
        m_camera.SetPosition(m_scene->InitialCameraPosition());
        m_camera.SetZoom(m_scene->InitialCameraZoom());
    }
    m_playModeSnapshot.clear();
}

// Starts one frame: measures real time, reads input, and works out how many
// fixed simulation steps this frame owes. Returns false when it is time to quit.
bool Engine::BeginFrame() {
    const double now = static_cast<double>(SDL_GetPerformanceCounter());
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double delta = (now - m_lastFrameTicks) / frequency;
    m_lastFrameTicks = now;

    // The first frame after loading a scene can be seconds long. Feeding that
    // straight into the clock would ask for hundreds of simulation steps at
    // once, so it is capped at a quarter of a second.
    delta = std::min(delta, 0.25);

    ResourceManager::PruneCache();

    m_events.Poll();
    InputMap::Update(m_events);

    if (m_events.QuitRequested()) {
        m_quitRequested = true;
    }

    m_camera.SetViewportSize(Renderer::OutputSize());
    
    m_stepsThisFrame = m_clock.BeginFrame(delta);
    return !m_quitRequested;
}

// Runs the simulation steps this frame owes, in system order: gameplay,
// movement, collision, messages, create/destroy, camera.
void Engine::Simulate() {
    int step = 0;
    for (step = 0; step < m_stepsThisFrame; step++) {
        const float fixedStep = m_clock.FixedStepSeconds();

        // Stages 100-500
        SystemScheduler::UpdateRange(0, SystemStage::kCollisionResponse, fixedStep);

        // Stage 500
        MessageBus::Dispatch();

        // Stage 600
        if (m_scene != nullptr) {
            DeferredOps::Apply(*m_scene);
        }

        // Stage 700 - the Camera
        SystemScheduler::UpdateRange(SystemStage::kDeferred + 1, SystemStage::kFirstRenderStage,
                                     fixedStep);

        m_clock.OnStepConsumed();
    }
    if (m_scene != nullptr && step == 0) {
        DeferredOps::Apply(*m_scene);
    }
}

// Draws the world through any camera into whatever is currently being drawn
// into. The editor calls this twice - once per view.
void Engine::RenderWorld(Camera& camera, bool includeGizmos) {
    camera.SetViewportSize(Renderer::OutputSize());

    Renderer::Clear(Color{18, 18, 22, 255});

    SpriteRenderSystem::Render(camera);

    SystemScheduler::RenderPass(m_clock.RealDeltaSeconds());

    if (includeGizmos) {
        Gizmos::Render(camera);
    }

}

// Draws one frame for the standalone game, gizmos included.
void Engine::RenderFrame() {
    RenderWorld(m_camera, true);

    Gizmos::EndFrame(m_clock.RealDeltaSeconds());
}

// Shows the frame that was just drawn.
void Engine::PresentFrame() {
    Renderer::Present();
}

// The standalone game's whole loop: begin, simulate, render, present, repeat.
void Engine::Run() {
    while (BeginFrame()) {
        Simulate();
        RenderFrame();
        PresentFrame();
    }
}

} // namespace eng
