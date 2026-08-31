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
    return *m_window;
}

void Engine::RegisterBuiltinSubsystems(const Options& options) {
    // =======================================================================
    //  THE START-UP ORDER.
    //
    //  Registration order IS dependency order: each entry may assume
    //  everything above it is already running. Shutting down happens in the
    //  exact reverse.
    // =======================================================================

    // 1. The log. First up and last down, because everything writes to it -
    //    including everything else's own shutdown message.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Log",
        [this] {
            LogBuffer::SetCapacity(static_cast<std::size_t>(m_config.logBufferCapacity));
            return Log::Init(m_config.logFile, m_config.logThreshold);
        },
        [] { Log::Shutdown(); }));

    // 2. The file system. Needs the log (it writes down where it found the
    //    assets). Everything that reads a file needs it.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "FileSystem", [] { return FileSystem::Init(); },
        [] { FileSystem::Shutdown(); }));

    // 3. The window. Needs the log and the settings for its size.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Window",
        [this] {
            m_window = std::make_unique<Window>(m_config.windowTitle.c_str(),
                                                m_config.windowWidth,
                                                m_config.windowHeight);
            return m_window->IsValid();
        },
        [this] { m_window.reset(); }));

    // 4. The renderer. Needs the window it draws into.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Renderer",
        [this] {
            if (!Renderer::Init(*m_window)) {
                return false;
            }
            m_camera.SetViewportSize(Renderer::OutputSize());
            return true;
        },
        [] { Renderer::Shutdown(); }));

    // 5. The editor's interface. Needs the window and the renderer, and is
    //    only registered when the editor supplied the two functions - the
    //    standalone game never does, which is what proves the engine ships
    //    without its tools.
    //
    //    Note that the engine calls these without knowing what they do. The
    //    interface library lives entirely in the editor; all the engine
    //    provides is the correct moment to start and stop it.
    if (options.guiInit) {
        m_subsystems.Register(std::make_unique<LambdaSubsystem>(
            "EditorGui", options.guiInit,
            options.guiShutdown ? options.guiShutdown : [] {}));
    }

    // 6. Input. Needs the window (events come from it) and the editor GUI when
    //    there is one, because it asks whether the GUI claimed a key press.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Input",
        [this] {
            std::string warnings;
            if (m_configDocument.contains("input")) {
                InputMap::LoadBindings(m_configDocument["input"], warnings);
            } else {
                ENGINE_LOG_WARN(Channels::kInput,
                                "the settings file has no \"input\" section, so no "
                                "controls are bound");
            }
            // The gameplay context sits at the bottom of the stack for the
            // whole run; a menu pushes on top of it. See InputMap.h.
            InputMap::PushContext("gameplay");
            return true;
        },
        [] { InputMap::ClearBindings(); }));

    // 7. Textures. Needs the file system (to read them) and the renderer (to
    //    hand them to the graphics card).
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Resources", [] { return ResourceManager::Init(); },
        [] { ResourceManager::Shutdown(); }));

    // 8. Gizmos. Needs the renderer.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Gizmos",
        [this] {
            Gizmos::SetCircleSegments(m_config.gizmoCircleSegments);
            return true;
        },
        [] { Gizmos::Clear(); }));

    // 9. Messaging. Registered BEFORE collision, because the collision system
    //    sends its events through it.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Messaging", [] { return true; }, [] { MessageBus::Clear(); }));

    // 10. The project's compiled scripts.
    //
    //     Registered BEFORE the scene, which means it is torn down AFTER it -
    //     and that order is not optional. Unloading the scene destroys every
    //     entity, which destroys their script objects, and those objects live
    //     in the compiled library. Unload the library first and the scene's
    //     teardown would be calling destructors that no longer exist.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Scripts",
        [] {
            std::string error;
            // A project with no scripts is fine and returns true. Only a
            // library that exists and will not load is a failure.
            return ScriptLibrary::Load(ScriptLibrary::DefaultVirtualPath(), error);
        },
        [] { ScriptLibrary::Unload(); }));

    // 11. The scene. Needs textures (components load them as they attach),
    //     messaging, and the scripts (so a ScriptComponent finds its behaviour
    //     as the scene loads rather than a frame later).
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Scene",
        [this] {
            // Tell the factory which type names mean which classes.
            ComponentFactory::RegisterBuiltins();
            CollisionSystem::RegisterComponentTypes();
            SpinSystem::RegisterComponentTypes();
            ScriptSystem::RegisterComponentTypes();

            m_spinSystem   = std::make_unique<SpinSystem>();
            m_scriptSystem = std::make_unique<ScriptSystem>();
            SystemScheduler::Register(m_spinSystem.get());
            SystemScheduler::Register(m_scriptSystem.get());

            m_scene = std::make_unique<Scene>();
            Scene::SetActive(m_scene.get());
            return true;
        },
        [this] {
            if (m_scene != nullptr) {
                m_scene->Unload();
            }
            if (m_spinSystem != nullptr) {
                SystemScheduler::Unregister(m_spinSystem.get());
                m_spinSystem.reset();
            }
            if (m_scriptSystem != nullptr) {
                SystemScheduler::Unregister(m_scriptSystem.get());
                m_scriptSystem.reset();
            }
            SpinSystem::Clear();
            ScriptSystem::Clear();
            SpriteRenderSystem::Clear();
            Scene::SetActive(nullptr);
            m_scene.reset();
        }));

    // 12. Collision. Needs messaging and the scene.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Collision",
        [this] {
            m_collisionSystem = std::make_unique<CollisionSystem>();
            SystemScheduler::Register(m_collisionSystem.get());

            // Scripts hear about collisions through the message bus, so this
            // subscription belongs after collision exists.
            ScriptSystem::SubscribeToCollisions();
            return true;
        },
        [this] {
            if (m_collisionSystem != nullptr) {
                SystemScheduler::Unregister(m_collisionSystem.get());
            }
            CollisionSystem::Clear();
            m_collisionSystem.reset();
        }));
}

bool Engine::Init(const Options& options) {

    // Two things have to happen BEFORE the ordered start-up above: the
    // settings file has to be read (the log's level and the window's size come
    // from it), and the file system has to exist in order to read it.
    //
    // FileSystem::Init is therefore called twice - once here, quietly, and
    // once inside the ordered list where it writes to the log and takes part
    // in the ordered shutdown. Calling it twice is harmless.
    FileSystem::Init();

    std::string configError;
    if (!LoadBootConfig(options.configPath, m_config, m_configDocument, configError)) {
        // The log is not open yet, so this goes straight to the terminal.
        std::fprintf(stderr, "settings error: %s\n", configError.c_str());
        return false;
    }

    RegisterBuiltinSubsystems(options);

    ENGINE_LOG_INFO(Channels::kCore, "starting {} subsystems in order",
                    m_subsystems.Count());
    if (!m_subsystems.InitAll()) {
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
