// =============================================================================
//  Entity.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Entity.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>

namespace eng {

// Destroys the entity. The order matters: OnDetach on every component first,
// then the components themselves, then the children are handed back to the
// world - see Entity.h for what breaks when those swap round.
Entity::~Entity() {
}

// Does the taking-apart, so that both the destructor and the scene can use the
// same path.
void Entity::DestroyInternal() {
}

// Renames the entity. The scene keeps a name-to-entity table, so this has to
// keep that in step.
void Entity::SetName(std::string_view /*name*/) {
}

// Builds a component from its type name and attaches it - the path a scene file
// takes. Attaching is what calls OnAttach, not the constructor.
Component* Entity::AddComponent(std::string_view /*typeName*/) {
    return nullptr;
}

// Attaches a component you built yourself, for code that does not go through a
// scene file.
Component* Entity::AddComponent(std::unique_ptr<Component> /*component*/) {
    return nullptr;
}

// Finds an attached component by type name, or nullptr when there is none.
Component* Entity::FindComponent(std::string_view /*typeName*/) {
    return nullptr;
}

// The same search, for when you only have a const Entity.
const Component* Entity::FindComponent(std::string_view /*typeName*/) const {
    return nullptr;
}

// Detaches and destroys one component by type name.
bool Entity::RemoveComponent(std::string_view /*typeName*/) {
    return false;
}

// One component by position, so the Inspector can list them in order.
Component* Entity::ComponentAt(std::size_t /*index*/) {
    return nullptr;
}

// Visits every attached component in turn.
void Entity::ForEachComponent(const std::function<void(Component&)>& /*fn*/) {
}

// The entity's position, rotation and scale. Every entity has one, always -
// which is why this returns a reference and no system in the engine has to
// check for a missing transform.
Transform2D& Entity::Transform() {
    static Transform2D fallback;
    return fallback;
}

// The same, for when you only have a const Entity.
const Transform2D& Entity::Transform() const {
    static const Transform2D fallback;
    return fallback;
}

} // namespace eng
