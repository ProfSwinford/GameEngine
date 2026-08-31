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
