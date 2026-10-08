// ============================================================================
//  Engine.cpp - starting the engine up, running one frame, and shutting down.
//  See Engine.h for the shape of a frame.
//
//  The most important thing in this file is RegisterBuiltinSubsystems, which
//  is where the engine's start-up ORDER is written down.
// ============================================================================

#include <engine/Engine.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace eng {

Engine& Engine::Get() {
    // Created the first time this is called. See the note in Subsystem.h about
    // why that is preferred over a plain global object.
    static Engine instance;
    return instance;
}

Window& Engine::GetWindow() {
    return m_window;
}

// ---------------------------------------------------------------------------
//  THE SUBSYSTEMS THAT BELONG TO THE ENGINE ITSELF.
//
//  Most of the engine's pieces are Subsystem classes in their own right. Log,
//  FileSystem, Window, ResourceManager, Gizmos, MessageBus and ScriptLibrary
//  each have their own Init and Shutdown, written in their own files, next to
//  the code they start. The engine keeps one object of each as a member and
//  does nothing more than put them in order.
//
//  The five below are the ones that cannot do that, because starting them is
//  not one call on one class - it is CONNECTING several pieces to each other.
//  That wiring is the engine's job, so it lives here. They are still ordinary
//  Subsystem classes, which is why the list at the bottom of this section
//  reads the same way for all twelve.
// ---------------------------------------------------------------------------

// The renderer, plus the camera that has to be told how big the window is.
//
// Renderer::Init is HANDED the window rather than going looking for one. That
// is what keeps the window out of the drawing code's list of things it depends
// on, and it is why this wiring is here rather than inside Renderer.
bool Engine::RendererSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    if (!Renderer::Init(engine.m_window)) {
        return false;
    }
    engine.m_camera.SetViewportSize(Renderer::OutputSize());
    return true;
}

void Engine::RendererSubsystem::Shutdown() {
    Renderer::Shutdown();
}

// The editor's interface.
//
// The editor supplies its own two functions through Engine::Options and the
// engine calls them without ever learning what they do. All the engine
// provides is the correct moment: after the renderer exists, and before it is
// destroyed. Getting that wrong is a crash at shutdown, which is why it is not
// left to the editor to remember.
//
// The standalone game supplies nothing, and that difference is what proves the
// engine ships without its tools attached.
void Engine::GuiSubsystem::Use(std::function<bool()> init,
                               std::function<void()> shutdown) {
    m_init     = std::move(init);
    m_shutdown = std::move(shutdown); 
}

bool Engine::GuiSubsystem::Init(const BootConfig&) {
    return m_init ? m_init() : true;
}

void Engine::GuiSubsystem::Shutdown() {
    if (m_shutdown) {
        m_shutdown();
    }
}

// The controls.
//
// The key bindings are not in BootConfig. They are a whole section of the
// settings file, read by InputMap itself, so the engine hands that section
// over here rather than copying every binding into BootConfig first.
bool Engine::InputSubsystem::Init(const BootConfig&) {
    const Json& document = Engine::Get().m_configDocument;

    std::string warnings;
    if (document.contains("input")) {
        InputMap::LoadBindings(document["input"], warnings);
    } else {
        ENGINE_LOG_WARN(Channels::kInput,
                        "the settings file has no \"input\" section, so no controls "
                        "are bound");
    }

    // The gameplay context sits at the bottom of the stack for the whole run;
    // a menu pushes on top of it. See InputMap.h.
    InputMap::PushContext("gameplay");
    return true;
}

void Engine::InputSubsystem::Shutdown() {
    InputMap::ClearBindings();
}

// The scene, and the systems that run on it.
//
// Needs textures (components load them as they attach), messaging, and the
// scripts, so a ScriptComponent finds its behaviour as the scene loads rather
// than a frame later.
bool Engine::SceneSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    // Tell the factory which type names mean which classes. Until this has
    // run, nothing in a scene file means anything.
    ComponentFactory::RegisterBuiltins();
    CollisionSystem::RegisterComponentTypes();
    SpinSystem::RegisterComponentTypes();
    ScriptSystem::RegisterComponentTypes();

    engine.m_spinSystem   = std::make_unique<SpinSystem>();
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

