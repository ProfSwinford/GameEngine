#pragma once

// ============================================================================
//  Engine.h - one include that gives you the whole engine.
//
//  Both the game and the editor include this file and nothing else. Everything
//  a game needs - load a scene, read the controls, move an entity, react to a
//  collision, draw some text - is reachable from here.
//
//  ==========================================================================
//  THE SHAPE OF ONE FRAME, in the order it happens:
//
//    BeginFrame()    real time moves on; input is read; the clock works out
//                    how many simulation steps this frame owes
//    Simulate()      run those steps: systems in order, then messages, then
//                    the create/destroy queue
//    RenderFrame()   clear the screen and draw. Does NOT show it yet.
//    PresentFrame()  show it
//
//  It is four calls rather than one Run() because the editor has to slip its
//  panels in between RenderFrame and PresentFrame - the interface has to land
//  on top of the game. Run() exists too, for the standalone game, and is just
//  those four in a loop.
// ==========================================================================

#include <engine/core/Config.h>
#include <engine/core/GameClock.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/core/LogBuffer.h>
#include <engine/core/Subsystem.h>
#include <engine/core/Time.h>
#include <engine/fs/AssetArchive.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputMap.h>
#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>
#include <engine/math/Random.h>
#include <engine/math/Transform2D.h>
#include <engine/math/Vec2.h>
#include <engine/physics/Collider.h>
#include <engine/physics/Physics2D.h>
#include <engine/physics/Rigidbody.h>
#include <engine/platform/EventPump.h>
#include <engine/platform/Window.h>
#include <engine/render/Camera.h>
#include <engine/render/Gizmos.h>
#include <engine/render/Renderer.h>
#include <engine/render/Texture.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/BuildScenes.h>
#include <engine/scene/CameraComponent.h>
#include <engine/scene/Component.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/EntityId.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptFields.h>
#include <engine/scene/ScriptLibrary.h>
#include <engine/scene/SpinComponent.h>
#include <engine/scene/SystemOrder.h>
#include <engine/scene/UI.h>
#include <engine/tilemap/AutoTile.h>
#include <engine/tilemap/TileAsset.h>
#include <engine/tilemap/Tilemap.h>
#include <engine/tilemap/TilemapCollider.h>
#include <engine/tools/GuiHooks.h>

#include <functional>
#include <memory>
#include <string>

typedef eng::ScriptBehaviour MyScriptBehaviour;

//Internal Component Definitions
typedef eng::Rigidbody2DComponent* Ridigbody2D;

namespace eng {

class Engine {
public:
    struct Options {
        std::string configPath = "config/engine.json";

        // Overrides the scene named in the config file. Empty means "use the
        // config", and the config's own default is a filename too - there is
        // no scene name compiled into the engine anywhere.
        std::string sceneOverride;

        // ------------------------------------------------------------------
        //  Starting and stopping the editor's interface.
        //
        //  The editor fills these two in; the standalone game leaves them
        //  empty, and that difference is what proves the engine ships without
        //  its tools attached.
        //
        //  They are handed to the engine rather than called by the editor
        //  itself so that the interface takes its proper place in the ordered
        //  start-up: it comes up after the window and the renderer exist, and
        //  goes down before they are destroyed. Getting that wrong is a crash
        //  at shutdown, so it is not left to a caller to remember.
        // ------------------------------------------------------------------
        std::function<bool()> guiInit;
        std::function<void()> guiShutdown;
    };

    // There is exactly one engine. Get() returns it.
    static Engine& Get();

    bool Init(const Options& options);
    void Shutdown();

    // ---- the frame --------------------------------------------------------
    bool BeginFrame();        // returns false when it is time to stop
    void Simulate();
    void RenderFrame();
    void PresentFrame();

    // Draws the world through ANY camera, into whatever is currently being
    // drawn into. This is what lets the editor draw the same world twice in
    // one frame - once through its own free-moving Scene camera and once
    // through the game camera - into two different panels.
    //
    // `includeGizmos` is the difference between the two: the Scene view wants
    // the grid, the collider outlines and the selection highlight; the Game
    // view wants only what a player would see.
    //
    // `editorTints` is a SEPARATE question, and it is separate because the
    // standalone player draws gizmos too - they simply have nothing queued in
    // a shipped game. Editor tints mark things that are deliberately invisible
    // at runtime, most of all a tilemap's connector tiles, and showing those
    // to a player would put magenta squares along the seam between two rooms.
    void RenderWorld(Camera& camera, bool includeGizmos, bool editorTints = false);

