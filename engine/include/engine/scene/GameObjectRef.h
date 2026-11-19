#pragma once

// ============================================================================
//  GameObjectRef.h - one reference that can be a thing in the scene OR a
//  prefab on disk.
//
//  WHY THE TWO WERE SEPARATE, AND WHY THAT WAS WRONG.
//
//  An eng::EntityRef holds a NAME and finds something that already exists in
//  the scene. An eng::PrefabRef holds a PATH and names a template that does
//  not exist yet. Those really are different things, and the engine was right
//  to have both.
//
//  But that is a distinction about STORAGE, and it was leaking out into the
//  script. A field that wanted "the thing to spawn when the player presses
//  fire" had to commit, in C++, to whether that thing would be a prefab or an
//  object already in the level - and changing your mind later meant editing
//  the script, not dragging something different onto it. The Inspector is
//  supposed to be the place that decision lives.
//
//  Unity settled this long ago: `public GameObject thing;` takes either, and
//  Instantiate(thing) works on either. That is what this is.
//
//  ==========================================================================
//  IT IS A VALUE, AND THAT IS WHAT MAKES IT AN INSPECTOR FIELD.
//
//  `GameObject` used to be `typedef eng::Entity*`, and a pointer is the one
//  thing the Inspector cannot draw - an address has nothing to put in a box,
//  and the variable exposer says exactly that and skips it. Which is why the
//  type could not be used as a variable at all, and the reason for the
//  change. The same migration happened to the component aliases before it:
//  see the long note in Engine.h about Rigidbody2D and SpriteRenderer
//  becoming eng::ComponentRef.
//
//  OLD CODE STILL COMPILES. It converts to Entity* implicitly and has
//  operator-> , so `GameObject self = Owner();` and `self->Transform()` and
//  `if (self != nullptr)` all mean what they meant.
//
//  ==========================================================================
//  WHAT A PREFAB ONE RESOLVES TO: NOTHING, AND THAT IS NOT A BUG.
//
//  Target() returns null for a reference holding a prefab, because a prefab
//  is not in the scene - there is no transform to read and no position to
//  ask for. It becomes an entity the moment you Instantiate() it, and the id
//  that comes back is the live thing.
//
//  That is the one place the two halves genuinely cannot behave alike, and
//  pretending otherwise would mean silently instantiating a copy every time
//  somebody read a position. IsPrefab() is there so a script that cares can
//  ask.
//
//  ==========================================================================
//  INSTANTIATING EITHER OF THEM.
//
//  ScriptBehaviour::Instantiate takes one of these and does the right thing:
//  a prefab is built from its file, and a SCENE OBJECT IS CLONED. Cloning is
//  Unity's behaviour for Instantiate(someSceneObject) and it is what makes
//  the two interchangeable at the call site - the alternative, handing back
//  the original, would mean a spawner quietly moving the thing it was meant
//  to copy.
//
//  ==========================================================================
//  IN A SCENE FILE it is written as an object naming which half it is:
//
//      "target": { "entity": "Door" }
//      "target": { "prefab": "prefabs/bullet.oprefab" }
//
//  NOT A BARE STRING WITH A RULE FOR TELLING THEM APART. "does it end in
//  .oprefab" would work until somebody named an entity after a file, and the
//  failure would be a spawner that silently spawns nothing. A plain string IS
//  still accepted on read, as an entity name, because that is what somebody
//  hand-editing a scene will type and it is the harmless half.
// ============================================================================

#include <engine/scene/EntityId.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace eng {

class Entity;

class GameObjectRef {
public:
    enum class Kind {
        None,          // nothing dragged onto it yet
        SceneObject,   // a name, resolved in the live scene
        Prefab,        // a virtual path to a .oprefab
    };

    GameObjectRef() = default;

    // `GameObject thing = nullptr;` - the spelling somebody coming from the
    // pointer typedef will write out of habit.
    GameObjectRef(std::nullptr_t) {}

    // IMPLICIT, so `GameObject self = Owner();` still compiles. It records the
    // entity's NAME rather than its address, for the reason EntityRef.h gives
    // at length: a pointer outlives the thing it points at exactly once, and
    // that once is a crash.
    GameObjectRef(Entity* entity);
    GameObjectRef(const Entity& entity);

    // The two named constructors, because a bare string cannot say which it
    // meant and guessing is how this breaks quietly.
    static GameObjectRef ToSceneObject(std::string_view name);
    static GameObjectRef ToPrefab(std::string_view virtualPath);

    Kind GetKind() const { return m_kind; }
    bool IsPrefab() const { return m_kind == Kind::Prefab; }
    bool IsSceneObject() const { return m_kind == Kind::SceneObject; }

    // Has anything been dragged onto it? Says nothing about whether that
    // thing can still be found - see Target().
    bool IsSet() const { return m_kind != Kind::None && !m_value.empty(); }

    // The name of the scene object, or an empty string when this is a prefab
    // or nothing. Never the path: a caller that mixed the two up would get a
    // path where it expected a name and look everywhere for an entity called
    // "prefabs/bullet.oprefab".
    const std::string& Name() const;

    // The prefab's virtual path, or an empty string when this is a scene
    // object or nothing.
    const std::string& PrefabPath() const;

    // The live entity. Null when this names a prefab, when nothing is set,
    // and when the named object is not in the scene - three different reasons
    // that all mean "there is nothing here to talk to".
    Entity* Target() const;
    EntityId Id() const;

    // IMPLICIT, and deliberately so: every line written against the old
    // pointer typedef goes on meaning what it meant.
    operator Entity*() const { return Target(); }
    Entity* operator->() const { return Target(); }

    void Clear();
    void Set(Entity* entity);
    void SetSceneObject(std::string_view name);
    void SetPrefab(std::string_view virtualPath);

    // The raw stored text, whichever half it is. For the serializer and the
    // Inspector; a script should ask Name() or PrefabPath() so that a mix-up
    // cannot compile.
    const std::string& Value() const { return m_value; }

    friend bool operator==(const GameObjectRef& a, const GameObjectRef& b) {
        return a.m_kind == b.m_kind && a.m_value == b.m_value;
    }

private:
    Kind        m_kind = Kind::None;
    std::string m_value;
};

} // namespace eng
