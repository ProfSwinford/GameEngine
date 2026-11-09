// ============================================================================
//  Transform2D.cpp - the parent/child transform tree declared in Transform2D.h.
//
//  The two rules from the header ("children survive their parent" and "nothing
//  can be its own ancestor") are enforced in ~Transform2D and SetParent
//  respectively.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>

#include <algorithm>
#include <cmath>

namespace eng {

Transform2D::~Transform2D() {
    // Rule 1: hand the children back to the world rather than destroying them
    // or leaving them pointing at memory that is about to disappear.
    DetachChildren();

    // And take this node off its own parent's child list, so the parent is not
    // left holding a pointer to something that no longer exists.
    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
        m_parent = nullptr;
    }
}

void Transform2D::AddChild(Transform2D* child) {
    m_children.push_back(child);
}

void Transform2D::RemoveChild(Transform2D* child) {
    // std::erase removes every matching element from a container, and does
    // nothing when there is no match - so there is no "did we find it?" step to
    // get wrong. A child appears in this list once, so "every match" is one.
    std::erase(m_children, child);
}

void Transform2D::SetParent(Transform2D* parent, bool keepWorldTransform) {
    if (parent == m_parent) {
        return;   // nothing to do
    }

    // Rule 2: refuse to build a loop. Walking up the proposed parent's chain
    // costs almost nothing, and it turns a permanent freeze inside the
    // renderer into a message in the Console at the moment the mistake is made.
    if (parent != nullptr) {
        if (parent == this || parent->IsDescendantOf(this)) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "refused to reparent a transform under itself or one of "
                             "its own children - that would make a loop");
            return;
        }
    }

    // Remember where the object currently looks, before the parent changes.
    const Mat3 worldBefore = keepWorldTransform ? WorldMatrix() : Mat3::Identity();

    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
    }
    m_parent = parent;
    if (m_parent != nullptr) {
        m_parent->AddChild(this);
    }

    if (keepWorldTransform) {
        // Work out what local position/rotation/scale would put the object
        // back exactly where it was: take the world transform it had, and undo
        // the new parent's transform from it.
        const Mat3 parentWorld = (m_parent != nullptr) ? m_parent->WorldMatrix()
                                                       : Mat3::Identity();
        const Mat3 local = worldBefore * parentWorld.Inverse();
        m_position = local.GetTranslation();
        m_rotation = local.GetRotation();
        m_scale    = local.GetScale();
    }
}

void Transform2D::DetachChildren() {
    // The list is COPIED before it is walked. SetParent below calls
    // RemoveChild on this node, which erases from m_children - and modifying a
    // container while looping over it is how you end up reading freed memory.
    // Iterating a copy sidesteps that completely.
    const std::vector<Transform2D*> children = m_children;
    for (Transform2D* child : children) {
        child->SetParent(nullptr, /*keepWorldTransform=*/true);
    }
    m_children.clear();
}

int Transform2D::Depth() const {
    int depth = 0;
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        ++depth;
    }
    return depth;
}

bool Transform2D::IsDescendantOf(const Transform2D* candidate) const {
    if (candidate == nullptr) {
        return false;
    }
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        if (node == candidate) {
            return true;
        }
    }
    return false;
}

Mat3 Transform2D::LocalMatrix() const {
    return Mat3::FromTRS(m_position, m_rotation, m_scale);
}

Mat3 Transform2D::WorldMatrix() const {
    // Start with this node's own transform, then apply each parent in turn
    // going outward. Under this engine's convention (see Mat3.h) "do local,
    // then the parent" is written local * parent, which is the same order it
    // reads in.
    //
    // This walks the whole chain every time it is called rather than caching
    // the answer. That is deliberate: caching means remembering to invalidate
    // the cache every time anything moves, and a stale transform is a much
    // harder bug than a slightly slower one. Scenes here are small enough that
    // it does not matter.
    Mat3 result = LocalMatrix();
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        result = result * node->LocalMatrix();
    }
    return result;
}

Vec2 Transform2D::WorldPosition() const {
    return WorldMatrix().GetTranslation();
}

float Transform2D::WorldRotation() const {
    // Rotations simply add up the chain, so there is no need to build a matrix
    // and pull the angle back out of it.
    float total = m_rotation;
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        total += node->m_rotation;
    }
    return total;
}

Vec2 Transform2D::WorldScale() const {
    return WorldMatrix().GetScale();
}

void Transform2D::SetWorldPosition(Vec2 world) {
    if (m_parent == nullptr) {
        // With no parent, local and world are the same thing.
        m_position = world;
        return;
    }
    // With a parent, undo the parent's transform to find the local position
    // that lands on the requested world position.
    m_position = m_parent->WorldMatrix().Inverse().TransformPoint(world);
}

