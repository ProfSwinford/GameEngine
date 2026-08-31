#pragma once

// ============================================================================
//  ScriptComponent.h - writing your own behaviour for an entity.
//
//  This is the engine's version of a Unity MonoBehaviour. You write a class
//  with the hooks you care about, attach it to an entity in the editor, and it
//  runs:
//
//      class Bouncer final : public eng::ScriptBehaviour {
//          void OnStart() override { ... }
//          void OnUpdate(float dt) override { Transform()->Translate({0, dt * 50}); }
//      };
//      ENGINE_REGISTER_SCRIPT(Bouncer)      // without this it can never be found
//
//  ONE HONEST DIFFERENCE FROM UNITY
//  A script here is COMPILED C++, not an interpreted file. There is no
//  scripting language and no virtual machine. So creating a script in the
//  editor writes the .cpp file and nothing more - it runs after you rebuild.
//
//  What that does NOT cost is the rest of the workflow. A script can be
//  attached to an entity, saved into a scene, and survive a Play/Stop cycle
//  before it has ever been compiled, because the connection is BY NAME.
//
//  THE THREE PIECES
//    ScriptBehaviour   what you write. Hooks named the way Unity names them.
//    ScriptRegistry    a table of name -> "how to make one", filled in by the
//                      ENGINE_REGISTER_SCRIPT macro.
//    ScriptComponent   the engine component. It stores a NAME, and an instance
//                      if that name exists in this build.
//
//  WHY THE COMPONENT STORES A NAME RATHER THAN A TYPE
//  Because the editor has to be able to attach a script that has not been
//  compiled yet. Drop "PlayerController" onto an entity in a build where
//  PlayerController.cpp does not exist and the component attaches, saves, and
//  shows as UNRESOLVED in red in the Inspector. Rebuild, reload, and the same
//  scene file produces a working behaviour with nothing reattached.
//
//  Refusing to attach until the type existed would make it impossible to lay
//  out a level before writing its code - and that is a completely normal order
//  to work in.
//
//  UNRESOLVED IS ALWAYS REPORTED. A script that does nothing because its name
//  is misspelled, and says nothing about it, is an afternoon lost.
// ============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <functional>
#include <memory>
#include <string>

namespace eng {

class ScriptComponent;

// ---------------------------------------------------------------------------
//  The class you inherit from.
//
//  Every hook does nothing by default, so a script that only needs OnUpdate
//  overrides only OnUpdate.
// ---------------------------------------------------------------------------
class ScriptBehaviour {
public:
    virtual ~ScriptBehaviour() = default;

    // Once, on the first simulation step after the script is attached.
    //
    // NOT at attach time. While a scene is loading, components are attached
    // one at a time, so a script looking for its entity's collider at attach
    // time would only find it if the file happened to list the collider first.
    // By the first update, the whole entity exists.
    virtual void OnStart() {}

    // Every fixed simulation step. `deltaSeconds` is always the same value -
    // see GameClock.h for why that matters.
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }

    // The entity is going away. The entity is still safe to touch here; after
    // this returns it is not.
    virtual void OnDestroy() {}

    // Collisions, forwarded from the message bus. `other` is an EntityId
    // rather than a pointer, and may already have been destroyed by the time
    // you look it up - which is exactly why it is an id. See EntityId.h.
    virtual void OnCollisionEnter(EntityId other) { (void)other; }
    virtual void OnCollisionStay(EntityId other)  { (void)other; }
    virtual void OnCollisionExit(EntityId other)  { (void)other; }

    // Handy accessors, all valid from OnStart onwards.
    Entity*      Owner() const;
    EntityId     OwnerId() const;
    Scene*       GetScene() const;
    Transform2D* Transform() const;

private:
    friend class ScriptComponent;
    ScriptComponent* m_component = nullptr;
};

// ---------------------------------------------------------------------------
//  The table of script names. Same idea as ComponentFactory: something has to
//  turn a name in a file into an object.
// ---------------------------------------------------------------------------
class ScriptRegistry {
public:
    using CreateFn = std::unique_ptr<ScriptBehaviour> (*)();

    static void Register(std::string_view scriptName, CreateFn create);
    static bool IsRegistered(std::string_view scriptName);
    static std::unique_ptr<ScriptBehaviour> Create(std::string_view scriptName);
    static void        ForEachScript(const std::function<void(const char*)>& fn);
    static std::size_t Count();
};

// ----------------------------------------------------------------------------
//  ENGINE_REGISTER_SCRIPT(MyScript) - put this at the bottom of your .cpp.
//
//  It creates one small object whose constructor adds your class to the
//  registry. Because that object exists at file scope, its constructor runs
//  automatically when the program starts, before main() - which is how the
//  engine learns your script's name without anybody editing a shared list.
//
//  The ## in Type##_Registrar is the preprocessor's "glue these together"
//  operator, so ENGINE_REGISTER_SCRIPT(Bouncer) produces a class called
//  Bouncer_Registrar. The #Type turns the name into the text "Bouncer".
// ----------------------------------------------------------------------------
#define ENGINE_REGISTER_SCRIPT(Type)                                                   \
    namespace {                                                                        \
    struct Type##_Registrar {                                                          \
        Type##_Registrar() {                                                           \
            ::eng::ScriptRegistry::Register(                                           \
                #Type, []() -> std::unique_ptr<::eng::ScriptBehaviour> {               \
                    return std::make_unique<Type>();                                   \
                });                                                                    \
        }                                                                              \
    };                                                                                 \
    const Type##_Registrar g_##Type##_registrar;                                       \
    }

// ---------------------------------------------------------------------------
//  The component that holds a script.
// ---------------------------------------------------------------------------
class ScriptComponent final : public Component {
public:
    static constexpr const char* kTypeName = "ScriptComponent";

    ~ScriptComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file field:
    //   "script": "PlayerController"
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    const std::string& ScriptName() const { return m_scriptName; }

    // Switches to a different script. Any running behaviour gets its OnDestroy
    // first, so swapping a script in the Inspector tidies up properly rather
    // than dropping the old one on the floor.
    void SetScriptName(std::string_view name);

    // False when the name is not compiled into this build - the "written but
    // not built yet" case. The Inspector shows it in red.
    bool IsResolved() const { return m_behaviour != nullptr; }

    // Called by ScriptSystem only.
    void Tick(float deltaSeconds);
    void DispatchCollision(const std::string& messageType, EntityId other);

private:
    void Bind();
    void Unbind();

    std::string                      m_scriptName;
    std::unique_ptr<ScriptBehaviour> m_behaviour;
    bool                             m_started = false;
};

// ---------------------------------------------------------------------------
//  The system that runs them.
//
//  Stage 200 (Gameplay) - BEFORE movement at 300 and collision at 400, so a
//  script that decides to move something this step has that movement applied
//  and checked in the same step rather than the next one.
// ---------------------------------------------------------------------------
class ScriptSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "ScriptSystem"; }
    int         Order() const override { return SystemStage::kGameplay; }

    static void        Register(ScriptComponent& script);
    static void        Unregister(ScriptComponent& script);
    static void        Clear();
    static std::size_t Count();

    // How many attached scripts could not be found in this build. Shown in the
    // editor, because "nothing happens when I press Play" and "three of my
    // scripts are not compiled in" are the same fact, and only one of them
    // tells you what to do about it.
    static std::size_t UnresolvedCount();

    static void RegisterComponentTypes();

    // Listens for collision messages and passes them to the right behaviours.
    // Done once at start-up rather than once per component.
    static void SubscribeToCollisions();
};

} // namespace eng
