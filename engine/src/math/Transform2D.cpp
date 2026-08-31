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
    // std::find is the standard-library search over any container. It returns
    // an iterator to the match, or end() when there is none - which is why the
    // result has to be checked before erasing.
    const auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        m_children.erase(it);
    }
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
