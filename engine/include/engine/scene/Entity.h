#pragma once

// =============================================================================
//  WEEK 9 - entities.
//
//  *** THIS FILE AND Component.h ARE WHAT MAKE WEEKS 10-16 POSSIBLE AT ALL. ***
//
//  WHY COMPOSITION RATHER THAN INHERITANCE:
//
//  The obvious design is a hierarchy - GameObject, Character, Player. It works
//  for about four types. Then you need a door that moves and takes damage,
//  moving lives in one branch and taking damage in another, and you either
//  duplicate code or push everything into the base class until it has ninety
//  virtual functions every object pays for.
//
//  Composition inverts it. An entity is an ID plus a bag of components. A door
//  that moves and takes damage HAS a Transform, a Sprite, a Mover and a
//  Health. No inheritance, no duplication, and - critically - the SET OF
//  COMPONENTS IS DATA, so a designer can build a new kind of object in a scene
//  file without a programmer and without a rebuild. That property is the whole
//  reason Phase 2 works.
//
//  ---------------------------------------------------------------------------
//  ENTITIES ARE REFERRED TO BY HANDLE, for exactly the reason assets are: an
//  entity can be destroyed while something still refers to it, and Week 10
//  destroys entities mid-frame on purpose. The editor's Hierarchy panel holds
//  an EntityHandle and resolves it every frame; a cached Entity* would crash
//  the first time the selected entity was destroyed.
//
//  ---------------------------------------------------------------------------
//  WHAT HAPPENS TO COMPONENTS WHEN AN ENTITY IS DESTROYED - written down
//  before the code, as Week 10 requires:
//
//    1. OnDetach() is called on every component, in REVERSE attach order, so
//       each deregisters from its system while its owner is still valid and
//       while everything it might depend on is still attached.
//    2. Then the components are destroyed, also in reverse attach order.
//    3. Then the entity's transform detaches its children, orphaning them to
//       the root with their world transforms preserved (see Transform2D.h).
//    4. Only then does the slot's generation increment, which is what makes
//       every outstanding handle to it stale rather than dangling.
//
//  Step 1 before step 2 is the load-bearing part. A component that
//  deregistered in its DESTRUCTOR would be doing so with its owner pointer
//  already half-torn-down, and the system it deregisters from would be
//  swapping records around underneath a partially destroyed object.
//
//  ---------------------------------------------------------------------------
//  DESIGNED FOR THE C# BOUNDARY: components are found by string id and carry a
//  runtime type NAME, never a C++ type known only at compile time. The binding
//  is not built; it is not made impossible either.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/resource/Handle.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

class Component;
class Scene;
class Transform2D;

struct EntityTag {};
using EntityHandle = Handle<EntityTag>;

class Entity {
public:
    Entity() = default;
    ~Entity();

    Entity(const Entity&)            = delete;
    Entity& operator=(const Entity&) = delete;

    EntityHandle       Handle() const { return m_handle; }
    StringId           NameId() const { return m_nameId; }
    const std::string& Name() const   { return m_name; }
    Scene*             GetScene() const { return m_scene; }

    void SetName(std::string_view name);

    // Attaches a component built by the factory from a type NAME - which is
    // what a scene file contains. Returns the component, or null if the type
    // is not registered (reported, not asserted: an unknown component type in
    // a data file is an authoring error, not a programmer error).
    Component* AddComponent(std::string_view typeName);
    Component* AddComponent(std::unique_ptr<Component> component);

    // Finding a component an entity does not have is a NORMAL QUESTION, not an
    // error. Returns null.
    Component*       FindComponent(StringId typeId);
    const Component* FindComponent(StringId typeId) const;

    // Typed convenience for engine and gameplay code. Uses the type's own
    // TypeIdStatic(), so it stays a string-id lookup underneath and a C#
    // caller can do exactly the same thing by name.
    template <typename T>
    T* Find() {
        return static_cast<T*>(FindComponent(T::TypeIdStatic()));
    }
    template <typename T>
    const T* Find() const {
        return static_cast<const T*>(FindComponent(T::TypeIdStatic()));
    }

    bool RemoveComponent(StringId typeId);

    usize      ComponentCount() const { return m_components.size(); }
    Component* ComponentAt(usize index);
    void       ForEachComponent(const std::function<void(Component&)>& fn);

    // Every entity has a transform. It is created with the entity rather than
    // required from the data file, because "an entity with no position" is not
    // a thing this engine has a use for and making it optional would put a
    // null check in every system.
    Transform2D&       Transform();
    const Transform2D& Transform() const;

    bool IsAlive() const { return m_alive; }

private:
    friend class Scene;

    void DestroyInternal();

    EntityHandle                            m_handle{};
    StringId                                m_nameId{};
    std::string                             m_name;
    Scene*                                  m_scene = nullptr;
    std::vector<std::unique_ptr<Component>> m_components;
    bool                                    m_alive = false;
};

} // namespace eng
