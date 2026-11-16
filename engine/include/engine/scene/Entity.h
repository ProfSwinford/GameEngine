#pragma once

// ============================================================================
//  Entity.h - a thing in the world.
//
//  An entity is a NAME, an ID, and a bag of components. It has no behaviour of
//  its own at all. This is the same model Unity uses: a GameObject is an empty
//  container and everything interesting is a component attached to it.
//
//  WHY COMPOSITION INSTEAD OF INHERITANCE
//  The obvious design is a family tree of classes: GameObject, then Character,
//  then Player. That works for about four kinds of thing. Then you need a door
//  that moves AND takes damage; moving lives in one branch of the tree and
//  taking damage in another, so you either copy code between branches or push
//  everything up into the base class until every object carries every feature
//  in the game.
//
//  Composition turns that inside out. A door that moves and takes damage HAS a
//  Transform, a Sprite, a Mover and a Health. Nothing is inherited and nothing
//  is duplicated - and crucially, the LIST OF COMPONENTS IS DATA, so a new
//  kind of object can be built in a scene file with no programming at all.
//
//  WHAT HAPPENS WHEN AN ENTITY IS DESTROYED, in order:
//
//    1. OnDetach() is called on every component, in the REVERSE of the order
//       they were attached, so each one unhooks itself while its owner is
//       still valid and while anything it depends on is still there.
//    2. The components are then destroyed, also in reverse order.
//    3. The transform hands its children back to the world, keeping them
//       where they visibly are.
//    4. Only then does the scene bump the slot's generation, which is what
//       makes every EntityId referring to it detectably out of date.
//
//  Step 1 happening before step 2 is the load-bearing part. A component that
//  unhooked itself in its DESTRUCTOR would be doing so while it was already
//  half torn down.
// ============================================================================

#include <engine/scene/EntityId.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class Component;
class Entity;
class Scene;
class Transform2D;

// ===========================================================================
//  WHAT A LOOKUP IS ACTUALLY BEING ASKED FOR, when the name it was handed is
//  a REFERENCE rather than the thing itself.
//
//  Inside a script a bare component name means a reference to one, so
//  `SpriteComponent renderer;` is a slot you drag an entity onto - exactly as
//  `Invisibility invis;` is for a script the game defines. That is the whole
//  point: an engine component and a script are the same thing to write.
//
//  But it also means `Find<SpriteComponent>()` arrives here asking for a
//  ComponentRef, which is not a component, has no kTypeName, and would fail
//  with an error naming a type the author never typed.
//
//  SO A LOOKUP UNWRAPS FIRST. The primary template says "not a reference, use
//  it as it stands"; ComponentRef.h and ScriptRef.h each specialise it for
//  their own type. Those two include this header rather than the other way
//  round, which is why the specialisations cannot live here - and they do not
//  need to, because a specialisation only has to be visible where the
//  template is INSTANTIATED, and every script sees the whole of
//  <engine/Engine.h> before its own first line.
// ===========================================================================
template <class T>
struct FindTargetOf {
    using type = T;
};

// ---------------------------------------------------------------------------
//  DECLARED HERE, DEFINED IN ScriptComponent.h, and the split is forced.
//
//  Finding a script means dynamic_cast-ing to it, which needs the complete
//  ScriptBehaviour - and ScriptBehaviour's own header includes this one. A
//  template may be declared now and defined later in the same translation
//  unit, so Entity::Find can route a script lookup through this name and the
//  definition is in scope by the time anything instantiates it.
//
//  A translation unit that includes Entity.h WITHOUT ScriptComponent.h and
//  asks for a script would fail to link rather than to compile. Nothing in
//  the engine does, and a script cannot: <engine/Engine.h> brings in both.
// ---------------------------------------------------------------------------
template <class T>
T* FindScriptOnEntity(Entity& entity);


class Entity {
public:
    Entity() = default;
    ~Entity();

    // Entities are not copyable. Copying one would raise "does the copy have
    // the same id? the same children? the same components?", and every answer
    // is surprising. Use Scene::DuplicateEntity, which is explicit about what
    // it does.
    Entity(const Entity&)            = delete;
    Entity& operator=(const Entity&) = delete;

    EntityId           Id() const   { return m_id; }
    const std::string& Name() const { return m_name; }
    Scene*             GetScene() const { return m_scene; }

    void SetName(std::string_view name);