// Collision. Needs messaging and the scene.
bool Engine::CollisionSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    engine.m_collisionSystem = std::make_unique<CollisionSystem>();
    SystemScheduler::Register(engine.m_collisionSystem.get());

    // Scripts hear about collisions through the message bus, so this
    // subscription belongs after collision exists.
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

// ===========================================================================
//  THE START-UP ORDER. This list is the most important thing in the engine.
//
//  Every line hands over one object and a name. Adding one does NOT start it -
//  nothing runs until Init calls InitAll, further down this file.
//
//  The order they are added in IS the order they start in, and each one may
//  assume everything above it is already running. Shutting down happens in the
//  exact reverse, from the bottom of this list back up to the top.
//
//    Log         everything writes to it, including everything else's own
//                shutdown message, so it is first up and last down
//    FileSystem  turns "textures/player.bmp" into a real path
//    Window      needs the settings for its size
//    Renderer    needs the window it draws into
//    EditorGui   after the renderer exists, and only when there is an editor
//    Input       events come from the window
//    Resources   needs the file system to read textures, and the renderer to
//                hand them to the graphics card
//    Gizmos      needs the renderer
//    Messaging   nothing to start; it is in the list so that it is CLEARED
//                before collision on the way down, and collision sends
//                through it
//    Scripts     BEFORE the scene, which means it is unloaded AFTER it.
//                Unloading the scene destroys script objects that live in the
//                script library; unload the library first and the scene's
//                teardown would be calling destructors that no longer exist
//    Scene       needs textures, messaging and the scripts
//    Collision   needs messaging and the scene
// ===========================================================================
void Engine::RegisterBuiltinSubsystems(const Options& options) {
    m_subsystems.Add("Log",        m_log);
    m_subsystems.Add("FileSystem", m_fileSystem);
    m_subsystems.Add("Window",     m_window);
    m_subsystems.Add("Renderer",   m_renderer);

    // Only when the editor supplied them.
    if (options.guiInit) {
        m_gui.Use(options.guiInit, options.guiShutdown);
        m_subsystems.Add("EditorGui", m_gui);
    }

    m_subsystems.Add("Input",      m_input);
    m_subsystems.Add("Resources",  m_resources);
    m_subsystems.Add("Gizmos",     m_gizmos);
    m_subsystems.Add("Messaging",  m_messaging);
    m_subsystems.Add("Scripts",    m_scripts);
    m_subsystems.Add("Scene",      m_sceneSubsystem);
    m_subsystems.Add("Collision",  m_collisionSubsystem);
}
bool Engine::Init(const Options& options) {

    // Two things have to happen BEFORE the ordered start-up above: the
    // settings file has to be read (the log's level and the window's size come
    // from it), and the file system has to exist in order to read it.
    //
    // The file system is therefore started twice - once here, quietly, and
    // once inside the ordered list where it writes to the log and takes part
    // in the ordered shutdown. Starting it twice is harmless, and it ignores
    // the settings anyway, which is just as well: they have not been read yet
    // and m_config is still all defaults on this line.
    m_fileSystem.Init(m_config);

    std::string configError;
    if (!LoadBootConfig(options.configPath, m_config, m_configDocument, configError)) {
        // The log is not open yet, so this goes straight to the terminal.
        std::fprintf(stderr, "settings error: %s\n", configError.c_str());
        return false;
    }

    RegisterBuiltinSubsystems(options);

    ENGINE_LOG_INFO(Channels::kCore, "starting {} subsystems in order",
                    m_subsystems.Count());
    if (!m_subsystems.InitAll(m_config)) {
        // Everything that did start has already been shut down in reverse.
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
            // A scene that will not load is not a reason to refuse to start.
            // The editor is far more useful with an empty scene and a readable
            // error than not running at all.
            ENGINE_LOG_ERROR(Channels::kScene, "the starting scene '{}' did not load: {}",
                             scene, sceneError);
        }
    }

    // SDL_GetPerformanceCounter is SDL's high-resolution timer. It counts
    // ticks whose rate SDL_GetPerformanceFrequency reports, so dividing one by
    // the other gives seconds.
    m_lastFrameTicks = static_cast<double>(SDL_GetPerformanceCounter());
    m_initialised    = true;
    ENGINE_LOG_INFO(Channels::kCore, "engine ready");
    return true;
}

