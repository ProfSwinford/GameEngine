// =============================================================================
//  Collider.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Collider.h is the specification; read it first.
//
//  NO COLLISION MATHS BELONGS IN THIS FILE. math/Overlap.h already answers
//  "are these two shapes touching?"; this file is about layers, components and
//  events. Writing the geometry here instead is the mistake worth not making.
// =============================================================================

#include <engine/physics/Collider.h>

namespace eng {

// Does this collider want to hear about that layer? A pair is only tested when
// BOTH sides say yes, so collision events always arrive in pairs.
bool ColliderComponent::CaresAbout(const std::string& /*layer*/) const {
    return false;
}

// Reads the layer, the collides-with list, the trigger flag and the offset from
// the scene file.
bool ColliderComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes those back out.
bool ColliderComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Adds this collider to the collision system's list.
void ColliderComponent::OnAttach() {
}

// Takes it back out.
void ColliderComponent::OnDetach() {
}

// Reads the box's half-width and half-height from the scene file.
bool AABBColliderComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes them back out.
bool AABBColliderComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Where this box actually is this step: its corners pushed through the owner's
// world transform, wrapped in an upright box. A rotated entity therefore gets a
// slightly larger box, so collisions fire early rather than late.
AABB AABBColliderComponent::WorldBounds() const {
    return AABB{};
}

// Reads the circle's radius from the scene file.
bool CircleColliderComponent::Deserialize(const Json& /*node*/,
                                          std::string& /*outError*/) {
    return false;
}

// Writes it back out.
bool CircleColliderComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Where this circle actually is this step, in world coordinates.
Circle CircleColliderComponent::WorldCircle() const {
    return Circle{};
}

// The upright box that contains this circle, so both shapes can be compared the
// same way.
AABB CircleColliderComponent::WorldBounds() const {
    return AABB{};
}

// Adds a collider to the list the system tests.
void CollisionSystem::Register(ColliderComponent& /*collider*/) {
}

// Takes a collider back out, and reports an Exit for anything it was touching -
// without that, "open a door on enter, close it on exit" leaves the door open
// forever when the key is destroyed inside the volume.
void CollisionSystem::Unregister(ColliderComponent& /*collider*/) {
}

// Forgets every collider and every remembered pair.
void CollisionSystem::Clear() {
}

// Tests every interested pair and works out Enter, Stay and Exit by comparing
// what is touching now against what was touching last step. Runs at stage 400,
// AFTER movement at 300, so shapes are tested where they now are.
void CollisionSystem::Update(float /*deltaSeconds*/) {
}

// How many colliders are attached.
std::size_t CollisionSystem::ColliderCount() {
    return 0;
}

// How many pairs are touching right now - the number the editor's toolbar shows.
std::size_t CollisionSystem::ActivePairCount() {
    return 0;
}

// Tells the component factory that "AABBColliderComponent" and
// "CircleColliderComponent" mean these classes.
void CollisionSystem::RegisterComponentTypes() {
}

} // namespace eng
