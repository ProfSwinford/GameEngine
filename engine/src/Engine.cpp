// ============================================================================
//  Engine.cpp - starting the engine up, running one frame, and shutting down.
//  See Engine.h for the shape of a frame.
//
//  The most important thing in this file is RegisterBuiltinSubsystems, which
//  is where the engine's start-up ORDER is written down.
// ============================================================================

#include <engine/Engine.h>
#include <engine/core/WorldUnit.h>
#include <engine/input/InputActions.h>
#include <engine/render/Presentation.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <limits>

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
    RigidbodySystem::RegisterComponentTypes();
    TilemapRenderSystem::RegisterComponentTypes();
    SpinSystem::RegisterComponentTypes();
    SpriteAnimationSystem::RegisterComponentTypes();
    ScriptSystem::RegisterComponentTypes();
    UISystem::RegisterComponentTypes();
    PlayerInputSystem::RegisterComponentTypes();

    // The camera is a component like any other - see CameraComponent.h.
    ComponentFactory::Register(CameraComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<CameraComponent>();
                               });

    engine.m_spinSystem       = std::make_unique<SpinSystem>();
    engine.m_scriptSystem     = std::make_unique<ScriptSystem>();
    engine.m_animationSystem  = std::make_unique<SpriteAnimationSystem>();
    engine.m_cameraSystem     = std::make_unique<CameraSystem>();
    engine.m_tileObjectSystem = std::make_unique<TileObjectSystem>();

    // FIRST OF THE SIMULATION STAGES, so that a cell painted by a script last
    // step has its tile object before anything this step looks for one.
    SystemScheduler::Register(engine.m_tileObjectSystem.get());

    SystemScheduler::Register(engine.m_spinSystem.get());
    SystemScheduler::Register(engine.m_scriptSystem.get());

    // AFTER THE SCRIPTS, and the order matters because both sit at the
    // gameplay stage: ties keep registration order, so a script calling
    // Play("jump") in its OnFixedUpdate is honoured on that step rather than
    // being a step late.
    SystemScheduler::Register(engine.m_animationSystem.get());
    SystemScheduler::Register(engine.m_cameraSystem.get());

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
    if (engine.m_animationSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_animationSystem.get());
        engine.m_animationSystem.reset();
    }
    if (engine.m_cameraSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_cameraSystem.get());
        engine.m_cameraSystem.reset();
    }
    if (engine.m_tileObjectSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_tileObjectSystem.get());
        engine.m_tileObjectSystem.reset();
    }
    SpinSystem::Clear();
    ScriptSystem::Clear();
    SpriteAnimationSystem::Clear();
    SpriteRenderSystem::Clear();
    TilemapRenderSystem::Clear();
    TileLibrary::Clear();
    PlayerInputSystem::Clear();
    InputActionLibrary::Clear();
    CameraSystem::Clear();
    Scene::SetActive(nullptr);
    engine.m_scene.reset();
}

