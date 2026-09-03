// =============================================================================
//  Transform2D.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. Transform2D.h is the specification; read it
//  before filling one in.
// =============================================================================

#include <engine/math/Transform2D.h>

namespace eng {

// A transform being destroyed hands its children back to the world, so none of
// them is left pointing at something that no longer exists.
Transform2D::~Transform2D() {
}

// Records a child in this transform's list. Called by SetParent, not directly.
void Transform2D::AddChild(Transform2D* /*child*/) {
}

// Takes a child back out of this transform's list.
void Transform2D::RemoveChild(Transform2D* /*child*/) {
}

// Attaches this transform to a parent, so it moves when the parent moves.
// keepWorldTransform decides whether it stays where it visibly is, or keeps its
// local numbers and jumps.
void Transform2D::SetParent(Transform2D* /*parent*/, bool /*keepWorldTransform*/) {
}

// Releases every child to the world, leaving each one where it visibly is.
void Transform2D::DetachChildren() {
}

// How many parents there are above this one. Zero means it is at the top.
int Transform2D::Depth() const {
    return 0;
}

// Is this transform somewhere below the candidate? Checked before parenting, so
// a transform cannot be made its own grandparent.
bool Transform2D::IsDescendantOf(const Transform2D* /*candidate*/) const {
    return false;
}

// This transform's own position, rotation and scale, as one matrix - ignoring
// any parent.
Mat3 Transform2D::LocalMatrix() const {
    return Mat3::Identity();
}

// This transform combined with every parent above it. This is what puts a moon
// in orbit around a planet that is itself orbiting a sun.
Mat3 Transform2D::WorldMatrix() const {
    return Mat3::Identity();
}

// Where this transform actually is in the world, parents included.
Vec2 Transform2D::WorldPosition() const {
    return Vec2{};
}

// Which way it is actually facing in the world, parents included.
float Transform2D::WorldRotation() const {
    return 0.0f;
}

// How big it actually is in the world, parents included.
Vec2 Transform2D::WorldScale() const {
    return Vec2{1.0f, 1.0f};
}

// Puts this transform at a world position, working out the local position that
// produces it under whatever parent it has.
void Transform2D::SetWorldPosition(Vec2 /*world*/) {
}

// Converts a point from this transform's own space into world space.
Vec2 Transform2D::LocalToWorldPoint(Vec2 /*local*/) const {
    return Vec2{};
}

// Converts a world point into this transform's own space.
Vec2 Transform2D::WorldToLocalPoint(Vec2 /*world*/) const {
    return Vec2{};
}

// Converts a direction out of this transform's space. Unlike a point, a
// direction ignores the move part.
Vec2 Transform2D::LocalToWorldVector(Vec2 /*local*/) const {
    return Vec2{};
}

// Converts a world direction into this transform's space.
Vec2 Transform2D::WorldToLocalVector(Vec2 /*world*/) const {
    return Vec2{};
}

} // namespace eng