void Transform2D::TranslateWorld(Vec2 delta) {
    if (m_parent == nullptr) {
        // With no parent, local and world are the same thing.
        m_position += delta;
        return;
    }

    const Mat3 parentWorld = m_parent->WorldMatrix();

    // ASKED BEFORE INVERTING, so a flattened parent costs a comparison
    // instead of a warning. See the note on the declaration.
    if (std::fabs(parentWorld.Determinant()) < 1e-12f) {
        m_position += delta;
        return;
    }

    // A VECTOR and not a point: the parent's translation is the same before
    // and after the move, so it cancels and only the 2x2 part matters.
    m_position += parentWorld.Inverse().TransformVector(delta);
}

void Transform2D::SetWorldRotation(float radians) {
    if (m_parent == nullptr) {
        m_rotation = radians;
        return;
    }
    // Rotations add up the chain - see WorldRotation above - so undoing the
    // parents is a subtraction rather than a matrix inverse.
    m_rotation = radians - m_parent->WorldRotation();
}

void Transform2D::SetWorldScale(Vec2 world) {
    if (m_parent == nullptr) {
        m_scale = world;
        return;
    }

    const Vec2 parentScale = m_parent->WorldScale();

    // ======================================================================
    //  A PARENT SCALED TO ZERO ON AN AXIS HAS NO ANSWER, so that axis is left
    //  alone rather than divided.
    //
    //  The division would produce an infinity, which then multiplies into
    //  every matrix below this node and turns every world position in the
    //  subtree into a NaN. Nothing reports that: the objects simply stop
    //  being drawn, and they stop being drawn for as long as the parent is
    //  flat, which is usually one frame of an animation. Keeping the old
    //  local scale means the subtree stays flat while the parent is flat and
    //  comes back correctly when it does - which is what somebody squashing a
    //  sprite actually wants.
    // ======================================================================
    if (parentScale.x != 0.0f) {
        m_scale.x = world.x / parentScale.x;
    }
    if (parentScale.y != 0.0f) {
        m_scale.y = world.y / parentScale.y;
    }
}

Vec2 Transform2D::Right() const {
    const float angle = WorldRotation();
    return Vec2{std::cos(angle), std::sin(angle)};
}

Vec2 Transform2D::Up() const {
    // The perpendicular of Right, anticlockwise - which is what Perpendicular
    // does, and spelling it that way means the handedness is stated in exactly
    // one place in the engine. See Vec2::Perpendicular.
    return Right().Perpendicular();
}

void Transform2D::LookAt(Vec2 worldTarget) {
    const Vec2 toTarget = worldTarget - WorldPosition();

    // NOTHING TO FACE IS NOT AN ERROR AND NOT A SNAP. atan2(0, 0) is 0, so the
    // unguarded version turns to face right the instant the target lands on
    // top of this object - which is exactly when two things touch, so it reads
    // as a glitch at the worst moment. Keeping the current rotation is both
    // the stable answer and the one nobody notices.
    if (toTarget.LengthSquared() <= Vec2::kNormalizeEpsilon) {
        return;
    }
    SetWorldRotation(std::atan2(toTarget.y, toTarget.x));
}

void Transform2D::RotateAround(Vec2 worldPivot, float radians) {
    const Vec2  offset = WorldPosition() - worldPivot;
    const float c      = std::cos(radians);
    const float s      = std::sin(radians);

    // The position first, then the facing. Either order gives the same answer
    // here because the two are independent, but doing the position first means
    // the SetWorldPosition below is not reading a rotation this function has
    // already half-changed - which it would be if this node were its own
    // parent's child, and it is the kind of thing that only breaks once
    // somebody nests two of these.
    SetWorldPosition(worldPivot + Vec2{offset.x * c - offset.y * s,
                                      offset.x * s + offset.y * c});
    Rotate(radians);
}

Transform2D* Transform2D::Root() {
    Transform2D* node = this;
    while (node->m_parent != nullptr) {
        node = node->m_parent;
    }
    return node;
}

const Transform2D* Transform2D::Root() const {
    const Transform2D* node = this;
    while (node->m_parent != nullptr) {
        node = node->m_parent;
    }
    return node;
}

Vec2 Transform2D::LocalToWorldPoint(Vec2 local) const {
    return WorldMatrix().TransformPoint(local);
}

Vec2 Transform2D::WorldToLocalPoint(Vec2 world) const {
    return WorldMatrix().Inverse().TransformPoint(world);
}

Vec2 Transform2D::LocalToWorldVector(Vec2 local) const {
    return WorldMatrix().TransformVector(local);
}

Vec2 Transform2D::WorldToLocalVector(Vec2 world) const {
    return WorldMatrix().Inverse().TransformVector(world);
}

} // namespace eng
