// WEEK 9 - entities. See Entity.h for the destruction-order contract.

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
        return;
    }

    // STEP 1: OnDetach on every component, in REVERSE attach order, while the
    // owner pointer is still valid and everything they might depend on is
    // still attached. See the contract block in Entity.h - this ordering is
    // the load-bearing part.
    for (auto it = m_components.rbegin(); it != m_components.rend(); ++it) {
        if (*it != nullptr) {
            (*it)->OnDetach();
        }
    }

    // STEP 2: destroy them, also in reverse attach order.
    while (!m_components.empty()) {
        m_components.pop_back();
    }

    // STEP 3: the transform detaches its children, orphaning them to the root
    // with their world transforms preserved. That happens inside
    // ~Transform2D, which runs when the TransformComponent is destroyed above.

    m_alive = false;
}

void Entity::SetName(std::string_view name) {
    m_name.assign(name);
    m_nameId = Intern(m_name);
}

Component* Entity::AddComponent(std::string_view typeName) {
    std::unique_ptr<Component> component = ComponentFactory::Create(typeName);
    if (component == nullptr) {
        // An unknown component type in a data file is an AUTHORING error, not
        // a programmer error, so it is reported and skipped rather than
        // asserted. The message names the type so the author can fix the file.
        ENGINE_LOG_ERROR(Channels::kScene,
                         "entity '{}': unknown component type '{}' - is it registered "
                         "with ComponentFactory?", m_name, typeName);
        return nullptr;
    }
    return AddComponent(std::move(component));
}

Component* Entity::AddComponent(std::unique_ptr<Component> component) {
    if (component == nullptr) {
        return nullptr;
    }

    Component* raw = component.get();
    raw->m_owner   = this;
    m_components.push_back(std::move(component));

    // OnAttach AFTER the component is in the list and its owner is set. A
    // system that immediately calls back into the entity - and the sprite
    // system does, for the transform - must find a fully formed object.
    raw->OnAttach();
    return raw;
}

Component* Entity::FindComponent(StringId typeId) {
    for (const std::unique_ptr<Component>& component : m_components) {
        if (component != nullptr && component->TypeId() == typeId) {
            return component.get();
        }
    }
    return nullptr;   // a normal question with a normal answer
}

const Component* Entity::FindComponent(StringId typeId) const {
    return const_cast<Entity*>(this)->FindComponent(typeId);
}

bool Entity::RemoveComponent(StringId typeId) {
    const auto it = std::find_if(m_components.begin(), m_components.end(),
                                 [typeId](const std::unique_ptr<Component>& component) {
                                     return component != nullptr &&
                                            component->TypeId() == typeId;
                                 });
    if (it == m_components.end()) {
        return false;
    }
    (*it)->OnDetach();
    m_components.erase(it);
    return true;
}

Component* Entity::ComponentAt(usize index) {
    return (index < m_components.size()) ? m_components[index].get() : nullptr;
}

void Entity::ForEachComponent(const std::function<void(Component&)>& fn) {
    // Iterating by index rather than by iterator: a callback may attach a
    // component (the Inspector's "Add Component" does), and push_back on a
    // vector invalidates iterators. The size is re-read each step on purpose.
    for (usize i = 0; i < m_components.size(); ++i) {
        if (m_components[i] != nullptr) {
            fn(*m_components[i]);
        }
    }
}

Transform2D& Entity::Transform() {
    // Every entity has one. Created on demand rather than in the constructor
    // so that the component list order still reflects the data file, and
    // guaranteed non-null so no system needs a null check.
    TransformComponent* component = Find<TransformComponent>();
    if (component == nullptr) {
        component = static_cast<TransformComponent*>(
            AddComponent(std::make_unique<TransformComponent>()));
    }
    return component->Transform();
}

const Transform2D& Entity::Transform() const {
    return const_cast<Entity*>(this)->Transform();
}

} // namespace eng
