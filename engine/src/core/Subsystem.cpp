// WEEK 7 - the subsystem stack and the engine's boot sequence.
// See Subsystem.h for the fiasco this exists to avoid, and Engine.h for the
// frame structure.

#include <engine/Engine.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace eng {

// ---------------------------------------------------------------------------
//  SubsystemStack
// ---------------------------------------------------------------------------

void SubsystemStack::Register(std::unique_ptr<Subsystem> subsystem) {
    if (subsystem != nullptr) {
        m_subsystems.push_back(std::move(subsystem));
    }
}

void SubsystemStack::SetForcedFailure(std::string_view subsystemName) {
    m_forcedFailure.assign(subsystemName);
}

bool SubsystemStack::InitAll() {
    m_initialisedCount = 0;

    for (usize i = 0; i < m_subsystems.size(); ++i) {
        Subsystem& subsystem = *m_subsystems[i];

        const bool forced = !m_forcedFailure.empty() && m_forcedFailure == subsystem.Name();
        if (forced) {
            ENGINE_LOG_WARN(Channels::kCore,
                            "FORCED FAILURE: subsystem '{}' Init() is being made to "
                            "return false on purpose", subsystem.Name());
        }

        const bool ok = !forced && subsystem.Init();
        if (ok) {
            ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} up", i + 1,
                            m_subsystems.size(), subsystem.Name());
            ++m_initialisedCount;
            continue;
        }

        ENGINE_LOG_ERROR(Channels::kCore, "subsystem '{}' failed to initialise",
                         subsystem.Name());

        // *** THE GRADED PART. *** Unwind exactly what came up, in exact
        // reverse order. The FAILING subsystem is NOT shut down - it never
        // came up, and calling Shutdown on something that never ran Init is
        // how a teardown crashes in a destructor. Subsystems after it were
        // never touched.
        ENGINE_LOG_INFO(Channels::kCore, "unwinding {} already-initialised subsystem(s)",
                        m_initialisedCount);
        for (usize j = i; j-- > 0;) {
            ENGINE_LOG_INFO(Channels::kCore, "  [{}] {} down (unwind)", j + 1,
                            m_subsystems[j]->Name());
            m_subsystems[j]->Shutdown();
        }
        m_initialisedCount = 0;
        return false;
    }

    return true;
}

void SubsystemStack::ShutdownAll() {
    // EXACT REVERSE of initialisation, and only as far as initialisation got.
    for (usize i = m_initialisedCount; i-- > 0;) {
        ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} down", i + 1, m_subsystems.size(),
                        m_subsystems[i]->Name());
        m_subsystems[i]->Shutdown();
    }
    m_initialisedCount = 0;
}

void SubsystemStack::ForEach(const std::function<void(const Subsystem&, bool)>& fn) const {
    for (usize i = 0; i < m_subsystems.size(); ++i) {
        fn(*m_subsystems[i], i < m_initialisedCount);
    }
}

// ---------------------------------------------------------------------------
//  Engine
// ---------------------------------------------------------------------------

Engine& Engine::Get() {
    static Engine instance;   // constructed on first use; see Subsystem.h
    return instance;
}

Window& Engine::GetWindow() {
    ENGINE_ASSERT_MSG(m_window != nullptr, "Engine::GetWindow before Init");
    return *m_window;
}

