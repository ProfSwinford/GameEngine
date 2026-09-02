// =============================================================================
//  Collider.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. Collider.h explains WHAT each
//  function is for and WHY it exists - read it first.
//
//  NO COLLISION MATHS BELONGS IN THIS FILE. math/Overlap.h already answers
//  "are these two shapes touching?"; this file is about layers, components and
//  events. Writing the maths here instead is the mistake worth not making.
// =============================================================================

#include <engine/physics/Collider.h>

namespace eng {

// ---------------------------------------------------------------------------
//  ColliderComponent - what every collider has in common
// ---------------------------------------------------------------------------

// TODO: is `layer` one of the layers this collider wants to hear about?
//
// A pair is only tested when BOTH sides say yes. "Either side is enough" would
// let the player receive a collision from a wall while the wall received
// nothing, and one-sided events are the kind of bug where the first hour is
// spent not believing it.
bool ColliderComponent::CaresAbout(const std::string& /*layer*/) const { return false; }

// TODO: read "layer" and "collidesWith" out of the scene file.
bool ColliderComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool ColliderComponent::Serialize(Json& /*out*/) const { return true; }

void ColliderComponent::OnAttach() {}
void ColliderComponent::OnDetach() {}

// ---------------------------------------------------------------------------
//  AABBColliderComponent - an axis-aligned box
// ---------------------------------------------------------------------------
bool AABBColliderComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool AABBColliderComponent::Serialize(Json& /*out*/) const { return true; }

// TODO: the box where the entity actually IS this step - its half-extents put
// through the owner's world transform. Returning an empty box is why the Scene
// view draws no collider outlines yet.
AABB AABBColliderComponent::WorldBounds() const { return AABB{}; }

// ---------------------------------------------------------------------------
//  CircleColliderComponent
// ---------------------------------------------------------------------------
bool CircleColliderComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool CircleColliderComponent::Serialize(Json& /*out*/) const { return true; }

Circle CircleColliderComponent::WorldCircle() const { return Circle{}; }

AABB CircleColliderComponent::WorldBounds() const { return AABB{}; }

// ---------------------------------------------------------------------------
//  CollisionSystem - stage 400, AFTER movement at 300
//
//  TODO: compare which pairs are touching this tick against which were
//  touching last tick, and send Enter / Stay / Exit accordingly.
//
//  An EXIT is also owed when one side is DESTROYED. Without it the commonest
//  trigger pattern - open a door on enter, close it on exit - breaks the
//  moment the key is destroyed while still inside the volume, and the door
//  stays open forever.
// ---------------------------------------------------------------------------
void CollisionSystem::Register(ColliderComponent& /*collider*/) {}

void CollisionSystem::Unregister(ColliderComponent& /*collider*/) {}

void CollisionSystem::Clear() {}

void CollisionSystem::Update(float /*deltaSeconds*/) {}

std::size_t CollisionSystem::ColliderCount() { return 0; }

std::size_t CollisionSystem::ActivePairCount() { return 0; }

// TODO: register "AABBColliderComponent" and "CircleColliderComponent" with
// the component factory, so a scene file can ask for them.
void CollisionSystem::RegisterComponentTypes() {}

} // namespace eng