void Engine::Shutdown() {
    if (!m_initialised) {
        // Still unwind. A failed Init already cleaned up after itself, but a
        // caller that never called Init must not be punished for calling
        // Shutdown.
        m_subsystems.ShutdownAll();
        return;
    }
    ENGINE_LOG_INFO(Channels::kCore, "shutting down (reverse of the start-up order)");
    SystemScheduler::Clear();
    m_subsystems.ShutdownAll();
    m_initialised = false;

    // SDL_Quit exactly once, after every part of the engine that used SDL has
    // finished with it.
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
                         "snapshotted: {}", outError);
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
                             "play mode ended but the scene could not be restored: {}",
                             error);
        } else {
            ENGINE_LOG_INFO(Channels::kEditor, "play mode stopped; scene restored");
        }
        m_camera.SetPosition(m_scene->InitialCameraPosition());
        m_camera.SetZoom(m_scene->InitialCameraZoom());
    }
    m_playModeSnapshot.clear();
}

// ---------------------------------------------------------------------------
//  The frame
// ---------------------------------------------------------------------------

bool Engine::BeginFrame() {
    // How much real time has passed since the last frame.
    const double now       = static_cast<double>(SDL_GetPerformanceCounter());
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double       delta     = (now - m_lastFrameTicks) / frequency;
    m_lastFrameTicks       = now;

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

void Engine::Simulate() {
    for (int step = 0; step < m_stepsThisFrame; ++step) {
        const float fixedStep = m_clock.FixedStepSeconds();

        // Stages 100 to 500: gameplay, movement, collision. See SystemOrder.h.
        SystemScheduler::UpdateRange(0, SystemStage::kCollisionResponse, fixedStep);

        // Stage 500: deliver messages. Every handler runs here and nowhere else.
        MessageBus::Dispatch();

        // Stage 600: create and destroy entities, at one defined point.
        if (m_scene != nullptr) {
            DeferredOps::Apply(*m_scene);
        }

        // Stage 700: the camera, after everything it might follow has moved.
        SystemScheduler::UpdateRange(SystemStage::kDeferred + 1,
                                     SystemStage::kFirstRenderStage, fixedStep);

        m_clock.OnStepConsumed();
    }

    // AND ONCE MORE, OUTSIDE THE LOOP.
    //
    // Everything above happens only if the clock asked for at least one step,
    // and a PAUSED clock asks for none - which is exactly what the editor sits
    // in while you are editing. Without this line, anything queued while
    // paused is never applied: the editor's Destroy button marks an entity for
    // destruction and nothing ever acts on it, so the button appears to do
    // nothing until Play is pressed.
    //
    // This is the right place for it. The queue exists so that entities are
    // only taken apart when nothing is halfway through walking the list of
    // them, and the end of Simulate is such a moment whether any steps ran or
    // not. Draining an empty queue costs nothing, so there is nothing to ask
    // first.
    if (m_scene != nullptr) {
        DeferredOps::Apply(*m_scene);
    }
}

void Engine::RenderWorld(Camera& camera, bool includeGizmos) {
    // The camera sizes itself from whatever is currently being drawn into, so
    // this same call frames the world correctly whether it is filling the
    // whole window or a small panel in the editor.
    camera.SetViewportSize(Renderer::OutputSize());

    Renderer::Clear(Color{18, 18, 22, 255});

    SpriteRenderSystem::Render(camera);

    // Stages 800 and above, for anything a game wants drawn between the
    // sprites and the gizmos.
    SystemScheduler::RenderPass(m_clock.RealDeltaSeconds());

    // Gizmos last, so they land on top of everything else.
    if (includeGizmos) {
        Gizmos::Render(camera);
    }
}

void Engine::RenderFrame() {
    RenderWorld(m_camera, /*includeGizmos=*/true);

    // The standalone game has exactly one view, so it also ages the gizmo
    // queue here. The editor does this itself, after BOTH of its views have
    // drawn the same queue.
    Gizmos::EndFrame(m_clock.RealDeltaSeconds());
}

void Engine::PresentFrame() {
    Renderer::Present();
}

void Engine::Run() {
    while (BeginFrame()) {
        Simulate();
        RenderFrame();
        PresentFrame();
    }
}

} // namespace eng