void Engine::RegisterBuiltinSubsystems(const Options& options) {
    // =======================================================================
    //  *** THE DECLARED BOOT ORDER. REGISTRATION ORDER IS DEPENDENCY ORDER. ***
    //  Each entry may assume everything above it is up. Teardown is the exact
    //  reverse. The table is repeated in docs/week07-milestone2.md with the
    //  dependency for each.
    // =======================================================================

    // 1. Logging. First up, last down, because everything logs - including
    //    everything's own shutdown.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Logging",
        [this] {
            LogBuffer::SetCapacity(m_config.logBufferCapacity);
            return Log::Init(m_config.logFile, m_config.logThreshold);
        },
        [] { Log::Shutdown(); }));

    // 2. Profiling. Depends on logging (it reports through it) and on nothing
    //    else, so it goes early and its report is the last thing before the
    //    logger closes.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Profiling", [] { return true; },
        [] { TimerRegistry::Report(); }));

    // 3. File system. Depends on logging (it logs its resolved root at Info,
    //    every boot). Everything that reads a file depends on it.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "FileSystem", [] { return FileSystem::Init(); },
        [] { FileSystem::Shutdown(); }));

    // 4. Memory. Depends on logging and on the config having been read for its
    //    sizes. Takes its slabs ONCE, here - see the page-fault note in
    //    StackAllocator.h.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Memory",
        [this] {
            return MemorySystem::Init(m_config.frameAllocatorBytes,
                                      m_config.entityPoolBlocks, 256);
        },
        [] { MemorySystem::Shutdown(); }));

    // 5. Platform. Depends on logging. Owns the window, which everything
    //    visual needs.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Platform",
        [this] {
            m_window = std::make_unique<Window>(m_config.windowTitle.c_str(),
                                                m_config.windowWidth,
                                                m_config.windowHeight);
            return m_window->IsValid();
        },
        [this] { m_window.reset(); }));

    // 6. Renderer. Depends on the platform's window.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Renderer",
        [this] {
            if (!Renderer::Init(*m_window)) {
                return false;
            }
            m_camera.SetViewportSize(Renderer::OutputSize());
            m_camera.SetZoom(CVarRegistry::GetFloat("camera.defaultZoom", 1.0f));
            return true;
        },
        [] { Renderer::Shutdown(); }));

    // 7. Editor GUI. Depends on the window and the renderer. Only registered
    //    when the editor asked for it - the sandbox never has it, which is
    //    what proves the engine ships without its tools.
    if (options.withEditorGui) {
        m_subsystems.Register(std::make_unique<LambdaSubsystem>(
            "EditorGui", [this] { return EditorGui::Init(*m_window); },
            [] { EditorGui::Shutdown(); }));
    }

    // 8. Input. Depends on the platform (events come from it) and on the
    //    editor GUI when present (it asks whether the GUI swallowed a key).
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Input",
        [this] {
            InputMap::SetDeadZone(m_config.inputDeadZone);
            std::string warnings;
            InputMap::LoadBindings(m_document.Root().Child("input"), warnings);
            // The gameplay context is pushed first and stays at the bottom of
            // the stack; a menu pushes on top of it. See InputMap.h.
            InputMap::PushContext(Intern("gameplay"));
            return true;
        },
        [] { InputMap::ClearBindings(); }));

    // 9. Jobs. Depends on logging. Registered BEFORE the resource manager,
    //    because async loading depends on the workers existing - get this
    //    backwards and it works right up until the first async load, which is
    //    exactly the latent ordering bug this whole file exists to prevent.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Jobs",
        [this] { return JobSystem::Init(static_cast<u32>(std::max(0, m_config.workerThreadCount))); },
        [] { JobSystem::Shutdown(); }));

    // 10. Resources. Depends on the file system (reads), the renderer
    //     (creates textures) and the jobs system (async reads).
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Resources", [] { return ResourceManager::Init(); },
        [] { ResourceManager::Shutdown(); }));

    // 11. Debug draw. Depends on the renderer and the camera.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "DebugDraw",
        [this] {
            DebugDraw::SetCircleSegments(
                CVarRegistry::GetInt("debug.circleSegments", m_config.debugCircleSegments));
            return true;
        },
        [] { DebugDraw::Clear(); }));

    // 12. Messaging. Registered BEFORE collision, because the collision system
    //     dispatches through it.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Messaging", [] { return true; }, [] { MessageBus::Clear(); }));

    // 13. Scene. Depends on resources (components acquire textures on attach)
    //     and on messaging.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Scene",
        [this] {
            ComponentFactory::RegisterBuiltins();
            CollisionSystem::RegisterComponentTypes();
            SpinSystem::RegisterComponentTypes();
            ScriptSystem::RegisterComponentTypes();

            m_spinSystem = std::make_unique<SpinSystem>();
            m_scriptSystem = std::make_unique<ScriptSystem>();
            SystemScheduler::Register(m_spinSystem.get());
            SystemScheduler::Register(m_scriptSystem.get());

            m_scene = std::make_unique<Scene>();
            Scene::SetActive(m_scene.get());

            // HOW MANY SCRIPTS LINKED IN. Worth a line at boot because the
            // failure it catches is otherwise invisible: script registrars are
            // file-scope objects nothing references, so a link without
            // WHOLE_ARCHIVE silently discards every one of them and the only
            // symptom is that nothing happens when you press Play. A zero here
            // when scripts/ is not empty names the problem immediately.
            ENGINE_LOG_INFO(Channels::kScene, "{} script(s) registered",
                            ScriptRegistry::Count());
            ScriptRegistry::ForEachScript([](const char* name) {
                ENGINE_LOG_INFO(Channels::kScene, "    script '{}'", name);
            });
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

    // 14. Collision. Depends on messaging and on the scene.
    m_subsystems.Register(std::make_unique<LambdaSubsystem>(
        "Collision",
        [this] {
            m_collisionSystem = std::make_unique<CollisionSystem>();
            SystemScheduler::Register(m_collisionSystem.get());

            // Scripts hear about collisions through the bus, so the
            // subscription belongs AFTER collision exists. One subscription
            // for every script rather than one per component - see the note
            // on SubscribeToCollisions.
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
    m_withEditorGui = options.withEditorGui;

    // ---- BEFORE the subsystem stack ------------------------------------
    // Two things have to happen before the ordered boot: the config file has
    // to be read (the logger's threshold and the window's size come from it),
    // and the file system has to exist in order to read it.
    //
    // FileSystem::Init is therefore called twice - once here, unlogged,
    // and once inside the stack where it logs its resolved root and takes part
    // in the ordered teardown. It is idempotent and the second call is what
    // the boot log shows. The alternative - a config path relative to the
    // working directory - is exactly the thing Week 9's file system layer
    // exists to abolish.
    FileSystem::Init();

    std::string configError;
    if (!LoadBootConfig(options.configPath, m_config, configError)) {
        std::fprintf(stderr, "config error: %s\n", configError.c_str());
        return false;
    }
    m_document.LoadFromVirtualPath(options.configPath, configError);

    // CVars are registered before the config is applied, so that applying it
    // can warn about a name that does not exist.
    CVarRegistry::RegisterBool("debug.drawColliders", true,
                               "Draw every collider through the debug draw system.");
    CVarRegistry::RegisterBool("debug.showAllocatorHud", true,
                               "Draw the allocator HUD as debug text (sandbox only).");
    CVarRegistry::RegisterBool("debug.showGrid", false,
                               "Draw a world grid and the origin axes.");
    CVarRegistry::RegisterFloat("camera.defaultZoom", 1.0f,
                                "Camera zoom applied at startup.");
    CVarRegistry::RegisterInt("debug.circleSegments", m_config.debugCircleSegments,
                              "Line segments used to approximate a debug circle.");
    CVarRegistry::RegisterFloat("time.fixedStep", m_config.fixedTimestepSeconds,
                                "Simulation step size in seconds. 1/60 is the usual "
                                "choice; change it to see the accumulator work.");
    CVarRegistry::RegisterInt("time.maxStepsPerFrame", m_config.maxStepsPerFrame,
                              "Spiral-of-death clamp: the most simulation steps one "
                              "frame may run before surplus time is discarded.");
    CVarRegistry::RegisterBool("physics.playerCollidesWithPickups", true,
                               "Week 10 M4 item 5: clearing this removes Pickup from the "
                               "player's collision mask at runtime, and the collision "
                               "events stop.");

    if (m_document.IsLoaded()) {
        std::string warnings;
        CVarRegistry::ApplyFromConfig(m_document.Root().Child("cvars"), warnings);
    }

    m_subsystems.SetForcedFailure(options.forceFailSubsystem);

    RegisterBuiltinSubsystems(options);

    ENGINE_LOG_INFO(Channels::kCore, "engine booting: {} subsystems, in declared order",
                    m_subsystems.Count());
    if (!m_subsystems.InitAll()) {
        // Everything that came up has been torn down in reverse, nothing after
        // the failure was started, and the caller exits non-zero.
        return false;
    }

    m_clock.Init();
    m_clock.SetFixedStepSeconds(CVarRegistry::GetFloat("time.fixedStep",
                                                       m_config.fixedTimestepSeconds));
    m_clock.SetMaxStepsPerFrame(CVarRegistry::GetInt("time.maxStepsPerFrame",
                                                     m_config.maxStepsPerFrame));

    SystemScheduler::LogOrder();

    const std::string scene =
        options.sceneOverride.empty() ? m_config.startupScene : options.sceneOverride;
    if (!scene.empty()) {
        std::string sceneError;
        if (!LoadScene(scene, sceneError)) {
            // A scene that will not load is an environment failure, not a
            // reason to refuse to boot: the editor is far more useful with an
            // empty scene and a readable error than not running at all.
            ENGINE_LOG_ERROR(Channels::kScene, "startup scene '{}' did not load: {}",
                             scene, sceneError);
        }
    }

    m_lastFrameTicks = static_cast<f64>(SDL_GetPerformanceCounter());
    m_initialised    = true;
    ENGINE_LOG_INFO(Channels::kCore, "engine up");
    return true;
}

void Engine::Shutdown() {
    if (!m_initialised) {
        // Still unwind: a failed Init already did its own unwinding, but a
        // caller that never called Init must not be punished for calling
        // Shutdown.
        m_subsystems.ShutdownAll();
        return;
    }
    ENGINE_LOG_INFO(Channels::kCore, "engine shutting down (reverse of boot order)");
    SystemScheduler::Clear();
    m_subsystems.ShutdownAll();
    m_initialised = false;

    // SDL_Quit exactly once, after every subsystem that touched SDL is down.
    // Doing it inside ~Window - which is where Week 1 put it - meant quitting
    // SDL while the file system still held a base path, and that is the shape
    // of "leaks reported inside SDL with no frame in your code".
    SDL_Quit();
}

bool Engine::LoadScene(std::string_view virtualPath, std::string& outError) {
    if (m_scene == nullptr) {
        outError = "no scene subsystem";
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
        outError = "no scene subsystem";
        return false;
    }

    const std::string target =
        virtualPath.empty() ? m_scene->SourcePath() : std::string(virtualPath);
    if (target.empty()) {
        outError = "this scene has never been loaded from or saved to a file; use Save As";
        return false;
    }

    // The live camera goes in FIRST. Doing it here rather than in Scene::Save
    // means the scene does not need to know a Camera exists, and doing it at
    // all means "frame the shot, then save" behaves the way anyone would
    // expect.
    m_scene->SetCameraState(m_camera.Position(), m_camera.Zoom());

    return m_scene->Save(target, outError);
}

bool Engine::EnterPlayMode(std::string& outError) {
    if (m_inPlayMode || m_scene == nullptr) {
        return m_inPlayMode;
    }
    if (!m_scene->SaveToString(m_playModeSnapshot, outError)) {
        // Refuse rather than play unsafely. Entering play mode without a
        // snapshot means Stop cannot restore, and silently turning a
        // non-destructive action into a destructive one is the worst possible
        // failure for this feature.
        ENGINE_LOG_ERROR(Channels::kEditor,
                         "cannot enter play mode: the scene could not be snapshotted ({})",
                         outError);
        return false;
    }
    m_inPlayMode = true;
    m_clock.SetPaused(false);
    ENGINE_LOG_INFO(Channels::kEditor, "play mode entered ({} byte snapshot)",
                    m_playModeSnapshot.size());
    return true;
}

void Engine::ExitPlayMode() {
    if (!m_inPlayMode) {
        return;
    }
    m_inPlayMode = false;
    m_clock.SetPaused(true);

    // Anything still queued belongs to the play session and must not be applied
    // to the restored scene - a destroy queued on the last frame of play would
    // otherwise delete an entity in the freshly restored authoring scene.
    DeferredOps::Clear();
    MessageBus::Clear();

    if (m_scene != nullptr && !m_playModeSnapshot.empty()) {
        std::string error;
        if (!m_scene->LoadFromString(m_playModeSnapshot, error)) {
            ENGINE_LOG_ERROR(Channels::kEditor,
                             "play mode ended but the scene could not be restored: {}",
                             error);
        } else {
            ENGINE_LOG_INFO(Channels::kEditor, "play mode exited; scene restored");
        }
        m_camera.SetPosition(m_scene->InitialCameraPosition());
        m_camera.SetZoom(m_scene->InitialCameraZoom());
    }
    m_playModeSnapshot.clear();
}

bool Engine::BeginFrame() {
    ENGINE_SCOPED_TIMER("Engine::BeginFrame");

    const f64 now       = static_cast<f64>(SDL_GetPerformanceCounter());
    const f64 frequency = static_cast<f64>(SDL_GetPerformanceFrequency());
    f64       delta     = (now - m_lastFrameTicks) / frequency;
    m_lastFrameTicks    = now;

    // A first frame after a long load can be seconds wide. Passing that
    // straight into the accumulator asks for hundreds of simulation steps and
    // triggers the clamp immediately; capping the REAL delta at a quarter
    // second is the standard guard and it is separate from the clamp.
    delta = std::min(delta, 0.25);

    TimerRegistry::SubmitFrameTime(static_cast<f32>(delta * 1000.0));

    MemorySystem::BeginFrame();          // the sawtooth on the Memory panel
    FileSystem::PumpCompletions();       // async callbacks fire HERE, main thread
    ResourceManager::PumpPendingUploads();

    m_events.Poll();
    InputMap::Update(m_events);

    if (m_events.QuitRequested()) {
        m_quitRequested = true;
    }

    m_camera.SetViewportSize(Renderer::OutputSize());

    // Live CVars. Reading them once per frame rather than caching means the
    // CVar panel's edits take effect immediately with no notification
    // machinery - which is the whole reason the panel is worth having.
    m_clock.SetFixedStepSeconds(CVarRegistry::GetFloat("time.fixedStep",
                                                       m_config.fixedTimestepSeconds));
    m_clock.SetMaxStepsPerFrame(CVarRegistry::GetInt("time.maxStepsPerFrame",
                                                     m_config.maxStepsPerFrame));
    DebugDraw::SetCircleSegments(CVarRegistry::GetInt("debug.circleSegments",
                                                      m_config.debugCircleSegments));

    m_stepsThisFrame = m_clock.BeginFrame(delta);
    return !m_quitRequested;
}

void Engine::Simulate() {
    ENGINE_SCOPED_TIMER("Engine::Simulate");

    for (i32 step = 0; step < m_stepsThisFrame; ++step) {
        const f32 fixedStep = m_clock.FixedStepSeconds();

        // Stages 100-700: input, gameplay, movement, collision, response,
        // deferred, camera. See SystemOrder.h for the declared list.
        SystemScheduler::UpdateRange(0, SystemStage::kCollisionResponse, fixedStep);

        // Stage 500: message dispatch. Handlers run here and nowhere else.
        MessageBus::Dispatch();

        // Stage 600: structural changes, at ONE defined point.
        if (m_scene != nullptr) {
            DeferredOps::Apply(*m_scene);
        }

        // Stage 700: camera, after everything it might follow has moved.
        SystemScheduler::UpdateRange(SystemStage::kDeferred + 1,
                                     SystemStage::kFirstRenderStage, fixedStep);

        m_clock.OnStepConsumed();
    }
}

void Engine::RenderWorld(Camera& camera, bool includeDebugDraw) {
    ENGINE_SCOPED_TIMER("Engine::RenderWorld");

    // The camera's viewport follows whatever is currently bound, so the same
    // call frames correctly whether it is filling the window or a 400x300
    // panel. Renderer::OutputSize reports the bound target's size.
    camera.SetViewportSize(Renderer::OutputSize());

    Renderer::Clear(Color{18, 18, 22, 255});

    SpriteRenderSystem::Render(camera);

    // Render-stage systems (800+), for anything a game wants drawn between
    // the sprites and the debug geometry.
    SystemScheduler::RenderPass(m_clock.RealDeltaSeconds());

    // Debug geometry last, so it lands on top of everything.
    if (includeDebugDraw) {
        DebugDraw::Render(camera);
    }
}

void Engine::RenderFrame() {
    ENGINE_SCOPED_TIMER("Engine::RenderFrame");

    if (CVarRegistry::GetBool("debug.showGrid", false)) {
        DebugDraw::Grid(100.0f, Color{40, 40, 48, 255});
        DebugDraw::OriginAxes(120.0f);
    }

    RenderWorld(m_camera, /*includeDebugDraw=*/true);

    // The sandbox has exactly one view, so it also owns the once-per-frame
    // expiry. The editor calls EndFrame itself, after both of its views have
    // drawn the same queue.
    DebugDraw::EndFrame(m_clock.RealDeltaSeconds());
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
