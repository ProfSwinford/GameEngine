#pragma once

// ============================================================================
//  Entities.h - entity ids, the entities themselves, and the create/destroy queue.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <engine/modules/Math.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
//  EntityId.h - how one entity refers to another.
//
//  WHY NOT JUST USE AN Entity* POINTER
//  Entities are destroyed while the game is running - a pickup is collected,
//  an enemy dies. Anything still holding a raw pointer to it is then holding a
//  pointer to memory that has been reused for something else. Reading through
//  it does not crash reliably; it usually gives you nonsense, which is worse.
//
//  An EntityId is a pair of plain numbers instead:
//
//      index       which slot in the scene's list of entities
//      generation  which occupant of that slot
//
//  The scene bumps a slot's generation each time it is reused. So a saved id
//  saying "slot 7, generation 3" can be checked: if slot 7 is now on
//  generation 4, the entity being referred to is gone, and the scene says so
//  instead of handing back whatever now lives there.
//
//  This is why the editor's Inspector can hold on to a selection safely, and
//  why deleting the selected entity produces an empty Inspector rather than a
//  crash. Unity does the same thing with its instance IDs.
//
//  A default-constructed EntityId has index -1 and refers to nothing, so a
//  field somebody forgot to fill in is obviously empty rather than accidentally
//  pointing at the first entity in the scene.
// ============================================================================

namespace eng {

struct EntityId {
    int index      = -1;
    int generation = 0;

    bool IsNull() const { return index < 0; }

    // Two ids refer to the same entity only when BOTH parts match. A matching
    // index with a different generation means "the same slot, but the entity
    // that used to be in it has gone" - which is the whole reason the
    // generation is there.
    friend bool operator==(const EntityId& a, const EntityId& b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const EntityId& a, const EntityId& b) { return !(a == b); }

    // Written out rather than left to the compiler, because "what does it mean
    // for one id to be less than another?" has no meaning in the game - it is
    // needed only so an EntityId can be a key in a std::set or std::map, which
    // keep their contents in order. The collision system uses that to remember
    // which pairs were touching last step.
    //
    // Compare the index first; only if those are equal does the generation
    // decide. That is the same rule as alphabetical order on two-letter words.
    friend bool operator<(const EntityId& a, const EntityId& b) {
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return a.generation < b.generation;
    }
};

} // namespace eng


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



namespace eng {

class Component;
class Scene;
class Transform2D;

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
    template <typename T>
    T* Find() {
        return static_cast<T*>(FindComponent(T::kTypeName));
    }
    template <typename T>
    const T* Find() const {
        return static_cast<const T*>(FindComponent(T::kTypeName));
    }

    bool RemoveComponent(std::string_view typeName);

    std::size_t ComponentCount() const { return m_components.size(); }
    Component*  ComponentAt(std::size_t index);
    void        ForEachComponent(const std::function<void(Component&)>& fn);

    // Every entity has a transform, always. It is created automatically rather
    // than required from the data file, because "an entity with no position"
    // is not a useful thing and making it optional would put a null check in
    // every system in the engine.
    Transform2D&       Transform();
    const Transform2D& Transform() const;

    bool IsAlive() const { return m_alive; }

private:
    friend class Scene;

    void DestroyInternal();

    EntityId    m_id{};
    std::string m_name;
    Scene*      m_scene = nullptr;

    // std::unique_ptr means the entity OWNS its components: when the vector is
    // destroyed, so are they, with nothing to remember. The pointer is needed
    // (rather than storing components by value) because each one is a
    // different derived type and only a pointer can refer to all of them.
    std::vector<std::unique_ptr<Component>> m_components;

    bool m_alive = false;
};

} // namespace eng


// ============================================================================
//  DeferredOps.h - creating and destroying entities SAFELY, at one known point
//  in the frame.
//
//  ==========================================================================
//  THE PROBLEM
//
//  A system is walking its list of components. One entity's update spawns a
//  bullet and destroys an enemy. Both of those change the very lists that are
//  currently being walked.
//
//  In some languages, modifying a collection while looping over it raises an
//  error immediately. In C++ it does not. Adding to a std::vector can move all
//  of its contents somewhere else in memory, and the loop carries on reading
//  the old location. Sometimes that works. Sometimes it reads a destroyed
//  object. Sometimes it works for months and then stops.
//
//  Concretely here: removing a sprite moves the LAST entry in the render list
//  into the hole. If that happens mid-draw, one sprite gets drawn twice and
//  another gets skipped. That is the mild version.
//
//  THE FIX
//  Nothing structural happens immediately. Spawns and destroys go into QUEUES
//  which are applied at ONE point - stage 600 in the system order, after every
//  system has finished and after messages have been delivered.
//  ==========================================================================
//
//  FOUR RULES, WRITTEN DOWN
//
//  1. AN ENTITY DESTROYED THIS FRAME STILL EXISTS FOR THE REST OF IT.
//     It still draws (one extra frame of a dead thing is invisible at 60
//     frames per second) and it still updates (its own update is what asked
//     to be destroyed; cutting it off halfway would leave whatever it was
//     doing half done). It does NOT collide - "destroyed but still hurting
//     you" is a genuinely confusing bug.
//
//  2. AN ENTITY SPAWNED THIS FRAME STARTS NEXT FRAME. It is created after
//     every system has run, so its first update is on the following tick.
//
//  3. DESTROYING SOMETHING TWICE IS HARMLESS. Game code does this constantly -
//     two bullets hit the same enemy on the same tick - so the queue ignores
//     the duplicate instead of treating it as an error.
//
//  4. THE QUEUE IS DRAINED ONCE PER TICK. Anything spawned WHILE draining
//     happens next frame instead. Draining repeatedly until empty risks never
//     finishing, because something that spawns a copy of itself is a perfectly
//     reasonable thing to write.
// ============================================================================


namespace eng {

class Scene;

class DeferredOps {
public:
    // Ask for an entity to be destroyed. It happens at stage 600, not now.
    //
    // To CREATE an entity, call Scene::CreateEntity directly. Creating is safe
    // at any time because it only ever appends; destroying is the dangerous
    // half, because it removes entries from the very lists systems are
    // walking, and that is what this queue exists to make safe.
    static void QueueDestroy(EntityId id);

    // True between QueueDestroy and the moment the queue is applied. Systems
    // that must not act on something already dying check this - see rule 1.
    static bool IsPendingDestroy(EntityId id);

    // Applies everything queued. Called once per simulation step, at stage
    // 600, and never from inside a system's Update.
    static void Apply(Scene& scene);

    static void Clear();

    static std::size_t PendingDestroyCount();
};

} // namespace eng
