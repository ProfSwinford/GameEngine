#pragma once

// =============================================================================
//  The engine facade. One include for the sandbox and the editor.
//
//  This is the file the Week 10 gate is written against. Everything a game
//  needs - load a scene, read mapped input, move an entity, receive a
//  collision event, draw debug text - is reachable from here or from a header
//  it names, without opening anything under engine/src.
//
//  ---------------------------------------------------------------------------
//  THE FRAME, in the order the Week 10 system order declares:
//
//      BeginFrame()    real time advances; events are pumped; async file
//                      completions fire; the frame allocator is reset
//      Simulate()      for each fixed step: stages 100-700, then message
//                      dispatch, then the deferred queue
//      RenderFrame()   clear, stages 800+, debug draw. Does NOT present.
//      PresentFrame()  present
//
//  Split into four calls rather than one Run() because the editor has to put
//  its panels between RenderFrame and PresentFrame - the IDE must land on top
//  of the game - and a single Run() with a callback for that would be one
//  callback per stage. Run() exists too, for the sandbox, and is those four
//  calls in a loop.
// =============================================================================

#include <engine/concurrency/JobSystem.h>
#include <engine/core/Assert.h>
#include <engine/core/ByteBuffer.h>
#include <engine/core/CVar.h>
#include <engine/core/Config.h>
#include <engine/core/GameClock.h>
#include <engine/core/Log.h>
#include <engine/core/LogBuffer.h>
#include <engine/core/StringId.h>
#include <engine/core/Subsystem.h>
#include <engine/core/Types.h>
#include <engine/debug/Camera.h>
#include <engine/debug/DebugDraw.h>
#include <engine/debug/ScopedTimer.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputMap.h>
#include <engine/math/Overlap.h>
#include <engine/math/Random.h>
#include <engine/math/Transform2D.h>
#include <engine/math/Vec2.h>
#include <engine/memory/MemorySystem.h>
#include <engine/memory/PoolAllocator.h>
#include <engine/memory/StackAllocator.h>
#include <engine/physics/Collider.h>
#include <engine/platform/EventPump.h>
#include <engine/platform/Renderer.h>
#include <engine/platform/Window.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Component.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/SpinComponent.h>
#include <engine/scene/SystemOrder.h>
#include <engine/tools/EditorGui.h>

#include <memory>
#include <string>

namespace eng {

class Engine {
public:
    struct Options {
        std::string configPath = "config/engine.json";
        // The editor sets this; the sandbox does not, and that is what proves
        // the engine ships without its tools attached.
        bool        withEditorGui = false;
        // Overrides the config file's startup scene. Empty means "use the
        // config", and the config's default is itself a virtual path - there
        // is no scene name compiled into the engine or the sandbox.
        std::string sceneOverride;
        // Names a subsystem whose Init() is forced to fail, for the Week 7
        // forced-failure verification.
        std::string forceFailSubsystem;
    };

    static Engine& Get();

    bool Init(const Options& options);
    void Shutdown();

    // --- the frame --------------------------------------------------------
    // Returns false when the engine should stop.
    bool BeginFrame();
    void Simulate();
    void RenderFrame();
    void PresentFrame();

    // Renders the world through an ARBITRARY camera, into whatever render
    // target is currently bound. This is what lets the editor draw the same
    // world twice - once through its free Scene camera and once through the
    // game camera - into two panel-sized textures.
    //
    // `includeDebugDraw` is the difference between the two views: the Scene
    // view wants colliders, the grid and selection outlines; the Game view
    // wants what a player would see.
    void RenderWorld(Camera& camera, bool includeDebugDraw);

    // The sandbox's loop: the four above until BeginFrame returns false.
    void Run();

    void RequestQuit() { m_quitRequested = true; }
    bool QuitRequested() const { return m_quitRequested; }

    // How many fixed simulation steps the most recent BeginFrame asked for.
    // The toolbar shows it; the Week 10 evidence run counts it.
    i32 StepsThisFrame() const { return m_stepsThisFrame; }

    // --- accessors, all public API ----------------------------------------
    Window&          GetWindow();
    const EventPump& Events() const { return m_events; }
    Camera&          GetCamera() { return m_camera; }
    GameClock&       Clock() { return m_clock; }
    Scene&           GetScene() { return *m_scene; }
    const BootConfig& Config() const { return m_config; }

    // Loads a different scene at a safe point. The gate game uses it; the
    // editor's toolbar uses it. Applies the scene's own camera settings.
    bool LoadScene(std::string_view virtualPath, std::string& outError);

    // The mirror of LoadScene: pushes the LIVE camera into the scene, then
    // writes it out. Pass an empty path to save over wherever it was loaded
    // from. The editor's File > Save and Save As both come through here, so
    // the camera handling cannot be forgotten at one of two call sites.
    bool SaveScene(std::string_view virtualPath, std::string& outError);

    // --- play mode --------------------------------------------------------
    //
    // Unity's contract, and the reason it is safe to press Play on a scene you
    // have been editing for an hour: EnterPlayMode snapshots the scene,
    // ExitPlayMode restores it. Anything the running game did - moving the
    // player, destroying pickups, spawning bullets - is undone.
    //
    // The snapshot is the same serialised document Save writes, so it is
    // exercised constantly rather than only when someone saves, and any bug in
    // it shows up immediately instead of the first time a file is written.
    bool EnterPlayMode(std::string& outError);
    void ExitPlayMode();
    bool IsInPlayMode() const { return m_inPlayMode; }

    // For the editor's boot-order table and the Week 7 evidence document.
    const SubsystemStack& Subsystems() const { return m_subsystems; }

    bool IsInitialised() const { return m_initialised; }

private:
    Engine() = default;

    void RegisterBuiltinSubsystems(const Options& options);

    SubsystemStack           m_subsystems;
    BootConfig               m_config;
    ConfigDocument           m_document;
    std::unique_ptr<Window>  m_window;
    std::unique_ptr<Scene>   m_scene;
    std::unique_ptr<CollisionSystem> m_collisionSystem;
    std::unique_ptr<SpinSystem>      m_spinSystem;
    std::unique_ptr<ScriptSystem>    m_scriptSystem;
    EventPump                m_events;
    Camera                   m_camera;
    GameClock                m_clock;

    f64  m_lastFrameTicks  = 0.0;
    i32  m_stepsThisFrame  = 0;
    bool m_initialised     = false;
    bool m_quitRequested   = false;
    bool m_withEditorGui   = false;
    bool m_inPlayMode      = false;
    std::string m_playModeSnapshot;
};

} // namespace eng
