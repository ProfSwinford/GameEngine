#pragma once

// =============================================================================
//  SCRIPTS - the behaviour layer, and the seam a scripting language plugs into.
//
//  Unity's model, with the one honest difference this engine has to state up
//  front: a script here is COMPILED C++, not an interpreted file. There is no
//  VM, and pretending otherwise by "hot-loading" a .cpp would be a lie the
//  first time someone pressed Play.
//
//  What that costs, said plainly: creating a script in the editor writes the
//  file and reconfigures nothing. It runs after a rebuild. What it does NOT
//  cost is the rest of the workflow - a script can be attached to an entity,
//  saved into a scene, and survive a Play/Stop cycle before it has ever been
//  compiled, because the binding is BY NAME.
//
//  ---------------------------------------------------------------------------
//  THE THREE PIECES.
//
//    ScriptBehaviour   what you write. Virtual hooks, Unity's names.
//    ScriptRegistry    name -> factory. Populated by REGISTER_SCRIPT.
//    ScriptComponent   the engine component. Stores a NAME and, if that name
//                      is registered in this build, an instance.
//
//  ---------------------------------------------------------------------------
//  WHY THE COMPONENT STORES A NAME RATHER THAN A TYPE.
//
//  Because the editor has to attach a script it cannot link against. Drop
//  `PlayerController` onto an entity in a build where PlayerController.cpp has
//  not been compiled yet, and the component attaches, serialises, and reports
//  itself UNRESOLVED in the Inspector. Recompile, reload the scene, and the
//  same file now produces a live behaviour with no edit to the scene.
//
//  A component that refused to attach until the type existed would make the
//  editor useless for authoring anything not already built - and "author the
//  scene, then write the code" is a completely normal order to work in.
//
//  This is the same argument Component.h makes for identity being a runtime
//  STRING rather than a C++ type, followed one step further.
//
//  ---------------------------------------------------------------------------
//  UNRESOLVED IS REPORTED, NEVER SILENT. A script that does nothing because
//  its name is misspelled, and says nothing about it, is an afternoon lost.
//  Every unresolved binding is logged once at attach and shown in red in the
//  Inspector.
// =============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <functional>
#include <memory>
#include <string>

namespace eng {

class ScriptComponent;

// ---------------------------------------------------------------------------
//  What you inherit from when you write a script.
//
//  Every hook is optional and does nothing by default, so a script that only
//  needs OnUpdate overrides only OnUpdate. The template the editor generates
//  has all of them, commented, and says so.
// ---------------------------------------------------------------------------
class ScriptBehaviour {
public:
    virtual ~ScriptBehaviour() = default;

    // Once, on the first simulation step after the script is attached and its
    // entity is live. NOT at attach time: at attach time the rest of the
    // entity may not exist yet - a scene load attaches components one at a
    // time, so a script that looked for its sibling collider in OnAttach would
    // find it only if the file happened to list the collider first.
    virtual void OnStart() {}

    // Every FIXED simulation step, with the fixed delta. Not a render frame:
    // this engine simulates on a fixed timestep and renders separately, so
    // this is called a whole number of times per frame and sometimes zero.
    virtual void OnUpdate(f32 deltaSeconds) { (void)deltaSeconds; }

    // The entity is going away, either destroyed or unloaded with the scene.
    // Still safe to touch the entity here; after this returns it is not.
    virtual void OnDestroy() {}

    // Collision, forwarded from the Week 10 message bus. `other` is a HANDLE,
    // not a pointer, and may already be dead by the time you resolve it -
    // which is exactly why it is a handle.
    virtual void OnCollisionEnter(EntityHandle other) { (void)other; }
    virtual void OnCollisionStay(EntityHandle other)  { (void)other; }
    virtual void OnCollisionExit(EntityHandle other)  { (void)other; }

    // Set by ScriptComponent immediately after construction, before any hook
    // runs, so every accessor below is valid inside OnStart.
    Entity*      Owner() const;
    EntityHandle OwnerHandle() const;
    Scene*       GetScene() const;
    Transform2D* Transform() const;

private:
    friend class ScriptComponent;
    ScriptComponent* m_component = nullptr;
};

// ---------------------------------------------------------------------------
//  name -> factory. The same shape as ComponentFactory, and for the same
//  reason: something has to turn a string from a file into an object.
// ---------------------------------------------------------------------------
class ScriptRegistry {
public:
    using CreateFn = std::unique_ptr<ScriptBehaviour> (*)();

    static void Register(std::string_view scriptName, CreateFn create);
    static bool IsRegistered(std::string_view scriptName);
    static std::unique_ptr<ScriptBehaviour> Create(std::string_view scriptName);
    static void ForEachScript(const std::function<void(const char*)>& fn);
    static usize Count();
};

// The macro a generated script ends with. A file-scope object whose
// constructor registers, which is the one case where a static initialiser is
// the right tool: there is no ordering hazard because the registry is a
// function-local static that constructs on first use.
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
//  The component itself.
// ---------------------------------------------------------------------------
class ScriptComponent final : public Component {
public:
    static constexpr const char* kTypeName = "ScriptComponent";
    static StringId TypeIdStatic();

    ~ScriptComponent() override;

    StringId    TypeId() const override { return TypeIdStatic(); }
    const char* TypeName() const override { return kTypeName; }

    // Scene-file field:
    //   "script": "PlayerController"
    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    const std::string& ScriptName() const { return m_scriptName; }

    // Rebinds to a different script. Destroys any live behaviour first, so
    // swapping a script in the Inspector runs OnDestroy on the old one rather
    // than dropping it on the floor.
    void SetScriptName(std::string_view name);

    // False when the name is not registered in this build - the "written but
    // not compiled yet" case. The Inspector shows this in red.
    bool IsResolved() const { return m_behaviour != nullptr; }

    // For the ScriptSystem only.
    void Tick(f32 deltaSeconds);
    void DispatchCollision(StringId messageType, EntityHandle other);

private:
    void Bind();
    void Unbind();

    std::string                      m_scriptName;
    std::unique_ptr<ScriptBehaviour> m_behaviour;
    bool                             m_started = false;
};

// ---------------------------------------------------------------------------
//  The system that ticks them.
//
//  kGameplay (200) - BEFORE movement at 300 and collision at 400, so a script
//  that sets a velocity this step has it integrated this step rather than
//  next. That is the ordering SystemOrder.h exists to make arguable rather
//  than accidental.
// ---------------------------------------------------------------------------
class ScriptSystem final : public System {
public:
    void        Update(f32 deltaSeconds) override;
    const char* Name() const override { return "ScriptSystem"; }
    i32         Order() const override { return SystemStage::kGameplay; }

    static void Register(ScriptComponent& script);
    static void Unregister(ScriptComponent& script);
    static void Clear();
    static usize Count();

    // How many attached scripts could NOT be bound. Surfaced in the editor,
    // because "nothing happens when I press Play" and "three scripts are not
    // compiled into this build" are the same fact and only one of them is
    // actionable.
    static usize UnresolvedCount();

    static void RegisterComponentTypes();

    // Subscribes to the collision messages and forwards them to behaviours.
    // Separate from Register so the subscription happens once at boot rather
    // than once per component.
    static void SubscribeToCollisions();
};

} // namespace eng