// Collision. Needs messaging and the scene.
bool Engine::CollisionSubsystem::Init(const BootConfig&) {
    Engine& engine = Engine::Get();

    engine.m_rigidbodySystem = std::make_unique<RigidbodySystem>();
    engine.m_collisionSystem = std::make_unique<CollisionSystem>();

    // Integration at stage 350 BEFORE collision at 400, so the overlap being
    // resolved is the one the body actually moved into this step.
    SystemScheduler::Register(engine.m_rigidbodySystem.get());
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
    if (engine.m_rigidbodySystem != nullptr) {
        SystemScheduler::Unregister(engine.m_rigidbodySystem.get());
    }
    RigidbodySystem::Clear();
    engine.m_rigidbodySystem.reset();

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
    // WHICH project, before the file system goes looking for one. Empty is
    // the normal case and restores the original search - see
    // FileSystem::UseRoot.
    FileSystem::UseRoot(options.projectRoot);

    // AND THE ANSWER IS CHECKED NOW. This used to discard the bool, which
    // mattered once a failed Init stopped leaving a guessed root behind:
    // carrying on without a project means every Resolve returns nothing, and
    // the first thing to go wrong would be the log file failing to open,
    // reported through the log. Failing here names the folder instead.
    if (!m_fileSystem.Init(m_config)) {
        // The log is not open yet, so this goes straight to the terminal -
        // the same reason the config error below does.
        std::fprintf(stderr,
                     "no project was opened%s%s. A project is a folder containing "
                     "'project.oproj' or an 'assets' folder.\n",
                     options.projectRoot.empty() ? "" : ": ",
                     options.projectRoot.empty() ? "" : options.projectRoot.c_str());
        return false;
    }

    // Remembered for ReloadSettings, which re-reads this same file when the
    // editor changes a setting. See Engine::ReloadSettings.
    m_configPath        = options.configPath;
    m_gameOwnsTheWindow = options.gameOwnsTheWindow;

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

    // The world's gravity and how hard the contact solver works, both from
    // config/engine.json so they can be tuned without a rebuild.
    Physics2D::SetGravity(m_config.gravity);
    CollisionSystem::SetSolverIterations(m_config.solverIterations);
    m_clock.SetMaxStepsPerFrame(m_config.maxStepsPerFrame);

    // BEFORE ANY SCENE IS LOADED, and that is the whole reason it is here
    // rather than wherever it is first read. Every collider measures its
    // authored scale against this number, so a scene deserialized against the
    // default and then re-measured against the project's value would be two
    // different scenes depending on when you looked. See core/WorldUnit.h.
    WorldUnit::SetPixels(m_config.worldUnitPixels);

    // ======================================================================
    //  WHAT SHAPE THE GAME IS, and black in whatever is left over.
    //
    //  REPORTED RATHER THAN REFUSED when the text makes no sense. A settings
    //  file is edited by people, and a mistyped aspect should cost you the
    //  aspect - not the ability to start the game - which is the rule the
    //  whole of Config.h states for every other key.
    // ======================================================================
    ApplyPresentationSetting();

    // ======================================================================
    //  THE WINDOW SETTINGS, AND ONLY FOR THE GAME.
    //
    //  These describe the shipped game's window, so the editor must not take
    //  them: a project that ships full screen at a locked size would
    //  otherwise open the EDITOR full screen at a locked size, with no way
    //  back except hand-editing the settings file. See
    //  Engine::Options::gameOwnsTheWindow.
    //
    //  AFTER the renderer exists, because entering full screen changes the
    //  window's size and anything that cached it would be a frame behind.
    if (options.gameOwnsTheWindow) {
        // ------------------------------------------------------------------
        //  THE LOCK FIRST, FULL SCREEN SECOND, and that order is load-bearing.
        //
        //  SDL constrains a window CREATED resizable to what the compositor
        //  thinks will fit on the desktop, and its own advice is to clear the
        //  flag AFTER creation when a specific size is wanted. So clearing it
        //  and then asking for the configured size back is what actually pins
        //  a game to 1280x720; without the second half, a project that locks
        //  its window gets locked at whatever the compositor allowed.
        //
        //  Doing this after entering full screen would ask for a size while
        //  the window has no say over its own, and would make the message
        //  below report the size of the DISPLAY as the size the project
        //  asked for.
        // ------------------------------------------------------------------
        if (!m_config.windowResizable) {
            m_window.SetResizable(false);
            m_window.SetSize(m_config.windowWidth, m_config.windowHeight);

            const int granted = m_window.Width();
            const int grantedHeight = m_window.Height();
            if (granted == m_config.windowWidth &&
                grantedHeight == m_config.windowHeight) {
                ENGINE_LOG_INFO(Channels::kPlatform,
                                "the window is fixed at {}x{} because the project "
                                "asked for it - window.resizable is false",
                                granted, grantedHeight);
            } else {
                // Said out loud rather than swallowed: a game pinned to a
                // size the display cannot give is a game whose look nobody
                // can reproduce, and the alternative is a silent difference
                // between the configured number and the window on screen.
                ENGINE_LOG_WARN(Channels::kPlatform,
                                "the window is fixed, but at {}x{} rather than the "
                                "{}x{} the project asked for - the display would not "
                                "give the whole of it",
                                granted, grantedHeight, m_config.windowWidth,
                                m_config.windowHeight);
            }
        }
        if (m_config.fullscreen) {
            m_window.SetFullscreen(true);
        }
    }

    SystemScheduler::LogOrder();

    // WHICH SCENE THE GAME OPENS WITH, decided in this order:
    //
    //   1. --scene on the command line, because somebody asking for a
    //      specific scene right now outranks anything written down.
    //   2. index 0 of the build list, which is what the Build panel sets and
    //      what a shipped game normally uses.
    //   3. "startup.scene" in config/engine.json, so a project that predates
    //      the Build panel keeps working exactly as it did.
    //
    // See scene/BuildScenes.h. The list is read here rather than by a
    // subsystem because it is not one - it is a few lines of data that only
    // this decision and LoadSceneByIndex care about.
    std::string buildListError;
    BuildScenes::Load(buildListError);

    std::string scene = options.sceneOverride;
    if (scene.empty() && BuildScenes::Count() > 0) {
        scene = BuildScenes::PathAt(0);
    }
    if (scene.empty()) {
        scene = m_config.startupScene;
    }

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
//  Settings, after start-up
// ---------------------------------------------------------------------------

// Shared by Init and ReloadSettings, so that one piece of text decides what
// presentation.mode does to the renderer. Reported rather than refused when
// the text makes no sense: a settings file is edited by people, and a mistyped
// aspect should cost you the aspect, not the ability to start the game.
void Engine::ApplyPresentationSetting() {
    Presentation::Mode mode = Presentation::Mode::Free;
    int                x    = 0;
    int                y    = 0;
    std::string        why;
    if (!Presentation::ParseSpec(m_config.presentation, mode, x, y, why)) {
        ENGINE_LOG_WARN(Channels::kRender,
                        "presentation.mode '{}' was not understood ({}), so the "
                        "game fills the window",
                        m_config.presentation, why);
    } else if (mode == Presentation::Mode::Aspect) {
        Presentation::SetAspect(x, y);
        ENGINE_LOG_INFO(Channels::kRender,
                        "the game is presented at {}:{}, with black in the margins",
                        x, y);
    } else if (mode == Presentation::Mode::Resolution) {
        Presentation::SetResolution(x, y);
        ENGINE_LOG_INFO(Channels::kRender,
                        "the game is rendered at {}x{} and scaled to the window, "
                        "with black in the margins",
                        x, y);
    } else {
        Presentation::SetFree();
    }
}

bool Engine::ReloadSettings(std::string& outError) {
    // ======================================================================
    //  PRESENT AND UNREADABLE IS NOT THE SAME AS ABSENT.
    //
    //  LoadBootConfig treats both as "no settings file; using built-in
    //  defaults" and returns TRUE - which is right at start-up, where there
    //  may genuinely be no file, and wrong here. Every value in `fresh`
    //  would then be a built-in default, the diff would read that as "the
    //  project now asks for the defaults", and a file locked by something
    //  else for a moment would silently reset the world unit, the gravity
    //  and the log threshold of a project that still says otherwise on disk.
    // ======================================================================
    {
        std::string probe;
        std::string probeError;
        if (!FileSystem::ReadTextFile(m_configPath, probe, probeError) &&
            FileSystem::Exists(m_configPath)) {
            outError = m_configPath + " is there but could not be read (" +
                       probeError + "); the engine is still running on the settings " +
                       "it started with";
            ENGINE_LOG_WARN(Channels::kConfig, "{}", outError);
            return false;
        }
    }

    // The file the engine itself read at start-up, read by the same function,
    // so a key added to BootConfig arrives here with no new code.
    BootConfig fresh;
    Json       document = Json::object();
    if (!LoadBootConfig(m_configPath, fresh, document, outError)) {
        // Present and unparseable. The settings the engine is running on are
        // left exactly as they are rather than half-replaced.
        return false;
    }

    const BootConfig& was = m_config;

    // ======================================================================
    //  ONLY WHAT CHANGED, and this is not an optimisation.
    //
    //  LogBuffer::SetCapacity EMPTIES THE CONSOLE. Re-applying every setting
    //  on every edit would wipe the log each time somebody nudged the gravity,
    //  which is the kind of thing that gets blamed on the Console.
    //
    //  WorldUnit::SetPixels is the same shape of problem from the other end:
    //  every collider in the open scene is measured against it, so changing
    //  it is a real event and worth saying out loud, not a silent re-apply.
    // ======================================================================
    int         applied = 0;
    std::string restartNeeded;

    const auto note = [&restartNeeded](const char* key) {
        if (!restartNeeded.empty()) {
            restartNeeded += ", ";
        }
        restartNeeded += key;
    };

    // ---- logging ---------------------------------------------------------
    if (fresh.logThreshold != was.logThreshold) {
        Log::SetThreshold(fresh.logThreshold);
        ++applied;
    }
    if (fresh.logBufferCapacity != was.logBufferCapacity) {
        LogBuffer::SetCapacity(static_cast<std::size_t>(fresh.logBufferCapacity));
        ++applied;
    }
    if (fresh.logFile != was.logFile) {
        // The log file is opened once and held open for the run - Log::Init
        // opens it, Log::Shutdown closes it, and there is no reopen. Said
        // out loud rather than silently ignored.
        note("logging.file");
    }

    // ---- tunables --------------------------------------------------------
    if (fresh.gizmoCircleSegments != was.gizmoCircleSegments) {
        Gizmos::SetCircleSegments(fresh.gizmoCircleSegments);
        ++applied;
    }
    if (fresh.fixedTimestepSeconds != was.fixedTimestepSeconds) {
        m_clock.SetFixedStepSeconds(fresh.fixedTimestepSeconds);
        ++applied;
    }
    if (fresh.maxStepsPerFrame != was.maxStepsPerFrame) {
        m_clock.SetMaxStepsPerFrame(fresh.maxStepsPerFrame);
        ++applied;
    }

    // ---- physics ---------------------------------------------------------
    if (fresh.gravity.x != was.gravity.x || fresh.gravity.y != was.gravity.y) {
        Physics2D::SetGravity(fresh.gravity);
        ++applied;
    }
    if (fresh.solverIterations != was.solverIterations) {
        CollisionSystem::SetSolverIterations(fresh.solverIterations);
        ++applied;
    }

    // ---- units -----------------------------------------------------------
    if (fresh.worldUnitPixels != was.worldUnitPixels) {
        WorldUnit::SetPixels(fresh.worldUnitPixels);
        ++applied;

        // EVERY COLLIDER IN THE OPEN SCENE JUST CHANGED SIZE, because a
        // collider's scale is a fraction of this number. That is the intended
        // effect and it is still worth a line in the log: a scene that was
        // right a moment ago now has hitboxes of a different size, and
        // anything already measured against the old number - a sprite fitted
        // to one unit, say - was measured once and does not re-measure.
        ENGINE_LOG_WARN(Channels::kCore,
                        "one world unit is now {} pixels (was {}); colliders are "
                        "authored as fractions of it, so every collider in the open "
                        "scene has changed size",
                        fresh.worldUnitPixels, was.worldUnitPixels);
    }

    // ---- presentation ----------------------------------------------------
    const bool presentationChanged = fresh.presentation != was.presentation;

    // ---- the window, and ONLY when this host is the game ------------------
    //
    // Size, full screen and resizability describe the SHIPPED window. An
    // editor that went full screen at a locked size because a project asked
    // for it would leave no way back but hand-editing the file, which is the
    // whole reason Options::gameOwnsTheWindow exists.
    // ---- the startup scene -----------------------------------------------
    //
    // Read once, in Init, and only as the last of three fallbacks behind
    // --scene and build-list index 0. So it cannot be applied - and saying
    // nothing would make it the one key in the file whose change is reported
    // nowhere, which is the thing Config.h asks never to happen.
    const bool startupSceneChanged = fresh.startupScene != was.startupScene;

    const bool windowChanged = fresh.windowWidth != was.windowWidth ||
                               fresh.windowHeight != was.windowHeight ||
                               fresh.windowTitle != was.windowTitle ||
                               fresh.fullscreen != was.fullscreen ||
                               fresh.windowResizable != was.windowResizable;

    // The new values are in place BEFORE anything is applied from them, so
    // that ApplyPresentationSetting reads the same m_config every other
    // caller does rather than being handed a second copy.
    m_config = fresh;

    // Kept in step although nothing re-reads it: InputMap loaded the bindings
    // at start-up and is deliberately NOT asked to load them again - see the
    // header. A document that disagreed with the file would still be a lie.
    m_configDocument = std::move(document);

    if (presentationChanged) {
        ApplyPresentationSetting();
        ++applied;
    }

    if (startupSceneChanged) {
        ENGINE_LOG_INFO(Channels::kConfig,
                        "the startup scene is now '{}'; it decides which scene the "
                        "GAME opens with and takes effect when the game is built or "
                        "run - the build list wins over it when there is one",
                        m_config.startupScene);
    }

    if (windowChanged) {
        if (m_gameOwnsTheWindow) {
            note("the window settings");
        } else {
            ENGINE_LOG_INFO(Channels::kConfig,
                            "the window settings changed; they describe the game's "
                            "window and take effect when the game is built or run");
        }
    }

    if (restartNeeded.empty()) {
        ENGINE_LOG_INFO(Channels::kConfig, "settings re-read from '{}': {} applied",
                        m_configPath, applied);
    } else {
        ENGINE_LOG_INFO(Channels::kConfig,
                        "settings re-read from '{}': {} applied; {} needs a restart",
                        m_configPath, applied, restartNeeded);
    }

    outError.clear();
    return true;
}

// ---------------------------------------------------------------------------
//  Scenes
// ---------------------------------------------------------------------------

bool Engine::LoadSceneByIndex(std::size_t index, std::string& outError) {
    if (index >= BuildScenes::Count()) {
        outError = "there is no scene " + std::to_string(index) + " in the build list; " +
                   (BuildScenes::Count() == 0
                        ? "the list is empty, so nothing has been added in the Build panel"
                        : "it holds " + std::to_string(BuildScenes::Count()) +
                              " scene(s), numbered 0 to " +
                              std::to_string(BuildScenes::Count() - 1));
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }
    return LoadScene(BuildScenes::PathAt(index), outError);
}

void Engine::RequestScene(std::string_view virtualPath) {
    if (virtualPath.empty()) {
        ENGINE_LOG_ERROR(Channels::kScene,
                         "a scene was requested with an empty path, which names no "
                         "file - the request has been ignored");
        return;
    }

    // Asking twice in one frame is not an error, but it is worth saying: two
    // scripts each trying to change level means only one of them happens, and
    // finding that out from the Console beats finding it out from behaviour.
    if (m_hasSceneRequest && m_requestedScene != virtualPath) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "'{}' was already requested this frame; '{}' replaces it",
                        m_requestedScene, virtualPath);
    }

    m_requestedScene.assign(virtualPath);
    m_hasSceneRequest = true;
}

