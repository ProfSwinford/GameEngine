// =============================================================================
//  Entity.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. Entity.h explains WHAT each
//  function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>

namespace eng {

// TODO: destroying an entity has a strict ORDER - OnDetach on every component
// in reverse, then the components themselves, then hand the children back to
// the world. Entity.h spells out why step 1 has to happen before step 2.
Entity::~Entity() {}

void Entity::DestroyInternal() {}

void Entity::SetName(std::string_view name) { m_name = std::string(name); }

// TODO: build a component by type name and attach it. This is where OnAttach
// gets called - not the constructor. See Component.h for why.
Component* Entity::AddComponent(std::string_view /*typeName*/) { return nullptr; }

Component* Entity::AddComponent(std::unique_ptr<Component> /*component*/) {
    return nullptr;
}

Component* Entity::FindComponent(std::string_view /*typeName*/) { return nullptr; }

const Component* Entity::FindComponent(std::string_view /*typeName*/) const {
    return nullptr;
}

bool Entity::RemoveComponent(std::string_view /*typeName*/) { return false; }

Component* Entity::ComponentAt(std::size_t /*index*/) { return nullptr; }

void Entity::ForEachComponent(const std::function<void(Component&)>& /*fn*/) {}

// Every entity is meant to have a TransformComponent, which is why these
// return a reference rather than a pointer. Until AddComponent works there is
// nothing real to return, so they hand back one shared dummy: writing to it
// does nothing, which is odd, but it is better than crashing before the
// lesson that fixes it.
namespace {
Transform2D& FallbackTransform() {
    static Transform2D dummy;
    return dummy;
}
} // namespace

Transform2D& Entity::Transform() { return FallbackTransform(); }

const Transform2D& Entity::Transform() const { return FallbackTransform(); }

} // namespace eng
