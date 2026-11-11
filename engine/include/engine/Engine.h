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
#include <engine/debug/Debug.h>
#include <engine/fs/AssetArchive.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputMap.h>
#include <engine/input/Pointer.h>
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
#include <engine/scene/EntityRef.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptFields.h>
#include <engine/scene/ScriptLibrary.h>
#include <engine/scene/SpinComponent.h>
#include <engine/scene/SpriteAnimation.h>
#include <engine/scene/SystemOrder.h>
#include <engine/scene/UI.h>
#include <engine/tilemap/AutoTile.h>
#include <engine/tilemap/TileAsset.h>
#include <engine/tilemap/Tilemap.h>
#include <engine/tilemap/TilemapCollider.h>
#include <engine/tools/GuiHooks.h>
#include <engine/tools/ScriptVars.h>

#include <functional>
#include <memory>
#include <string>

// ============================================================================
//  UNITY NAMES FOR THINGS THAT ALREADY HAVE NAMES.
//
//  A small compatibility layer, so that a script written from Unity habits
//  compiles. assets/scripts/TilemapWalker.cpp and PlayerMovement.cpp use it.
//
//  ==========================================================================
//  THE COMPONENT ONES ARE POINTER ALIASES, AND THAT IS DELIBERATE
//
//  `Transform` is `Transform2D*`, not `Transform2D`, because a script never
//  holds a transform by value - it asks for one and gets a pointer.
//
//  ==========================================================================
//  EVERY ENGINE TYPE IS ALREADY SPELLABLE WITHOUT QUALIFICATION, and it is
//  the `using namespace eng;` below that does it rather than any alias here.
//
//  So `Transform2D`, `Vec2`, `Entity`, `EntityId`, `Scene`, `Color` and the
//  other three hundred odd names in `eng` all work bare inside a script. The
//  `eng::` prefixes in assets/scripts are house style, not a requirement.
//  A using-directive is not a snapshot either: it keeps finding names added to
//  the namespace afterwards, which is why even `eng::Engine` - declared below
//  this line - is visible as plain `Engine`.
//
//  ==========================================================================
//  EXACTLY ONE NAME IS AN EXCEPTION, AND IT IS `Transform`.
//
//  Inside a ScriptBehaviour the unqualified name `Transform` finds the
//  inherited member FUNCTION and hides this alias, because a member found by
//  unqualified lookup hides a namespace-scope name of the same spelling even
//  when the member is a function and the hidden name is a type. So:
//
//      Transform t = Transform();        // does NOT compile
//      ::Transform t = Transform();      // compiles - the one at global scope
//      Transform2D* p = Transform();     // compiles, and is the better line
//
//  Two things worth knowing before meeting them:
//
//    `eng::Transform` DOES NOT EXIST. The alias is at global scope and the
//    class is eng::Transform2D, so the usual eng:: escape hatch is not
//    available for this one name - the error is "'Transform': is not a member
//    of 'eng'".
//
//    `class Transform t;` does not rescue it either. Unqualified lookup finds
//    the member function rather than the typedef, so the elaborated type
//    specifier DECLARES A NEW incomplete class in function scope and the error
//    ("uses undefined class") points nowhere near the real problem.
//
//  ==========================================================================
//  WHICH MOSTLY DOES NOT MATTER ANY MORE: WRITE `transform`.
//
//  ScriptBehaviour has a lowercase `transform` member - see the long note in
//  scene/ScriptComponent.h - so a script asks for its transform by name and
//  never writes the type:
//
//      transform->Translate({0.0f, 10.0f});
//
//  Case is what keeps the two apart: `transform` the member, `Transform2D` the
//  type. The `Transform` alias had no job left and is now COMMENTED OUT below,
//  so `::Transform` does not compile either any more.
//
//  Everything above is kept because it is what somebody needs to read when an
//  older script that writes `Transform` stops building. The answer is
//  `Transform2D*`, or better, the inherited `transform`.
//
//  ==========================================================================
//  WHAT IS DELIBERATELY NOT HERE
//
//  No alias for a COMPONENT TYPE that would change which overload is chosen,
//  and nothing that renames a function. These are spellings for types, so a
//  Unity habit compiles; they are not a shim that makes the two engines
//  behave alike, and docs/scripting-reference.md is the honest mapping.
// ============================================================================

typedef eng::ScriptBehaviour MyScriptBehaviour;
typedef eng::Vec2 Vector2;

// Unity's name for an entity. A pointer, like the component aliases below:
// `GameObject self = Owner();`
typedef eng::Entity* GameObject;

// ============================================================================
//  UNITY'S COMPONENT NAMES, AND THEY ARE REFERENCES
//
//  In C# `public SpriteRenderer flash;` is a reference, drawn by the Inspector
//  as a slot you drag onto - so that is what these are here. Each one is an
//  eng::ComponentRef, which travels as the target entity's NAME and resolves
//  on every access; scene/ComponentRef.h has the full argument for why it
//  cannot be a pointer.
//
//  THESE WERE POINTER TYPEDEFS UNTIL NOW, and the four of them were used that
//  way for a same-entity cache: `Rigidbody2D m_body = nullptr;` followed by
//  `m_body = Find<...>()` in OnStart. That spelling no longer compiles. The
//  cache is still right - and still safe, because it dies with the behaviour
//  object that holds it - so write the component type and a star for it:
//
//      eng::Rigidbody2DComponent* m_body = nullptr;   // in a private section
//
//  assets/scripts/TilemapWalker.cpp is the worked example, and was the one
//  file in this project that had to change.
// ============================================================================