    // ---- what KIND of thing this is ---------------------------------------
    //
    // A tag. Unity's, and it is a different thing from all three of the
    // identities this engine already had:
    //
    //   a NAME is unique and is an identity - parents and entity references
    //   are recorded by name, so two entities called "Bullet" is a bug (see
    //   Scene::CreateEntityUniquelyNamed). You cannot label fifty bullets
    //   with it.
    //
    //   a COLLISION LAYER decides what collides with what. Before this
    //   existed it was the only shared label available, so the engine's own
    //   scripts borrowed it: Pickup.cpp asked `FindCollider(*partner)->
    //   Layer() != "Player"` to mean "is this the player". That works until
    //   you want to label something WITHOUT changing what it collides with,
    //   or label something with no collider at all - a spawn point, a trigger
    //   volume's parent, the game-rules object.
    //
    //   a COMPONENT TYPE says what an entity can do, not what it is. Every
    //   bullet and every pickup has a SpriteComponent.
    //
    // So: not unique, no effect on anything, and present on everything.
    //
    // EMPTY MEANS UNTAGGED, and HasTag("") is therefore FALSE rather than
    // true-for-everything. A test that quietly answered yes for every
    // untagged entity because the string it was given happened to be empty is
    // the exact failure this is meant to replace.
    const std::string& Tag() const { return m_tag; }
    void               SetTag(std::string_view tag) { m_tag = tag; }
    bool               HasTag(std::string_view tag) const {
        return !tag.empty() && m_tag == tag;
    }

    // ---- components -------------------------------------------------------

    // Adds a component by its type NAME, which is what a scene file contains.
    // Returns nullptr for an unknown name - that is an error in the data file,
    // so it is reported and skipped rather than treated as a crash.
    Component* AddComponent(std::string_view typeName);
    Component* AddComponent(std::unique_ptr<Component> component);

    // Looks for a component by type name. Returning nullptr is a perfectly
    // ordinary answer - "does this entity have a collider?" is a normal
    // question - so callers check the result rather than assuming.
    Component*       FindComponent(std::string_view typeName);
    const Component* FindComponent(std::string_view typeName) const;

    // The typed version, for when you know what you want at compile time:
    //
    //     if (SpriteComponent* sprite = entity.Find<SpriteComponent>()) { ... }
    //
    // It still looks the component up by its type name underneath, so it finds
    // exactly the same thing the scene file would.
    //
    // IT UNWRAPS A REFERENCE FIRST - see FindTargetOf above. Inside a script
    // `SpriteComponent` names a ComponentRef and `Invisibility` names a
    // ScriptRef, so without this the commonest spelling of the commonest
    // lookup would fail on a type the author never wrote. A script goes to
    // FindScriptOnEntity, a component to the name lookup below, and the two
    // read identically at the call site, which is the entire point.
    template <typename T>
    auto* Find() {
        using Target = typename FindTargetOf<T>::type;
        if constexpr (requires { Target::kTypeName; }) {
            return static_cast<Target*>(FindComponent(Target::kTypeName));
        } else {
            // No kTypeName: a script, or an abstract component base. The
            // static_assert inside FindScriptOn tells the two apart and says
            // what to write for each.
            return FindScriptOnEntity<Target>(*this);
        }
    }

    // The const one answers about COMPONENTS only. A script found through it
    // would be handed out const, and every hook on it is non-const, so the
    // call could not do anything with what it got.
    //
    // The message is spelled out rather than hoisted into a constant, because
    // a static_assert message has to be a string LITERAL until C++26 and a
    // project's scripts compile as C++20 against this header.
    template <typename T>
    const auto* Find() const {
        using Target = typename FindTargetOf<T>::type;
        static_assert(
            requires { Target::kTypeName; },
            "A const Find<T>() looks a COMPONENT up by its kTypeName, and T has "
            "none. For a SCRIPT, ask through a non-const entity - or hold "
            "`TheScript member;` and call member->Method(), which is the spelling "
            "this engine prefers. If T is an abstract component base such as "
            "eng::ColliderComponent, it has no kTypeName of its own: name the "
            "concrete one you mean, or call eng::FindCollider(Entity&).");
        return static_cast<const Target*>(FindComponent(Target::kTypeName));
    }

