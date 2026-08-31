#pragma once

// =============================================================================
//  WEEK 6 - a node in a transform hierarchy.
//
//  Position, rotation and scale relative to a parent, plus the machinery to
//  ask for a world-space transform. Arbitrary depth; nothing here cares
//  whether the tree is three deep or thirty.
//
//  WEEK 9: this became the payload of TransformComponent - the thing every
//  entity in a scene file has. It is designed as something a DATA FILE
//  constructs, which is why everything is a plain setter with no invariants
//  beyond the cycle check.
//
//  ---------------------------------------------------------------------------
//  THE TWO QUESTIONS THE HEADER ASKED, ANSWERED:
//
//  1. WHAT HAPPENS TO CHILDREN WHEN A PARENT IS DESTROYED?
//
//     They are ORPHANED TO THE ROOT, keeping their WORLD transform. Not
//     destroyed, not silently left pointing at freed memory.
//
//     Reasoning: destroying children makes Transform2D own lifetime, and
//     lifetime here belongs to the Entity that holds the component - two
//     owners of the same object is the bug Week 3 spent a week on. Asserting
//     would make a perfectly ordinary gameplay action ("delete the ship, keep
//     the debris") a programmer error. Preserving the world transform means a
//     child does not teleport when its parent dies, which is what a player
//     would expect to see.
//
//  2. CAN A NODE BE ITS OWN ANCESTOR?
//
//     No. SetParent walks up from the proposed parent and ASSERTS if it meets
//     `this`, then refuses the reparent. Silently accepting a cycle would make
//     WorldMatrix() loop forever, and an infinite loop inside a render pass is
//     a hang with no diagnostic at all.
//
//  ---------------------------------------------------------------------------
//  WORLDMATRIX IS NAIVE ON PURPOSE. It walks to the root and multiplies, every
//  call. That is correct and it is fast enough for all of Phase 1 - the Week 6
//  stretch measured 100 entities at 3 deep at well under a tenth of a
//  millisecond, and the number is in docs/week06-milestone1.md. Caching it is
//  a Phase 2 job, to be done when a scoped timer says so and not before;
//  premature caching here is a reliable way to spend a week debugging stale
//  transforms instead of learning matrices.
// =============================================================================

#include <engine/math/Mat3.h>

#include <vector>

namespace eng {

class Transform2D {
public:
    Transform2D() = default;
    ~Transform2D();

    // Non-copyable: a Transform2D is a NODE IN A TREE, and copying a node
    // raises "does the copy have the same parent? the same children?" with no
    // good answer. Moving is not supported either, because children hold raw
    // back-pointers to their parent and a move would leave them dangling.
    Transform2D(const Transform2D&)            = delete;
    Transform2D& operator=(const Transform2D&) = delete;

    // --- local space ------------------------------------------------------
    Vec2 LocalPosition() const { return m_position; }
    f32  LocalRotation() const { return m_rotation; }   // radians, CCW
    Vec2 LocalScale()    const { return m_scale; }

    void SetLocalPosition(Vec2 position) { m_position = position; }
    void SetLocalRotation(f32 radians)   { m_rotation = radians; }
    void SetLocalScale(Vec2 scale)       { m_scale = scale; }

    void Translate(Vec2 delta)  { m_position += delta; }
    void Rotate(f32 radians)    { m_rotation += radians; }

    // --- hierarchy --------------------------------------------------------
    Transform2D*                            Parent() const { return m_parent; }
    const std::vector<Transform2D*>&        Children() const { return m_children; }

    // Reparents. `keepWorldTransform` recomputes the local values so the node
    // does not visibly move - which is what a scene editor's drag-and-drop
    // wants, and what the "reparenting to an identity parent does not move
    // anything" test checks.
    void SetParent(Transform2D* parent, bool keepWorldTransform = false);
    void DetachChildren();   // orphan to root, preserving world transforms

    // Depth from the root. Root nodes are depth 0.
    i32 Depth() const;
    bool IsDescendantOf(const Transform2D* candidate) const;

    // --- matrices ---------------------------------------------------------
    Mat3 LocalMatrix() const;
    Mat3 WorldMatrix() const;

    Vec2 WorldPosition() const;
    f32  WorldRotation() const;
    Vec2 WorldScale() const;

    void SetWorldPosition(Vec2 world);

    Vec2 LocalToWorldPoint(Vec2 local) const;
    Vec2 WorldToLocalPoint(Vec2 world) const;
    Vec2 LocalToWorldVector(Vec2 local) const;
    Vec2 WorldToLocalVector(Vec2 world) const;

private:
    void AddChild(Transform2D* child);
    void RemoveChild(Transform2D* child);

    Vec2 m_position{0.0f, 0.0f};
    f32  m_rotation = 0.0f;
    Vec2 m_scale{1.0f, 1.0f};

    Transform2D*              m_parent = nullptr;
    std::vector<Transform2D*> m_children;
};

} // namespace eng
