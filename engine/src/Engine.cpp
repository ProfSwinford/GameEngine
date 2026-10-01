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

    //m_subsystems.Add("Input", m_input);
    //m_subsystems.Add("Resources", m_resources);
    //m_subsystems.Add("Gizmos", m_gizmos);
    //m_subsystems.Add("Messaging", m_messaging);
    //m_subsystems.Add("Scripts", m_scripts);
    m_subsystems.Add("Scene", m_sceneSubsystem);
    //m_subsystems.Add("Collision", m_collisionSubsystem);

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

// Replaces the current scene with the one in the named file, and moves the
// camera to wherever that file says it should be.
bool Engine::LoadScene(std::string_view /*virtualPath*/, std::string& /*outError*/) {
    return false;
}

// Writes the current scene back out to a file, including where the camera is.
bool Engine::SaveScene(std::string_view /*virtualPath*/, std::string& /*outError*/) {
    return false;
}

// Takes a snapshot of the scene and starts running it. The snapshot is what
// makes pressing Play safe on a level you have been building.
bool Engine::EnterPlayMode(std::string& /*outError*/) {
    return false;
}

// Stops play mode and puts the snapshot back, undoing everything the running
// game did to the scene.
void Engine::ExitPlayMode() {
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
}

// Draws the world through any camera into whatever is currently being drawn
// into. The editor calls this twice - once per view.
void Engine::RenderWorld(Camera& /*camera*/, bool /*includeGizmos*/) {
}

// Draws one frame for the standalone game, gizmos included.
void Engine::RenderFrame() {

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