void Engine::RequestSceneByIndex(std::size_t index) {
    // CHECKED HERE rather than at the end of the frame. A bad index is a
    // mistake in a script, and a message that arrives while the script that
    // caused it is on the stack is worth far more than one that turns up
    // later with no context.
    if (index >= BuildScenes::Count()) {
        ENGINE_LOG_ERROR(Channels::kScene,
                         "there is no scene {} in the build list; it holds {} scene(s)",
                         index, BuildScenes::Count());
        return;
    }
    RequestScene(BuildScenes::PathAt(index));
}

void Engine::ForgetTheOldScene() {
    DeferredOps::Clear();
    MessageBus::ClearQueue();

    // AND THE DEBUG FACADE'S THROTTLES. Debug::LogOnce remembers which
    // messages it has already printed. One still remembered from the last
    // play session is one the next session never sees, which looks exactly
    // like a script that has stopped running - so the memory goes with the
    // scene it was built up against. See debug/Debug.h.
    Debug::ResetThrottles();
}

void Engine::ApplyPendingSceneRequest() {
    if (!m_hasSceneRequest) {
        return;
    }

    // Taken and CLEARED FIRST. A load that fails must not leave the request
    // in place to be retried on every frame for the rest of the run - that
    // turns one error message into a flood and the game never recovers.
    const std::string path = m_requestedScene;
    m_requestedScene.clear();
    m_hasSceneRequest = false;

    // ANYTHING QUEUED AGAINST THE OLD SCENE MUST NOT SURVIVE IT.
    //
    // A queued destroy names an EntityId, and the scene about to be loaded
    // reuses the same slot indices from scratch. A leftover id would stand a
    // real chance of matching a brand new entity and destroying something
    // nobody asked to destroy, on the first frame of the new level. Messages
    // carry entity ids for the same reason and go for the same reason.
    DeferredOps::Clear();
    MessageBus::ClearQueue();

    std::string error;
    if (!LoadScene(path, error)) {
        ENGINE_LOG_ERROR(Channels::kScene, "could not open the requested scene '{}': {}",
                         path, error);
    }

    // AND AGAIN AFTERWARDS, because the unload itself queues things; see
    // ForgetTheOldScene, which LoadScene also calls. The clear above stays:
    // it is what stops a destroy QUEUED BY THE OLD SCENE from reaching the
    // new one, which is a different thing.
    ForgetTheOldScene();
}

