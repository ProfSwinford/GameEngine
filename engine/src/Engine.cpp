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
    ScriptSystem::RegisterComponentTypes();
    UISystem::RegisterComponentTypes();

    // The camera is a component like any other - see CameraComponent.h.
    ComponentFactory::Register(CameraComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<CameraComponent>();
                               });

    engine.m_spinSystem   = std::make_unique<SpinSystem>();
    engine.m_scriptSystem = std::make_unique<ScriptSystem>();
    engine.m_cameraSystem = std::make_unique<CameraSystem>();
    SystemScheduler::Register(engine.m_spinSystem.get());
    SystemScheduler::Register(engine.m_scriptSystem.get());
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
    if (engine.m_cameraSystem != nullptr) {
        SystemScheduler::Unregister(engine.m_cameraSystem.get());
        engine.m_cameraSystem.reset();
    }
    SpinSystem::Clear();
    ScriptSystem::Clear();
    SpriteRenderSystem::Clear();
    TilemapRenderSystem::Clear();
    TileLibrary::Clear();
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
}

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

    // A SESSION STARTS WITH NOTHING HELD. Whatever the keyboard was doing
    // while you were editing is not the game's business, and a key that was
    // somehow left down would have the player moving before the first frame.
    InputMap::ResetState();

    ENGINE_LOG_INFO(Channels::kEditor, "play mode started");
    return true;
}

void Engine::ExitPlayMode() {
    if (!m_inPlayMode) {
        return;
    }
    m_inPlayMode = false;
    m_clock.SetPaused(true);

    // AND IT ENDS WITH NOTHING HELD EITHER. Clearing it here as well as on
    // the way in is not belt and braces: pressing Stop with a key down is
    // precisely how the key gets stuck, and the editor itself should not be
    // left believing something is held while the game is not even running.
    InputMap::ResetState();

    // Anything still queued belongs to the play session and must not be
    // applied to the restored scene - a destroy queued on the last frame of
    // play would otherwise delete an entity in the freshly restored one.
    DeferredOps::Clear();

    // ClearQueue, NOT Clear. Clear drops every SUBSCRIPTION as well, which
    // meant collision handlers worked on the first play session and silently
    // stopped on the second - with nothing reported anywhere. See Messaging.h.
    MessageBus::ClearQueue();

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
            UISystem::UpdateWindowPointer(
                Vec2{m_events.MouseX(), m_events.MouseY()}, Renderer::OutputSize(),
                m_events.MouseLeftDown());
            UISystem::Update(*m_scene, m_camera);
        }

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
    if (!m_clock.IsPaused() || m_stepsThisFrame > 0) {
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