    // Adds a component by its C++ TYPE, which is what code wants:
    //
    //     auto* body = entity.Add<eng::Rigidbody2DComponent>();
    //
    // The same thing as AddComponent(T::kTypeName) except that it hands back
    // the right type, so the cast at every call site disappears. It goes
    // through ComponentFactory rather than `new T` on purpose: the factory is
    // what the scene file uses, so a component added this way is built exactly
    // as a loaded one is - and a type somebody forgot to register fails here
    // rather than producing an entity that cannot be saved.
    template <typename T>
    typename FindTargetOf<T>::type* Add() {
        using Target = typename FindTargetOf<T>::type;
        static_assert(requires { Target::kTypeName; },
                      "Add<T>() creates a COMPONENT, and T is not one. A script is "
                      "attached by name rather than by type - see ScriptComponent - "
                      "and an abstract component base cannot be created at all.");
        return static_cast<Target*>(AddComponent(Target::kTypeName));
    }

    // Every component of this type, not just the first. Several scripts, or
    // several colliders making one compound shape, are both ordinary.
    //
    // `into` is CLEARED first and then filled; the caller owns the vector so
    // that a per-frame walk allocates once. The return value is how many were
    // written, which is also into.size() - so "did I find any" needs no
    // counter of its own.
    //
    // DEFINED IN Scene.h, not here. Its body asks a Component for its
    // TypeName(), and that expression does not depend on T - so the compiler
    // checks it where the template is WRITTEN rather than where it is used,
    // and Component is only forward-declared in this file. Component.h
    // includes this one, so it cannot be included back.
    //
    // Find<T> above gets away with staying here because its only use of
    // Component is a static_cast to T, which IS dependent on T and is
    // therefore deferred to the point of use.
    template <typename T>
    std::size_t FindAll(std::vector<typename FindTargetOf<T>::type*>& into);

    // ---- the hierarchy, by ENTITY rather than by transform ----------------
    //
    // Transform2D::Children() hands back transforms, and a transform is a dead
    // end: the math layer knows nothing about entities, so there is no route
    // from a child's transform to the components beside it. These cross that
    // gap, through the table described at Scene::EntityOf.
    //
    // THEY NEED A SCENE, so an entity that is not in one answers 0 and null.
    // That is not a defensive check for something impossible - an Entity built
    // on the stack in a test has no scene.
    std::size_t   ChildCount() const;
    Entity*       ChildAt(std::size_t index) const;
    Entity*       ParentEntity() const;

    // The nearest ancestor with no parent - this entity itself when it already
    // has none. Unity's transform.root.
    Entity* RootEntity() const;

    // A child by name. DIRECT CHILDREN ONLY, deliberately: a recursive search
    // would find a grandchild that happens to share a name with something
    // nearer, and "the Muzzle on my gun" is a question about one level.
    // FindInChildren below is the recursive one, and it searches by TYPE.
    Entity* FindChild(std::string_view name) const;

    bool IsDescendantOf(const Entity& candidate) const;

    // ---- components, anywhere in the subtree or up the chain --------------
    //
    // Unity's GetComponentInChildren / GetComponentInParent. DEFINED IN
    // Scene.h, not here, because they walk the hierarchy and therefore need
    // the complete Scene - and Scene.h includes this file, so it cannot be the
    // other way round. Every script gets them: <engine/Engine.h> includes
    // Scene.h.
    //
    // `includeSelf` DEFAULTS TO TRUE because that is what Unity's versions do,
    // and because the difference is a silent one - a component found on the
    // entity itself when you meant only its children is not an error anywhere,
    // it is just the wrong answer.
    template <typename T>
    typename FindTargetOf<T>::type* FindInChildren(bool includeSelf = true);
    template <typename T>
    typename FindTargetOf<T>::type* FindInParents(bool includeSelf = true);

    // Every match in the subtree, in depth-first order. Same `into` contract
    // as FindAll above: cleared first, caller owns it, count returned.
    template <typename T>
    std::size_t FindAllInChildren(
        std::vector<typename FindTargetOf<T>::type*>& into,
        bool includeSelf = true);

    // The same up the parent chain - Unity's GetComponentsInParent. NEAREST
    // FIRST, so index 0 is this entity when includeSelf is true and otherwise
    // its parent, which is the order somebody walking the result expects.
    //
    // THE ASYMMETRY THIS FILLS IN: FindInParents existed and FindAllInParents
    // did not, so "every Health above me" was the one lookup of the four with
    // no spelling at all. It was not a decision - the other three are here.
    template <typename T>
    std::size_t FindAllInParents(
        std::vector<typename FindTargetOf<T>::type*>& into,
        bool includeSelf = true);

