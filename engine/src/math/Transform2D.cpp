// =============================================================================
//  Transform2D.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/math/Transform2D.h>

namespace eng {

// TODO: a transform being destroyed must hand its children back to the world,
// or they are left pointing at something that no longer exists.
Transform2D::~Transform2D() {}

void Transform2D::AddChild(Transform2D* /*child*/) {}
void Transform2D::RemoveChild(Transform2D* /*child*/) {}

// TODO: the parenting that makes a moon orbit a planet that orbits a sun.
void Transform2D::SetParent(Transform2D* /*parent*/, bool /*keepWorldTransform*/) {}
void Transform2D::DetachChildren() {}

int  Transform2D::Depth() const { return 0; }
bool Transform2D::IsDescendantOf(const Transform2D* /*candidate*/) const { return false; }

// TODO: local is this transform on its own; world is it combined with every
// parent above it.
Mat3 Transform2D::LocalMatrix() const { return Mat3::Identity(); }
Mat3 Transform2D::WorldMatrix() const { return Mat3::Identity(); }

Vec2  Transform2D::WorldPosition() const { return m_position; }
float Transform2D::WorldRotation() const { return m_rotation; }
Vec2  Transform2D::WorldScale() const    { return m_scale; }

void Transform2D::SetWorldPosition(Vec2 world) { m_position = world; }

Vec2 Transform2D::LocalToWorldPoint(Vec2 local) const   { return local; }
Vec2 Transform2D::WorldToLocalPoint(Vec2 world) const   { return world; }
Vec2 Transform2D::LocalToWorldVector(Vec2 local) const  { return local; }
Vec2 Transform2D::WorldToLocalVector(Vec2 world) const  { return world; }

} // namespace eng
