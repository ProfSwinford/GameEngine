// ============================================================================
//  Entities.cpp - entity ids, the entities themselves, and the create/destroy queue.
//  See Entities.h for what each piece is for.
// ============================================================================

#include <engine/modules/Components.h>
#include <engine/modules/Diagnostics.h>
#include <engine/modules/Entities.h>
#include <engine/modules/Scene.h>

#include <algorithm>
#include <set>
#include <vector>

// ============================================================================
//  Entity.cpp - entities and their components. See Entity.h for the
//  destruction order this file implements.
// ============================================================================



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

Component* Entity::ComponentAt(std::size_t index) {
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

const Transform2D& Entity::Transform() const {
    // This one genuinely needs the cast, and it is the only place in the engine
    // that does. The version above CREATES the transform if it is missing, and
    // creating something is a change - which a const function is not allowed to
    // make. The alternative would be for this to return a pointer that can be
    // null, and then every single use of Transform() would need a null check.
    return const_cast<Entity*>(this)->Transform();
}

} // namespace eng


// ============================================================================
//  DeferredOps.cpp - the spawn and destroy queues. See DeferredOps.h for the
//  four rules this file implements.
// ============================================================================



namespace eng {
namespace {

std::vector<EntityId> g_destroys;

// The same set of ids as g_destroys, kept separately so that "is this one
// already queued?" is a quick lookup rather than a search through the whole
// list. std::set can do that because EntityId knows how to compare itself -
// see the <=> line in EntityId.h.
std::set<EntityId> g_pendingDestroy;

} // namespace

void DeferredOps::QueueDestroy(EntityId id) {
    if (id.IsNull()) {
        return;
    }
    // RULE 3: queueing the same destroy twice is ignored rather than treated
    // as a mistake. insert() returns a pair whose .second says whether
    // anything was actually added, which makes this a one-line check.
    if (!g_pendingDestroy.insert(id).second) {
        return;
    }
    g_destroys.push_back(id);
}

bool DeferredOps::IsPendingDestroy(EntityId id) {
    return !id.IsNull() && g_pendingDestroy.contains(id);
}

void DeferredOps::Apply(Scene& scene) {
    // RULE 4: take the queue away and drain the copy ONCE. Anything queued
    // while draining lands in the now-empty original and happens next frame.
    // swap() is used because it hands over the contents without copying them -
    // it just exchanges what the two vectors point at.
    std::vector<EntityId> destroys;
    destroys.swap(g_destroys);

    for (const EntityId id : destroys) {
        // Checked again: something else may have destroyed it between the
        // queueing and now. Being able to ask that question at all is exactly
        // what the generation number in EntityId is for.
        if (scene.IsValid(id)) {
            scene.DestroyEntityImmediate(id);
        }
    }

    g_pendingDestroy.clear();
}

void DeferredOps::Clear() {
    g_destroys.clear();
    g_pendingDestroy.clear();
}

std::size_t DeferredOps::PendingDestroyCount() { return g_destroys.size(); }

} // namespace eng