    // Removes the FIRST component with this type name. Fine when a type can
    // only sensibly appear once; see RemoveComponentAt when it cannot.
    bool RemoveComponent(std::string_view typeName);

    // Removes the component at a position, which is what ComponentAt reports.
    //
    // THE REASON THIS EXISTS: an entity may hold several components of the
    // same type - several scripts, most obviously - and RemoveComponent above
    // finds them by name, so it would always take the first one. Asking the
    // Inspector to remove the third script and watching the first disappear
    // is not a small annoyance; it is the tool doing something other than
    // what was asked.
    //
    // Positions shift when anything is removed, so an index is only good
    // until the next change. Take it and use it, rather than storing it.
    bool RemoveComponentAt(std::size_t index);

    std::size_t ComponentCount() const { return m_components.size(); }
    Component*  ComponentAt(std::size_t index);
    void        ForEachComponent(const std::function<void(Component&)>& fn);

    // The const half, for code that only wants to LOOK at what is attached -
    // "does this entity carry a script called DoorSwitch?" is a question with
    // no business taking a mutable entity. FindComponent and Transform below
    // both have the same pair for the same reason.
    const Component* ComponentAt(std::size_t index) const;

    // Every entity has a transform, always. It is created automatically rather
    // than required from the data file, because "an entity with no position"
    // is not a useful thing and making it optional would put a null check in
    // every system in the engine.
    Transform2D&       Transform();
    const Transform2D& Transform() const;

    // ---- switched on, or switched off -------------------------------------
    //
    // Unity's GameObject.SetActive / activeSelf / activeInHierarchy. An
    // inactive entity is still there - it keeps its components, its values and
    // its place in the hierarchy, and it saves and loads - but NOTHING ACTS ON
    // IT:
    //
    //   its sprites and tilemaps are not drawn;
    //   its colliders do not collide and are invisible to every Physics2D
    //     query;
    //   its rigidbody is not integrated, and KEEPS the velocity it had;
    //   its scripts do not tick and receive no collision or trigger events;
    //   its UI is neither drawn nor clickable;
    //   a camera on it cannot be the active camera.
    //
    // ==================================================================
    //  ALL OF IT OR NONE OF IT. THAT IS THE WHOLE POINT.
    //
    //  A flag that the renderer honours and the physics does not is WORSE
    //  THAN NOT HAVING ONE: the thing disappears, which looks like it worked,
    //  and then goes on hurting the player from somewhere invisible. Every
    //  system in the engine checks this, there is a test for each one, and
    //  anything added later has to do the same.
    // ==================================================================
    //
    // WHAT IT IS NOT. It does not destroy, so it is not a way to delete
    // something cheaply - DeferredOps::QueueDestroy is. It is not reference
    // counted: switching it off twice and on once leaves it on. And there are
    // no OnEnable/OnDisable hooks yet; a script that must notice can compare
    // against what it saw last step.
    bool IsActiveSelf() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    // SELF AND EVERY ANCESTOR. Switching off a parent switches off everything
    // under it, which is what makes "turn the whole menu off" one call rather
    // than a walk - and it is what every system actually asks.
    //
    // WALKED EVERY TIME RATHER THAN CACHED. A cached answer would have to be
    // invalidated on every SetActive anywhere above, and on every reparent -
    // and Transform2D::SetParent knows nothing about entities, so there is no
    // hook there to put it in. A stale cache would mean an entity that is
    // invisible and still colliding, which is the exact failure the block
    // above exists to prevent. The walk stops at the first inactive ancestor
    // and is one hash lookup per level, and in a 2D scene the depth is
    // usually zero.
    bool IsActiveInHierarchy() const;

    bool IsAlive() const { return m_alive; }

private:
    friend class Scene;

    void DestroyInternal();

    EntityId    m_id{};
    std::string m_name;

    // Empty for the overwhelming majority of entities, and an empty
    // std::string costs no allocation - so this is a few bytes per entity for
    // a label that is free when unused. See Tag() above.
    std::string m_tag;

    Scene*      m_scene = nullptr;

    // std::unique_ptr means the entity OWNS its components: when the vector is
    // destroyed, so are they, with nothing to remember. The pointer is needed
    // (rather than storing components by value) because each one is a
    // different derived type and only a pointer can refer to all of them.
    std::vector<std::unique_ptr<Component>> m_components;

    bool m_alive = false;

    // See IsActiveSelf. True by default, and the scene file only records it
    // when it is false - so adding this changed no existing scene file.
    bool m_active = true;
};

} // namespace eng
