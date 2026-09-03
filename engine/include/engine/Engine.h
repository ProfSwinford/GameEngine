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
#include <engine/fs/FileSystem.h>
#include <engine/input/InputMap.h>
#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>
#include <engine/math/Random.h>
#include <engine/math/Transform2D.h>
#include <engine/math/Vec2.h>
#include <engine/physics/Collider.h>
#include <engine/platform/EventPump.h>
#include <engine/platform/Window.h>
#include <engine/render/Camera.h>
#include <engine/render/Gizmos.h>
#include <engine/render/Renderer.h>
#include <engine/render/Texture.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Component.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/EntityId.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptLibrary.h>
#include <engine/scene/SpinComponent.h>
#include <engine/scene/SystemOrder.h>
#include <engine/tools/GuiHooks.h>

#include <functional>
#include <memory>
#include <string>

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
    void RenderWorld(Camera& camera, bool includeGizmos);

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

    void RegisterBuiltinSubsystems(const Options& options);

    SubsystemStack          m_subsystems;
    BootConfig              m_config;
    Json                    m_configDocument = Json::object();
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Scene>  m_scene;

    std::unique_ptr<CollisionSystem> m_collisionSystem;
    std::unique_ptr<SpinSystem>      m_spinSystem;
    std::unique_ptr<ScriptSystem>    m_scriptSystem;

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
};

} // namespace eng
