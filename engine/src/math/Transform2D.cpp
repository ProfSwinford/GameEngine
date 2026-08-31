// WEEK 6 - Transform2D. See the header for the two design answers.

#include <engine/core/Assert.h>
#include <engine/math/Transform2D.h>

#include <algorithm>

namespace eng {

Transform2D::~Transform2D() {
    // Answer 1 from the header, enforced here: children are orphaned to the
    // root keeping their world transform, never destroyed and never left
    // pointing at this node's storage.
    DetachChildren();
    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
        m_parent = nullptr;
    }
}

void Transform2D::AddChild(Transform2D* child) {
    m_children.push_back(child);
}

void Transform2D::RemoveChild(Transform2D* child) {
    const auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        m_children.erase(it);
    }
}

void Transform2D::SetParent(Transform2D* parent, bool keepWorldTransform) {
    if (parent == m_parent) {
        return;
    }

    // Answer 2 from the header: a node may not become its own ancestor. The
    // walk is O(depth), which is nothing, and it turns a hang inside the
    // render pass into a named assert at the moment the mistake is made.
    if (parent != nullptr) {
        ENGINE_ASSERT_MSG(parent != this, "a transform cannot be its own parent");
        ENGINE_ASSERT_MSG(!parent->IsDescendantOf(this),
                          "reparenting would create a cycle in the transform hierarchy");
        if (parent == this || parent->IsDescendantOf(this)) {
            return;   // release build: refuse rather than hang
        }
    }

    const Mat3 worldBefore = keepWorldTransform ? WorldMatrix() : Mat3::Identity();

    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
    }
    m_parent = parent;
    if (m_parent != nullptr) {
        m_parent->AddChild(this);
    }

    if (keepWorldTransform) {
        // local = world * inverse(parentWorld), under v * M.
        const Mat3 parentWorld = (m_parent != nullptr) ? m_parent->WorldMatrix()
                                                       : Mat3::Identity();
        const Mat3 local = worldBefore * parentWorld.Inverse();
        m_position = local.GetTranslation();
        m_rotation = local.GetRotation();
        m_scale    = local.GetScale();
    }
}

void Transform2D::DetachChildren() {
    // Copy first: SetParent mutates m_children while we walk it, which is the
    // iterator-invalidation problem Week 10 formalises, met here in miniature.
    const std::vector<Transform2D*> children = m_children;
    for (Transform2D* child : children) {
        child->SetParent(nullptr, /*keepWorldTransform=*/true);
    }
    m_children.clear();
}

i32 Transform2D::Depth() const {
    i32 depth = 0;
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
    // Naive by design. Local first, then every ancestor outward - which under
    // the row-vector convention is `local * parentWorld`, reading left to
    // right in application order.
    Mat3 result = LocalMatrix();
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        result = result * node->LocalMatrix();
    }
    return result;
}

Vec2 Transform2D::WorldPosition() const {
    return WorldMatrix().GetTranslation();
}

f32 Transform2D::WorldRotation() const {
    f32 total = m_rotation;
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
        m_position = world;
        return;
    }
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