typedef eng::ComponentRef<eng::Rigidbody2DComponent> Rigidbody2D;

// ============================================================================
//  AND WHY THIS ONE IS NOT CALLED `Transform`
//
//  Because it cannot be. ScriptBehaviour declares `Transform2D* Transform()`,
//  and inside a script class that inherited MEMBER FUNCTION hides a
//  namespace-scope TYPE of the same spelling - so `Transform anchor;` in the
//  one place a field can be declared does not name this alias at all. MSVC
//  reports it as "'anchor': unknown override specifier", which points
//  nowhere near the cause.
//
//  That is the same hiding the long note above this block describes, and the
//  reason `//using Transform = eng::Transform2D*;` is commented out rather
//  than merely unused. `::Transform` would resolve, but the note explains why
//  sending somebody there is worse than giving the alias a different name.
//
//  TransformRef it is, which is also the house spelling for a reference -
//  EntityRef, PrefabRef, ImageRef, AnimationRef.
// ============================================================================

typedef eng::ComponentRef<eng::TransformComponent> TransformRef;

// THERE WAS A MISSPELLED `Ridigbody2D` BESIDE THIS ONE, and taking it out left
// assets/scripts/TilemapWalker.cpp unable to compile - it was the one file
// that wrote it. It writes Rigidbody2D now.
//
// WORTH KNOWING BEFORE REMOVING THE NEXT ONE: a project's scripts are compiled
// by the EDITOR, not by this build, so deleting a name from this list cannot
// fail the engine build or the test suite. Nothing tells you until somebody
// opens the editor. Grep assets/scripts first.

typedef eng::ComponentRef<eng::SpriteComponent> SpriteRenderer;

// ============================================================================
//  COLLIDER STAYS A POINTER, and it is the engine that decides that, not a
//  preference.
//
//  Entity::Find<T> is `static_cast<T*>(FindComponent(T::kTypeName))` - a
//  lookup BY NAME, with no derived-to-base step. ColliderComponent is the
//  abstract base of BoxCollider/CircleCollider/PolygonCollider/EdgeCollider
//  and has no kTypeName of its own, so there is no name to look up and
//  ComponentRef<eng::ColliderComponent> cannot resolve - it does not even
//  compile. eng::FindCollider(Entity&) exists precisely because this question
//  needs a hand-written answer; see physics/Collider.h.
//
//  So a reference to "whatever collider is on that entity" is not expressible
//  by this mechanism. Name the concrete one you mean:
//
//      eng::ComponentRef<eng::BoxColliderComponent> area;
//
//  This typedef keeps its old meaning - a plain pointer, for a collider on
//  THIS entity - and every use of the word in this project is in a comment
//  about Unity's OnTriggerEnter signature, not a declaration.
// ============================================================================

typedef eng::ColliderComponent* Collider2D;

typedef eng::InputMap Input;
//using GameObject = eng::Entity;
//using GameObject = eng::PrefabRef;

using namespace eng;
namespace eng {

class Engine {
public:
    struct Options {
        // ------------------------------------------------------------------
        //  WHICH PROJECT TO OPEN, as a real folder on this machine.
        //
        //  Empty means "work it out", which is the original behaviour and
        //  what the player and the tests use: look at ENGINE_ASSET_ROOT, then
        //  walk up from the executable until a project turns up.
        //
        //  Set, it is an instruction rather than a hint, and a folder that is
        //  not a project makes Init fail instead of falling back to the
        //  search. See FileSystem::UseRoot for why that asymmetry is
        //  deliberate.
        //
        //  This is the whole of what lets a project live anywhere on disk.
        //  Everything else - config/engine.json, logs/, .build/, .editor/ and
        //  assets/ itself - is already resolved against the root, so moving
        //  the root moves all of them with it.
        // ------------------------------------------------------------------
        std::string projectRoot;

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
    std::unique_ptr<SpriteAnimationSystem> m_animationSystem;

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

    // ======================================================================
    //  THROWS AWAY EVERYTHING THE SCENE THAT HAS JUST GONE LEFT BEHIND.
    //
    //  CALLED AFTER EVERY REPLACEMENT, and it has to be, because the UNLOAD
    //  ITSELF is what queues most of it: taking a scene apart detaches every
    //  collider, and unregistering one that is still touching something
    //  queues two exit events for the pair. Clearing before the load cannot
    //  reach those.
    //
    //  And they do not land harmlessly. Unloading resets the slot list, so
    //  the next scene hands out entity ids from index 0 again - the old
    //  player's id is the new scene's first entity. The symptom was an
    //  OnCollisionExit on the first frame of a level, naming a pair that had
    //  never entered, which is the one thing "every enter has a matching
    //  exit" is supposed to rule out.
    //
    //  THREE PATHS REPLACE A SCENE: LoadScene, the deferred request a script
    //  makes, and ExitPlayMode putting the snapshot back. The clear went into
    //  one of them first, and pressing Stop still leaked the play session's
    //  exits into the restored scene - so it lives here instead, where no
    //  future caller can forget it.
    // ======================================================================
    void ForgetTheOldScene();
};

} // namespace eng