    // The standalone game's loop: the four calls above, until BeginFrame says
    // to stop.
    void Run();

    void RequestQuit()       { m_quitRequested = true; }
    bool QuitRequested() const { return m_quitRequested; }

    // How many simulation steps the most recent BeginFrame asked for. The
    // toolbar shows it.
    int StepsThisFrame() const { return m_stepsThisFrame; }

    // ---- getting at the pieces -------------------------------------------
    Window&           GetWindow();
    const EventPump&  Events() const { return m_events; }
    Camera&           GetCamera()    { return m_camera; }
    GameClock&        Clock()        { return m_clock; }
    Scene&            GetScene()     { return *m_scene; }
    const BootConfig& Config() const { return m_config; }

    // Loads a different scene at a safe moment, applying the camera settings
    // the scene file asked for.
    bool LoadScene(std::string_view virtualPath, std::string& outError);

    // Loads a scene by its NUMBER in the build list - see
    // scene/BuildScenes.h. This is what lets a script say "the next level"
    // without repeating a file name somebody will rename:
    //
    //     eng::Engine::Get().LoadSceneByIndex(2, error);
    //
    // Fails with a readable message rather than crashing when the index is
    // past the end of the list, because that is a mistake in a script and a
    // script's mistake belongs in the Console.
    //
    // CAUTION: loading destroys every entity in the current scene. Calling
    // this from inside a script's update pulls the ground out from under
    // the system that is part way through calling it - including the script
    // object itself. A SCRIPT SHOULD CALL RequestScene INSTEAD; see below.
    bool LoadSceneByIndex(std::size_t index, std::string& outError);

    // ========================================================================
    //  CHANGING LEVEL FROM A SCRIPT. This is the one to use.
    //
    //      eng::Engine::Get().RequestScene("scenes/you_win.oscene");
    //
    //  The request is written down and carried out at the end of the frame,
    //  once nothing is walking a list of entities or systems any more. It is
    //  the same arrangement DeferredOps uses for destroying an entity, and it
    //  exists for a sharper version of the same reason.
    //
    //  WHY A SCRIPT MUST NOT CALL LoadScene DIRECTLY. Loading unloads the old
    //  scene, which destroys every entity, which detaches every component,
    //  which destroys the behaviour object whose update is RUNNING AT THAT
    //  MOMENT. The call returns into a `this` that no longer exists, and the
    //  next line to touch a member reads freed memory. It will often appear
    //  to work, which is the worst property a bug of this kind can have.
    //
    //  Setting a flag and loading at the top of the next step does not fix
    //  it: the top of the next step is still inside ScriptSystem::Update,
    //  walking a list of script pointers that the load is about to free.
    //
    //  Asking twice in one frame is not an error; the last request wins. A
    //  request is dropped if the engine stops before the frame ends.
    // ========================================================================
    void RequestScene(std::string_view virtualPath);

    // The same thing by build-list number. An index that is not in the list
    // is reported HERE, where the mistake was made, rather than silently
    // doing nothing at the end of the frame.
    void RequestSceneByIndex(std::size_t index);

    // Whether a request is waiting. Mostly for tests and for a script that
    // wants to stop doing things once it has asked to leave.
    bool HasPendingSceneRequest() const { return m_hasSceneRequest; }

    // The opposite: pushes the LIVE camera into the scene, then writes it out.
    // Pass an empty path to save over wherever it came from.
    bool SaveScene(std::string_view virtualPath, std::string& outError);

    // ---- play mode --------------------------------------------------------
    //
    // Unity's arrangement, and the reason it is safe to press Play on a scene
    // you have been editing for an hour: entering play mode takes a snapshot,
    // and leaving it puts the snapshot back. Everything the running game did -
    // moving the player, collecting pickups, spawning things - is undone.
    //
    // The snapshot is the same text Save writes, so that code is exercised
    // constantly rather than only when somebody saves, and any bug in it shows
    // up immediately instead of the first time a file is written.
    bool EnterPlayMode(std::string& outError);
    void ExitPlayMode();
    bool IsInPlayMode() const { return m_inPlayMode; }

