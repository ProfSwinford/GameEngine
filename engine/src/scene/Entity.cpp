// ============================================================================
//  Entity.cpp - entities and their components. See Entity.h for the
//  destruction order this file implements.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <algorithm>

namespace eng {

Entity::~Entity() {
    DestroyInternal();
}

void Entity::DestroyInternal() {
    if (!m_alive && m_components.empty()) {
        return;   // already done
    }

    // Step 1: let every component unhook itself, newest first.
    //
    // rbegin/rend walk the vector backwards, which is what "reverse of the
    // order they were attached" means. Each component is still fully formed
    // and still owned by this entity at this point, which is exactly the state
    // OnDetach needs.
    for (auto it = m_components.rbegin(); it != m_components.rend(); ++it) {
        if (*it != nullptr) {
            (*it)->OnDetach();
        }
    }

    // Step 2: destroy them, also newest first. pop_back destroys the
    // unique_ptr at the end, which destroys the component it owns.
    while (!m_components.empty()) {
        m_components.pop_back();
    }

    // Step 3 happens on its own: destroying the TransformComponent above runs
    // ~Transform2D, which hands any children back to the world.

    m_alive = false;
}

void Entity::SetName(std::string_view name) {
    m_name.assign(name);
}

Component* Entity::AddComponent(std::string_view typeName) {
    std::unique_ptr<Component> component = ComponentFactory::Create(typeName);
    if (component == nullptr) {
        // A component type that does not exist is a mistake in the scene FILE,
        // not in the program, so it is reported by name and skipped. The rest
        // of the entity still loads.
        ENGINE_LOG_ERROR(Channels::kScene,
                         "entity '{}': there is no component type called '{}'",
                         m_name, typeName);
        return nullptr;
    }
    return AddComponent(std::move(component));
}

Component* Entity::AddComponent(std::unique_ptr<Component> component) {
    if (component == nullptr) {
        return nullptr;
    }

    // Keep a plain pointer to it before handing ownership to the vector, so
    // there is still something to call OnAttach on and to return.
    Component* raw = component.get();
    raw->m_owner   = this;

    // std::move hands the unique_ptr's ownership into the vector. After this
    // line `component` is empty - which is the point: there is only ever one
    // owner.
    m_components.push_back(std::move(component));

    // OnAttach runs AFTER the component is in the list and its owner is set.
    // Components register themselves with systems in here, and some of those
    // systems immediately ask the component about its entity - so it has to be
    // fully in place first.
    raw->OnAttach();
    return raw;
}

Component* Entity::FindComponent(std::string_view typeName) {
    for (const std::unique_ptr<Component>& component : m_components) {
        if (component != nullptr && component->TypeName() == typeName) {
            return component.get();
        }
    }
    return nullptr;
}

// The same search again, for when you only have a const Entity. Written out
// rather than casting the const away and calling the version above: six
// obvious lines are worth more here than a clever one.
const Component* Entity::FindComponent(std::string_view typeName) const {
    for (const std::unique_ptr<Component>& component : m_components) {
        if (component != nullptr && component->TypeName() == typeName) {
            return component.get();
        }
    }
    return nullptr;
}

bool Entity::RemoveComponent(std::string_view typeName) {
    // std::find_if searches with a condition. The [typeName] in the brackets
    // captures the parameter so the lambda can use it.
    const auto it = std::find_if(m_components.begin(), m_components.end(),
                                 [typeName](const std::unique_ptr<Component>& component) {
                                     return component != nullptr &&
                                            component->TypeName() == typeName;
                                 });
    if (it == m_components.end()) {
        return false;
    }
    (*it)->OnDetach();     // unhook before destroying, as always
    m_components.erase(it);
    return true;
}

bool Entity::RemoveComponentAt(std::size_t index) {
    if (index >= m_components.size() || m_components[index] == nullptr) {
        return false;
    }

    // OnDetach FIRST, while the component is still fully formed and still
    // owned by this entity - the same order the destructor above uses, and
    // for the same reason. A component that unhooks itself from a system
    // needs to still exist while it does it.
    m_components[index]->OnDetach();
    m_components.erase(m_components.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

Component* Entity::ComponentAt(std::size_t index) {
    return (index < m_components.size()) ? m_components[index].get() : nullptr;
}

const Component* Entity::ComponentAt(std::size_t index) const {
    return (index < m_components.size()) ? m_components[index].get() : nullptr;
}

void Entity::ForEachComponent(const std::function<void(Component&)>& fn) {
    // Looped by index rather than with a range-for on purpose. The callback is
    // allowed to ADD a component - the Inspector's "Add Component" button does
    // exactly that - and adding to a vector can move its contents somewhere
    // else in memory, which would leave a range-for reading the old location.
    // Re-reading m_components.size() each time round is what makes that safe.
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (m_components[i] != nullptr) {
            fn(*m_components[i]);
        }
    }
}

Transform2D& Entity::Transform() {
    // Created the first time it is asked for rather than in the constructor,
    // so that the component list keeps the order the scene file used. The
    // return is guaranteed to be a real transform, which is why no system in
    // the engine has to check for a null one.
    TransformComponent* component = Find<TransformComponent>();
    if (component == nullptr) {
        component = static_cast<TransformComponent*>(
            AddComponent(std::make_unique<TransformComponent>()));
    }
    return component->Transform();
}

// ---------------------------------------------------------------------------
//  The hierarchy, by entity.
//
//  Every one of these goes through Scene::EntityOf, which is the table
//  TransformComponent fills in - see the note at its declaration in Scene.h
//  for why the math layer is not the place for it. An entity with no scene
//  answers 0 and null, which is an Entity built on the stack in a test.
// ---------------------------------------------------------------------------

std::size_t Entity::ChildCount() const {
    return Transform().Children().size();
}

Entity* Entity::ChildAt(std::size_t index) const {
    const std::vector<Transform2D*>& children = Transform().Children();
    if (index >= children.size() || children[index] == nullptr) {
        return nullptr;
    }
    Scene* scene = GetScene();
    return scene != nullptr ? scene->EntityOf(*children[index]) : nullptr;
}

Entity* Entity::ParentEntity() const {
    const Transform2D* parent = Transform().Parent();
    if (parent == nullptr) {
        return nullptr;
    }
    Scene* scene = GetScene();
    return scene != nullptr ? scene->EntityOf(*parent) : nullptr;
}

Entity* Entity::RootEntity() const {
    Scene* scene = GetScene();
    if (scene == nullptr) {
        return nullptr;
    }

    const Transform2D* node = &Transform();
    while (node->Parent() != nullptr) {
        node = node->Parent();
    }
    // Walked by TRANSFORM rather than by repeated ParentEntity(), so a gap in
    // the table - a transform whose entity has gone - cannot stop the walk
    // halfway and return the wrong answer as if it were the root.
    return scene->EntityOf(*node);
}

Entity* Entity::FindChild(std::string_view name) const {
    if (name.empty()) {
        return nullptr;
    }
    Scene* scene = GetScene();
    if (scene == nullptr) {
        return nullptr;
    }
    for (Transform2D* child : Transform().Children()) {
        if (child == nullptr) {
            continue;
        }
        if (Entity* owner = scene->EntityOf(*child);
            owner != nullptr && owner->Name() == name) {
            return owner;
        }
    }
    return nullptr;
}

bool Entity::IsActiveInHierarchy() const {
    if (!m_active) {
        return false;
    }

    // No parents means nothing above to switch it off, and that is the common
    // case - it answers here without touching the scene at all.
    const Transform2D* parent = Transform().Parent();
    if (parent == nullptr) {
        return true;
    }

    Scene* scene = GetScene();
    if (scene == nullptr) {
        // An entity with a parent but no scene cannot have its ancestors
        // resolved, and guessing "inactive" would make such an entity
        // invisible to everything for no reason anybody could find. Active is
        // the answer that matches what IsActiveSelf already said.
        return true;
    }

    for (const Transform2D* node = parent; node != nullptr; node = node->Parent()) {
        const Entity* owner = scene->EntityOf(*node);
        // A transform with no entity is skipped rather than treated as
        // inactive. It means the chain passes through something that is not an
        // entity in this scene, which nothing in the engine currently builds -
        // and "an unrelated transform switched my sprite off" would be an
        // extremely confusing bug to be handed.
        if (owner != nullptr && !owner->m_active) {
            return false;
        }
    }
    return true;
}

bool Entity::IsDescendantOf(const Entity& candidate) const {
    // Answered entirely in the transform tree, which already knows how to do
    // this and already refuses to build a loop - so there is no walk here
    // that could run forever.
    return Transform().IsDescendantOf(&candidate.Transform());
}

const Transform2D& Entity::Transform() const {
    // This one genuinely needs the cast, and it is the only place in the engine
    // that does. The version above CREATES the transform if it is missing, and
    // creating something is a change - which a const function is not allowed to
    // make. The alternative would be for this to return a pointer that can be
    // null, and then every single use of Transform() would need a null check.
    return const_cast<Entity*>(this)->Transform();
}

} // namespace eng