bool Engine::LoadScene(std::string_view virtualPath, std::string& outError) {
    if (m_scene == nullptr) {
        outError = "the scene subsystem is not running";
        return false;
    }
    if (!m_scene->Load(virtualPath, outError)) {
        return false;
    }

    // The unload that just happened queued the old scene's exit events; see
    // ForgetTheOldScene.
    ForgetTheOldScene();

    ApplySceneCamera();
    return true;
}

// ===========================================================================
//  PUTS A SCENE'S FRAMING ON THE CAMERA, whichever of the two ways it was
//  written.
//
//  ONE FUNCTION BECAUSE THERE ARE TWO CALL SITES - loading a scene and
//  leaving play mode - and they were already two copies of the position-and-
//  zoom pair. A third thing to remember at both would eventually be
//  remembered at one.
// ===========================================================================
void Engine::ApplySceneCamera() {
    if (m_scene == nullptr) {
        return;
    }

    m_camera.SetPosition(m_scene->InitialCameraPosition());

    const float height = m_scene->InitialCameraFrameHeight();
    if (height > 0.0f) {
        // SetFrameHeight derives the zoom from the viewport, so the amount of
        // world in shot is the same on every display - see render/Camera.h.
        m_camera.SetFrameHeight(height);
    } else {
        // Every scene written before frameHeight existed takes this branch,
        // and it is the line that was always here.
        m_camera.SetZoom(m_scene->InitialCameraZoom());
    }
}