    bool IsInitialised() const { return m_initialised; }

private:
    Engine() = default;

    // Writes down the twelve subsystems and the order they start in. It only
    // records them - nothing runs until Init calls SubsystemStack::InitAll.
    void RegisterBuiltinSubsystems(const Options& options);

    // ------------------------------------------------------------------
    //  FIVE SUBSYSTEMS THAT BELONG TO THE ENGINE ITSELF.
    //
    //  Most of the engine's pieces are Subsystem classes in their own right -
    //  Log, FileSystem, Window, ResourceManager, Gizmos, MessageBus and
    //  ScriptLibrary each start and stop themselves, in their own files.
    //
    //  These five cannot, because starting them is not one call on one class:
    //  it is CONNECTING several pieces to each other, and that wiring is the
    //  engine's job. They are written here, inside Engine, so that they can
    //  reach the engine's own members; what each one does is in Engine.cpp.
    // ------------------------------------------------------------------
    class RendererSubsystem : public Subsystem {
    public:
        bool Init(const BootConfig& config) override;
        void Shutdown() override;
    };

    class GuiSubsystem : public Subsystem {
    public:
        // The editor's two functions, taken from Options before start-up.
        void Use(std::function<bool()> init, std::function<void()> shutdown);

        bool Init(const BootConfig& config) override;
        void Shutdown() override;

    private:
        std::function<bool()> m_init;
        std::function<void()> m_shutdown;
    };

    class InputSubsystem : public Subsystem {
    public:
        bool Init(const BootConfig& config) override;
        void Shutdown() override;
    };

    class SceneSubsystem : public Subsystem {
    public:
        bool Init(const BootConfig& config) override;
        void Shutdown() override;
    };

    class CollisionSubsystem : public Subsystem {
    public:
        bool Init(const BootConfig& config) override;
        void Shutdown() override;
    };

    // ------------------------------------------------------------------
    //  THE TWELVE SUBSYSTEM OBJECTS, in the order they start.
    //
    //  Members rather than globals, so they are created and destroyed with the
    //  engine and not in whatever order the linker felt like. Declared BEFORE
    //  m_subsystems on purpose: members are destroyed in reverse order, so the
    //  list of pointers is torn down before the objects it points at.
    // ------------------------------------------------------------------

    Log                m_log;
    FileSystem         m_fileSystem;
    Window             m_window;
    RendererSubsystem  m_renderer;
    GuiSubsystem       m_gui;
    InputSubsystem     m_input;
    ResourceManager    m_resources;
    Gizmos             m_gizmos;
    MessageBus         m_messaging;
    ScriptLibrary      m_scripts;
    SceneSubsystem     m_sceneSubsystem;
    CollisionSubsystem m_collisionSubsystem;

    SubsystemStack          m_subsystems;
    BootConfig              m_config;
    Json                    m_configDocument = Json::object();
    std::unique_ptr<Scene>  m_scene;

    std::unique_ptr<CollisionSystem> m_collisionSystem;
    std::unique_ptr<RigidbodySystem> m_rigidbodySystem;
    std::unique_ptr<SpinSystem>      m_spinSystem;
    std::unique_ptr<ScriptSystem>    m_scriptSystem;

    // Copies the active camera entity into m_camera once a frame. Registered
    // with the scheduler like the others, so its place in the frame is one
    // number rather than a call buried somewhere - see CameraComponent.h.
    std::unique_ptr<CameraSystem>    m_cameraSystem;

    EventPump m_events;
    Camera    m_camera;
    GameClock m_clock;

    double m_lastFrameTicks = 0.0;
    int    m_stepsThisFrame = 0;
    bool   m_initialised    = false;
    bool   m_quitRequested  = false;
    bool   m_inPlayMode     = false;

    // The scene as it was when Play was pressed, so Stop can put it back.
    std::string m_playModeSnapshot;

    // A scene a script has asked for, carried out at the end of the frame.
    // See RequestScene.
    std::string m_requestedScene;
    bool        m_hasSceneRequest = false;

    void ApplyPendingSceneRequest();
};

} // namespace eng