bool Engine::SaveScene(std::string_view virtualPath, std::string& outError) {
    if (m_scene == nullptr) {
        outError = "the scene subsystem is not running";
        return false;
    }

    // ======================================================================
    //  NOT WHILE THE GAME IS RUNNING. See the header for the whole argument;
    //  the short version is that the scene in memory is a throwaway copy of
    //  the one on disk, so writing it out saves the playtest into the asset
    //  and Stop then hides the evidence by restoring the snapshot.
    //
    //  THE MESSAGE NAMES THE WAY OUT, because every one of the editor's save
    //  routes shows it verbatim in the status bar.
    // ======================================================================
    if (m_inPlayMode) {
        outError = "the game is running, and the scene on screen is a copy that Stop "
                   "is about to replace - press Stop first, then save";
        ENGINE_LOG_WARN(Channels::kEditor, "save refused: {}", outError);
        return false;
    }

    // ======================================================================
    //  AND NOT WHEN THERE IS NO SCENE OPEN.
    //
    //  The source path deliberately survives an unload, so after Close Scene
    //  it still names a real asset with nothing behind it - and writing that
    //  out serialised zero entities over the file. The editor greys its save
    //  routes on the same question; this is the backstop, for the same reason
    //  the play-mode refusal above is here rather than only in the menus.
    // ======================================================================
    if (!m_scene->IsOpen()) {
        outError = "there is no scene open, so there is nothing to save - saving now "
                   "would write an empty scene over the file";
        ENGINE_LOG_WARN(Channels::kEditor, "save refused: {}", outError);
        return false;
    }

    const std::string target =
        virtualPath.empty() ? m_scene->SourcePath() : std::string(virtualPath);
    if (target.empty()) {
        outError = "this scene has never been saved anywhere; use Save Scene As";
        return false;
    }

    // =======================================================================
    //  THE LIVE CAMERA GOES IN FIRST - BUT ONLY WHEN IT IS THE SCENE'S OWN.
    //
    //  Framing a shot in the editor and pressing save should keep the framing,
    //  and that is what this is for. Done here rather than inside Scene::Save
    //  so the scene does not have to know a camera exists.
    //
    //  ---------------------------------------------------------------------
    //  WHY IT IS GATED, AND WHAT IT WAS QUIETLY DESTROYING
    //
    //  A camera can be an ENTITY - a CameraComponent, which is how a camera
    //  that follows the player is built. When one is active, CameraSystem
    //  copies its world position and zoom into m_camera at the end of EVERY
    //  Simulate, playing or not (the editor needs that, or dragging the camera
    //  entity in the Scene view would not move the Game view until Play was
    //  pressed - see Engine::Simulate).
    //
    //  So for such a scene m_camera is a DERIVED value, and writing it into
    //  the scene document meant every save overwrote the hand-authored
    //  "camera" block with wherever that entity happened to be standing. That
    //  block is not dead weight: it is the framing the scene falls back to
    //  when no camera entity is active, which is what happens once the LAST
    //  one is deleted, disabled, or left on an entity switched off in the
    //  hierarchy. (Priority does not bring the block back - it only chooses
    //  between cameras.) Saving
    //  during a playtest made it worse - the entity was wherever the player
    //  had walked to - but the overwrite happened in edit mode too, on every
    //  single save, and nothing on screen showed it.
    //
    //  THIS IS NOW THE SAME RULE THE "Align game camera" BUTTON ALREADY USES:
    //  write the camera ENTITY when there is one, and the engine's camera only
    //  when there is not. See editor/src/panels/ScenePanel.cpp, which branches
    //  on the very same question and says why.
    //
    //  WHICHEVER WAY THIS CAMERA IS FRAMED. A scene framed by height has to
    //  save as a height, or the next load would turn it back into a zoom and
    //  the framing would quietly start following the window again.
    // =======================================================================
    if (CameraSystem::Active() == nullptr) {
        if (m_camera.CurrentFraming() == Camera::Framing::Height) {
            m_scene->SetCameraFrameHeight(m_camera.Position(), m_camera.FrameHeight(),
                                          m_camera.Zoom());
        } else {
            m_scene->SetCameraState(m_camera.Position(), m_camera.Zoom());
        }
    }

    return m_scene->Save(target, outError);
}

// ---------------------------------------------------------------------------
//  Play mode
// ---------------------------------------------------------------------------

bool Engine::EnterPlayMode(std::string& outError) {
    if (m_inPlayMode || m_scene == nullptr) {
        return m_inPlayMode;
    }
    // TakePlaySnapshot, NOT SaveToString: the snapshot has to carry which file
    // this scene is, because a session that changes scene moves it. See
    // Scene::PlaySnapshot.
    if (!m_scene->TakePlaySnapshot(m_playModeSnapshot, outError)) {
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

    // THE SPEED IS WRITTEN DOWN BEFORE ANY SCRIPT CAN TOUCH IT, so Stop can
    // put the Toolbar back where the person left it. See m_playModeTimeScale.
    m_playModeTimeScale = m_clock.TimeScale();

    m_clock.SetPaused(false);

    // A SESSION STARTS WITH NOTHING HELD. Whatever the keyboard was doing
    // while you were editing is not the game's business, and a key that was
    // somehow left down would have the player moving before the first frame.
    InputMap::ResetState();
    InputActions::ReleaseAll();
    PlayerInputSystem::ResetAll();

    ENGINE_LOG_INFO(Channels::kEditor, "play mode started");
    return true;
}

void Engine::ExitPlayMode() {
    if (!m_inPlayMode) {
        return;
    }
    m_inPlayMode = false;
    m_clock.SetPaused(true);

    // ======================================================================
    //  THIS FRAME'S SIMULATION STEPS ARE CANCELLED, and that is not tidying.
    //
    //  BeginFrame asks the clock how many fixed steps to run and REMEMBERS
    //  the answer, at the top of the frame, while the game is still playing.
    //  Pausing the clock here does not unask the question. The automatic stop
    //  before a script rebuild runs BEFORE Simulate in the editor's frame -
    //  see EditorApp::RebuildScriptsIfChanged - so without this line Simulate
    //  would go on to run those steps against the scene this function has
    //  just restored: physics integrating, scripts getting an OnUpdate, every
    //  rigidbody drifting out of the pose it was authored in, with nothing on
    //  screen to say so and no dirty flag to catch it. The next save then
    //  wrote the drift to the file.
    //
    //  The Stop button and Ctrl+P are read after Simulate and so never hit
    //  that window. Zeroing it here covers all three, and the fourth.
    // ======================================================================
    m_stepsThisFrame = 0;

    // AND THE SPEED THE TOOLBAR WAS SET TO. A pause menu script is documented
    // to do this with SetTimeScale(0), and nothing put it back - so one such
    // playtest froze every later session in the same editor. See
    // m_playModeTimeScale.
    m_clock.SetTimeScale(m_playModeTimeScale);

    // AND IT ENDS WITH NOTHING HELD EITHER. Clearing it here as well as on
    // the way in is not belt and braces: pressing Stop with a key down is
    // precisely how the key gets stuck, and the editor itself should not be
    // left believing something is held while the game is not even running.
    InputMap::ResetState();
    InputActions::ReleaseAll();
    PlayerInputSystem::ResetAll();

    // Anything still queued belongs to the play session and must not be
    // applied to the restored scene - a destroy queued on the last frame of
    // play would otherwise delete an entity in the freshly restored one.
    DeferredOps::Clear();

    // ClearQueue, NOT Clear. Clear drops every SUBSCRIPTION as well, which
    // meant collision handlers worked on the first play session and silently
    // stopped on the second - with nothing reported anywhere. See Messaging.h.
    MessageBus::ClearQueue();

    if (m_scene != nullptr && !m_playModeSnapshot.Empty()) {
        std::string error;
        if (!m_scene->RestorePlaySnapshot(m_playModeSnapshot, error)) {
            // SAID AT ERROR LEVEL AND SAID COMPLETELY, because the scene now
            // has no file - RestorePlaySnapshot clears it rather than leave a
            // real asset one Ctrl+S from being overwritten with wreckage - and
            // somebody has to be told why Save is suddenly asking for a name.
            //
            // TWO WORDINGS, because a scene that had never been saved has no
            // file to name: the one message read "rather than overwrite ''"
            // and claimed to have saved a file that never existed. This is
            // the one branch whose whole job is explaining itself, so it is
            // the one that has to get that right.
            if (m_playModeSnapshot.sourcePath.empty()) {
                ENGINE_LOG_ERROR(Channels::kEditor,
                                 "play mode ended but the scene could not be restored: "
                                 "{}; this scene had never been saved anywhere, so Save "
                                 "will ask for a name", error);
            } else {
                ENGINE_LOG_ERROR(Channels::kEditor,
                                 "play mode ended but the scene could not be restored: "
                                 "{}; the scene has been left with no file, so Save will "
                                 "ask for a name rather than overwrite '{}'",
                                 error, m_playModeSnapshot.sourcePath);
            }
        } else {
            ENGINE_LOG_INFO(Channels::kEditor, "play mode stopped; scene restored");
        }

        // AND AGAIN, AFTER THE RESTORE. Putting the snapshot back unloads the
        // played scene, and that unload queues an exit event for every pair
        // still touching when Stop was pressed - after the clear above, so
        // they used to survive into the restored scene and arrive on the
        // first frame of the NEXT play session. See ForgetTheOldScene.
        ForgetTheOldScene();

        ApplySceneCamera();
    }
    m_playModeSnapshot = {};

    // ======================================================================
    //  ANY SCENE A SCRIPT ASKED FOR AND DID NOT GET IS DROPPED HERE.
    //
    //  LAST, AFTER THE RESTORE, and the order is the whole fix. Putting the
    //  snapshot back unloads the played scene, and an unload runs every
    //  script's OnDestroy - which is a perfectly ordinary place to call
    //  RequestScene from ("when this dies, go to the next level"). That
    //  request arrives after this frame's Simulate has already drained the
    //  field, so it was still standing when the editor went back to editing,
    //  and the next Simulate - in EDIT mode, with nobody playing - carried it
    //  out: the requested scene loaded straight over the scene that had just
    //  been restored, without the unsaved-work question the editor's own
    //  open-scene routes all ask, and re-pointed the save target again.
    //
    //  ApplyPendingSceneRequest is deliberately NOT gated on play mode
    //  instead. The engine's job is to carry out a request from a script, and
    //  the shipped player is never in play mode at all - the thing that is
    //  wrong is a request outliving the session that made it, which is this.
    // ======================================================================
    m_requestedScene.clear();
    m_hasSceneRequest = false;
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

    // THE PHYSICAL SET, AND IT IS NOT GATED ON ANYTHING - deliberately, and
    // beside InputMap's own update because the reason is identical. Poll
    // clears its list every frame, so an event nobody reads is gone for
    // good: gate this and letting go of a key while the game is paused
    // leaves that key held for the rest of the session. Deciding what the
    // presses MEAN is a separate call that is gated; see Simulate, and the
    // long note in input/InputActions.h.
    //
    // It takes the pump rather than the events so that whatever the editor's
    // GUI claimed is skipped here exactly as InputMap skips it.
    InputActions::Sample(m_events);

    if (m_events.QuitRequested()) {
        m_quitRequested = true;
    }

    m_camera.SetViewportSize(Renderer::OutputSize());

    // "THIS FRAME" IN THE NAME, so the counter has to start again here.
    //
    // Nothing used to call this, so Physics2D::QueriesThisFrame reported every
    // query since the process started - a number that only ever went up, and
    // read as a frame cost in the editor's readout.
    //
    // HERE RATHER THAN IN Simulate, because queries are not confined to the
    // simulation: a script's OnUpdate may fire one, and so may editor code
    // drawing a panel. The start of a frame is the one point before all of
    // them.
    Physics2D::ResetQueryCounter();

    m_stepsThisFrame = m_clock.BeginFrame(delta);
    return !m_quitRequested;
}

void Engine::Simulate() {
    // INPUT EVENTS BEFORE THE STEPS, and once per frame rather than once per
    // step. A frame may ask for four steps or none, so dispatching inside the
    // loop below would deliver one press four times on a slow frame and not at
    // all on a fast one. Before the loop rather than after it, so a script
    // polling Value() from OnFixedUpdate reads THIS frame's dispatch.
    //
    // GATED LIKE THE SCRIPTS, because an input event IS script code running:
    // in edit mode the clock is paused and no steps are asked for, so a key
    // pressed over the Scene view must not reach a game that is not playing.
    // Note IsPaused and not m_inPlayMode - a pause menu at a time scale of
    // zero has no steps and is not paused, and it has to keep hearing input
    // or nothing can ever un-pause it.
    //
    // The SAMPLING half is ungated and lives in BeginFrame; input/InputActions.h
    // explains at length why those two cannot be one call.
    if (!m_clock.IsPaused() || m_stepsThisFrame > 0) {
        PlayerInputSystem::FrameDispatch();
    }

    // Everything inside this bracket is a fixed simulation step, which is
    // what makes Time::DeltaTime() return the fixed step rather than the
    // frame time while a script's OnFixedUpdate is running. See core/Time.h.
    Time::BeginFixedStep();

    for (int step = 0; step < m_stepsThisFrame; ++step) {
        const float fixedStep = m_clock.FixedStepSeconds();

        // THE INTERFACE FIRST, before gameplay. A script asking
        // button->WasClicked() in its OnFixedUpdate has to be told about a click
        // that happened before this step, not after it - otherwise every
        // button would respond one step late. See scene/UI.h.
        if (m_scene != nullptr) {
            // ==============================================================
            //  THE MOUSE IS IN WINDOW PIXELS AND THE GAME MAY NOT BE.
            //
            //  With a pinned aspect the game sits in a centred rectangle with
            //  black either side, so a window position has to have the bar
            //  offset taken off it and then be scaled into the render - the
            //  same two steps the editor's Game view does for the same
            //  reason. Without this every UI hit and every world-space
            //  pointer query is wrong by the width of one bar, silently: the
            //  buttons simply stop lining up with where they are drawn.
            //
            //  Free costs nothing: the plan is the whole window, the offset
            //  is zero and the scale is one.
            // ==============================================================
            const Vec2 output = Renderer::OutputSize();
            const Presentation::Plan plan = Presentation::PlanFor(
                static_cast<int>(output.x), static_cast<int>(output.y));

            const float intoRenderX =
                plan.width > 0.0f ? static_cast<float>(plan.renderWidth) / plan.width
                                  : 1.0f;
            const float intoRenderY =
                plan.height > 0.0f ? static_cast<float>(plan.renderHeight) / plan.height
                                   : 1.0f;

            UISystem::UpdateWindowPointer(
                Vec2{(m_events.MouseX() - plan.x) * intoRenderX,
                     (m_events.MouseY() - plan.y) * intoRenderY},
                Vec2{static_cast<float>(plan.renderWidth),
                     static_cast<float>(plan.renderHeight)},
                m_events.MouseLeftDown());
            UISystem::Update(*m_scene, m_camera);
        }

        // Stages 100 to 500: gameplay, movement, collision. See SystemOrder.h.
        SystemScheduler::UpdateRange(0, SystemStage::kCollisionResponse, fixedStep);

        // Stage 500: deliver messages. Every handler runs here and nowhere else.
        MessageBus::Dispatch();

        // Stage 600: create and destroy entities, at one defined point.
        if (m_scene != nullptr) {
            // The fuses first, so anything whose delay ran out this step is
            // in the queue before it is drained and dies on this step rather
            // than next. Inside the step loop, not outside it, because a fuse
            // is measured in GAME time: a paused clock asks for no steps, so a
            // fuse stops burning - which is what DeferredOps.h promises.
            DeferredOps::TickFuses(fixedStep);
            DeferredOps::Apply(*m_scene);
        }

        // Stage 700: the camera, after everything it might follow has moved.
        SystemScheduler::UpdateRange(SystemStage::kDeferred + 1,
                                     SystemStage::kFirstRenderStage, fixedStep);

        m_clock.OnStepConsumed();
    }

    Time::EndFixedStep();

    // ======================================================================
    //  THE PER-FRAME UPDATE - every script's OnUpdate, exactly once.
    //
    //  ONCE PER FRAME, FROM HERE, and not as a render-stage system. Stages
    //  800 and up already run per drawn frame, but they run from
    //  Engine::RenderWorld - which the EDITOR CALLS TWICE, once for the Game
    //  view and once for the Scene view. A script run from there would update
    //  twice per frame in the editor and once in the player. See the note
    //  above ScriptSystem in scene/ScriptComponent.h.
    //
    //  ----------------------------------------------------------------------
    //  THE GATE, which is Unity's and not an approximation of it:
    //
    //    editor, not playing   the clock is PAUSED, so this does not run.
    //                          An entity sitting in a scene you are editing
    //                          is not a running game, and a script quietly
    //                          ticking while you drag things around would be
    //                          a genuinely nasty surprise.
    //
    //    Step button           the clock is paused but asked for one step, so
    //                          m_stepsThisFrame is 1 and this DOES run. One
    //                          press is one whole frame, update included.
    //
    //    time scale of zero    the clock is NOT paused, so this runs - while
    //                          the loop above ran no steps at all, because no
    //                          simulated time was collected.
    //
    //  That last line is the entire reason the two updates are separate
    //  things. A pause menu built on a time scale of zero still gets its
    //  OnUpdate, so it can animate, read input and let go of the pause, while
    //  every OnFixedUpdate in the game is frozen solid. Such a script asks
    //  Time::UnscaledDeltaTime(), because DeltaTime() is scaled and is
    //  therefore 0 for exactly as long as the game is stopped.
    // ======================================================================
    // DECIDED HERE AND REMEMBERED, because PresentFrame needs the very same
    // answer for the end-of-frame coroutine pass and the pause can be toggled
    // in between. See m_scriptsUpdatedThisFrame.
    m_scriptsUpdatedThisFrame = !m_clock.IsPaused() || m_stepsThisFrame > 0;
    if (m_scriptsUpdatedThisFrame) {
        ScriptSystem::FrameUpdate();
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

    // CHANGING LEVEL, at the only moment it is safe to.
    //
    // Right here: after the step loop, so no system is mid-update; after the
    // deferred queue, so nothing is still owed against the scene being
    // replaced. A script asked for this during an update, where carrying
    // it out would have destroyed the script while it was running. See
    // Engine::RequestScene.
    //
    // Before the camera below, so the new scene's camera takes effect on the
    // very frame it loads rather than one frame late.
    ApplyPendingSceneRequest();

    // AND THE CAMERA, for exactly the same reason as the queue above.
    //
    // CameraSystem runs at stage 700 inside the step loop, which is right
    // while the game is playing. But a PAUSED clock runs no steps - which is
    // what the editor sits in while you are editing - so without this line,
    // dragging the camera entity around in the Scene view would not move the
    // Game view until Play was pressed.
    //
    // Applying it twice in a playing frame costs a position copy and is
    // harmless. See CameraComponent.h.
    CameraSystem::Apply();
}

void Engine::RenderWorld(Camera& camera, bool includeGizmos, bool editorTints) {
    // The camera sizes itself from whatever is currently being drawn into, so
    // this same call frames the world correctly whether it is filling the
    // whole window or a small panel in the editor.
    camera.SetViewportSize(Renderer::OutputSize());

    Renderer::Clear(Color{18, 18, 22, 255});

    // ---- TILEMAPS AND SPRITES SHARE ONE LAYER ORDERING --------------------
    //
    // Drawing all tilemaps and then all sprites would be simpler and would be
    // wrong: a foreground tilemap - a bridge the player walks under, the tops
    // of trees, a wall in front of the room - has to come out ON TOP of
    // sprites, while the ground it is painted over has to stay behind them.
    // One ordering is the only way both can be true.
    //
    // So the layers that tilemaps occupy are walked in order, and the sprites
    // in the gaps between them are drawn in between. With no tilemaps in the
    // scene this is a single full-range call, which is exactly what it used to
    // be.
    //
    // Within a layer, tilemaps go first: a tilemap is scenery, and a sprite on
    // the same layer is standing on it.
    static std::vector<int> tilemapLayers;
    TilemapRenderSystem::ResetCounters();
    TilemapRenderSystem::CollectLayers(tilemapLayers);

    int spritesFrom = std::numeric_limits<int>::min();
    for (const int layer : tilemapLayers) {
        SpriteRenderSystem::RenderRange(camera, spritesFrom, layer);
        TilemapRenderSystem::RenderRange(camera, layer, layer + 1, editorTints);
        spritesFrom = layer;
    }
    SpriteRenderSystem::RenderRange(camera, spritesFrom,
                                    std::numeric_limits<int>::max());

    // Stages 800 and above, for anything a game wants drawn between the
    // sprites and the gizmos.
    SystemScheduler::RenderPass(m_clock.RealDeltaSeconds());

    // ---- THE INTERFACE, and note where it is: OUTSIDE the gizmo switch ----
    //
    // This is the whole reason the UI is a system of its own rather than a
    // few Gizmos::Text calls. Gizmos are a DEBUG overlay - the grid, the
    // origin arrows, the collider outlines - and the Game view asks for the
    // world without them, because none of that ships with a game. A score
    // drawn as a gizmo went with them, and the interface disappeared from
    // the one view that shows what a player sees.
    //
    // A UI is part of the game, so it draws whether gizmos are on or not.
    // See scene/UI.h.
    if (m_scene != nullptr) {
        UISystem::Render(*m_scene, camera, Renderer::OutputSize());
    }

    // Gizmos last, so they land on top of everything else - including the
    // interface, which is right for a debug overlay.
    if (includeGizmos) {
        Gizmos::Render(camera);
    }
}

void Engine::RenderFrame() {
    // ======================================================================
    //  FREE IS THE PATH THIS ALWAYS TOOK, and it is kept exactly as it was.
    //
    //  A project that never asks for a shape renders straight onto the window
    //  with no off-screen target, no extra copy and nothing to clear - so the
    //  feature costs those projects nothing, which is the only honest way to
    //  add a presentation step to an engine that did not have one.
    // ======================================================================
    if (Presentation::IsFree()) {
        RenderWorld(m_camera, /*includeGizmos=*/true);
        Gizmos::EndFrame(m_clock.RealDeltaSeconds());
        return;
    }

    const Vec2 output = Renderer::OutputSize();
    const Presentation::Plan plan = Presentation::PlanFor(
        static_cast<int>(output.x), static_cast<int>(output.y));

    // Resized rather than recreated when the window changes, which is what
    // RenderTarget::Resize already does - it keeps the texture when the size
    // has not moved, so this is free on the overwhelming majority of frames.
    if (!m_presentTarget.Resize(plan.renderWidth, plan.renderHeight)) {
        // COULD NOT MAKE ONE, so the game is drawn the old way rather than
        // not at all. A letterbox that fails should cost the black bars, not
        // the picture.
        ENGINE_LOG_ERROR(Channels::kRender,
                         "could not create the {}x{} presentation target, so the "
                         "game is drawn without one",
                         plan.renderWidth, plan.renderHeight);
        RenderWorld(m_camera, /*includeGizmos=*/true);
        Gizmos::EndFrame(m_clock.RealDeltaSeconds());
        return;
    }

    // ---- the game, at its own shape, off-screen --------------------------
    //
    // RenderWorld takes the camera's viewport from Renderer::OutputSize(),
    // which follows the bound target - so pinning the target's size is what
    // pins how much world the camera sees. That is the whole mechanism; see
    // render/Presentation.h.
    Renderer::SetRenderTarget(&m_presentTarget);
    RenderWorld(m_camera, /*includeGizmos=*/true);
    Renderer::SetRenderTarget(nullptr);

    // ---- and onto the window, with black in the margins ------------------
    //
    // CLEARED FIRST, and only when there is something to clear. Black rather
    // than the engine's usual dark grey: the margins are not part of the
    // game, and a player should not be able to tell them from the edge of a
    // dark level by their colour.
    if (plan.letterboxed) {
        Renderer::Clear(Color{0, 0, 0, 255});
    }
    Renderer::DrawRenderTarget(m_presentTarget, Vec2{plan.x, plan.y},
                               Vec2{plan.x + plan.width, plan.y + plan.height});

    // The standalone game has exactly one view, so it also ages the gizmo
    // queue here. The editor does this itself, after BOTH of its views have
    // drawn the same queue.
    Gizmos::EndFrame(m_clock.RealDeltaSeconds());
}

void Engine::PresentFrame() {
    // ======================================================================
    //  THE END OF THE FRAME - the last thing before the picture is shown.
    //
    //  HERE, AND NOT AT THE CALL SITES. Every host presents through this one
    //  function: Engine::Run, the standalone player's loop, and the editor once
    //  its interface has drawn. So `co_yield WaitForEndOfFrame()` resumes at the
    //  same instant in all of them, and a host written later cannot forget to
    //  call it - which three call sites could.
    //
    //  AND NOT A RENDER STAGE, for the reason the per-frame update is not one:
    //  the editor draws the world TWICE, once for the Game view and once for
    //  the Scene view, so a stage would mean two moments in the editor and one
    //  in the player. This is a point in the frame, not a point in a draw.
    //
    //  Behind Simulate's remembered answer rather than asking the clock again -
    //  see m_scriptsUpdatedThisFrame for why asking twice is a bug and not a
    //  tidiness matter. Cleared after use, so presenting twice in one frame
    //  would resume nothing a second time.
    // ======================================================================
    if (m_scriptsUpdatedThisFrame) {
        ScriptSystem::EndOfFrameUpdate();
        m_scriptsUpdatedThisFrame = false;
    }

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
