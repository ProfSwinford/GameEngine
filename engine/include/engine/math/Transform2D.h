#pragma once

// ============================================================================
//  Transform2D.h - where an object is, how it is turned, and how big it is.
//
//  This is the same idea as Unity's Transform component. Every entity in the
//  engine has exactly one, and it holds three things:
//
//      position   where it sits
//      rotation   which way it faces, in radians
//      scale      how large it is
//
//  PARENTS AND CHILDREN
//  A transform can have a parent. When it does, its position/rotation/scale
//  are measured RELATIVE TO THAT PARENT rather than to the world. Move the
//  parent and the children come along; turn the parent and the children orbit
//  it. This is how a turret stays on a tank, or a wheel stays on a car,
//  without any code having to keep them in sync.
//
//  Because of that there are two versions of every question:
//      LocalPosition()  - where am I relative to my parent?
//      WorldPosition()  - where am I actually, in the world?
//
//  TWO RULES THIS CLASS ENFORCES
//
//  1. WHEN A PARENT IS DESTROYED, ITS CHILDREN ARE NOT.
//     They are handed back to the world, keeping the position they were
//     already visibly at. Destroying them instead would mean this class owned
//     their lifetime, and their lifetime already belongs to the entity that
//     holds them. Moving them would make debris jump across the screen the
//     moment the ship it came from was deleted.
//
//  2. NOTHING CAN BE ITS OWN ANCESTOR.
//     Asking for a world position walks up the parent chain, so a loop in that
//     chain would walk forever and freeze the game with no error message.
//     SetParent checks for it and refuses.
// ============================================================================

#include <engine/math/Mat3.h>

#include <vector>

namespace eng {

class Transform2D {
public:
    Transform2D() = default;
    ~Transform2D();

    // Copying is switched off with `= delete`, which makes any attempt to copy
    // a compiler error instead of a runtime surprise.
    //
    // The reason: a Transform2D is a node in a tree. If you copied one, would
    // the copy have the same parent? The same children? Would the children now
    // have two parents? There is no answer that is not surprising, so the type
    // simply refuses to express the question.
    Transform2D(const Transform2D&)            = delete;
    Transform2D& operator=(const Transform2D&) = delete;

    // ---- relative to the parent ------------------------------------------
    Vec2  LocalPosition() const { return m_position; }
    float LocalRotation() const { return m_rotation; }   // radians, anticlockwise
    Vec2  LocalScale()    const { return m_scale; }

    void SetLocalPosition(Vec2 position) { m_position = position; }
    void SetLocalRotation(float radians) { m_rotation = radians; }
    void SetLocalScale(Vec2 scale)       { m_scale = scale; }

    void Translate(Vec2 delta)  { m_position += delta; }
    void Rotate(float radians)  { m_rotation += radians; }

    // ---- the tree ---------------------------------------------------------
    Transform2D*                     Parent()   const { return m_parent; }
    const std::vector<Transform2D*>& Children() const { return m_children; }

    // Unity's childCount / GetChild(i) / root. ChildAt returns null for an
    // index past the end rather than reading off the end of the vector,
    // because "how many children does this have" and "give me the third one"
    // get asked by loops whose bound somebody got wrong.
    std::size_t  ChildCount() const { return m_children.size(); }
    Transform2D* ChildAt(std::size_t index) const {
        return index < m_children.size() ? m_children[index] : nullptr;
    }

    // The top of this chain - itself, when it has no parent.
    Transform2D*       Root();
    const Transform2D* Root() const;

    // Attaches this transform to a new parent.
    //
    // `keepWorldTransform` decides what happens on screen. With it false, the
    // object keeps its local numbers and therefore jumps to wherever those
    // numbers mean under the new parent. With it true, the local numbers are
    // recomputed so the object does not appear to move at all - which is what
    // dragging something onto a new parent in the Hierarchy should do.
    void SetParent(Transform2D* parent, bool keepWorldTransform = false);

    // Hands every child back to the world, keeping them where they look.
    void DetachChildren();

    // How many parents there are above this one. A transform with no parent
    // has depth 0.
    int  Depth() const;
    bool IsDescendantOf(const Transform2D* candidate) const;

    // ---- matrices ---------------------------------------------------------
    // LocalMatrix turns this node's position/rotation/scale into a matrix.
    // WorldMatrix does the same but also folds in every parent above it.
    Mat3 LocalMatrix() const;
    Mat3 WorldMatrix() const;

    Vec2  WorldPosition() const;
    float WorldRotation() const;
    Vec2  WorldScale() const;

    // Moves the object to a world position, working out for you what local
    // position that corresponds to under the current parent.
    void SetWorldPosition(Vec2 world);

    // The other two, which were missing - and their absence was not symmetric
    // with SetWorldPosition, it was just a gap. "Point this at the player" is
    // a world-space statement, and under a rotating parent there is no way to
    // say it in local terms without doing the subtraction by hand.
    //
    void SetWorldRotation(float radians);

    // ---- and world scale, which is the one with a caveat ------------------
    //
    // WORLD SCALE IS NOT ALWAYS EXPRESSIBLE, and this is a property of
    // hierarchies rather than of this engine - Unity calls its version
    // `lossyScale` and the "lossy" is the same admission.
    //
    // When a NON-UNIFORM SCALE sits ABOVE A ROTATION in the chain - including
    // this node's own rotation - the combined transform contains SHEAR: the
    // axes stop being at right angles, and no (x, y) pair describes the
    // result. The numbers WorldScale() returns are the LENGTHS OF THE COMPOSED
    // AXES, which is the most useful summary available and is not a value this
    // function can invert. In that case this gets as close as a
    // component-wise division can, and reading WorldScale() back afterwards
    // will not give exactly what was asked for.
    //
    // IT IS EXACT whenever every scale above this node is uniform, and
    // whenever nothing between here and the unevenly scaled ancestor is
    // rotated. Which covers no parent at all, a parent that is only moved, a
    // parent that is only rotated, and a stack of evenly scaled parents - so
    // in practice, everything except deliberately squashing a rotated rig.
    //
    // A PARENT FLAT ON AN AXIS LEAVES THAT AXIS ALONE. There is no local scale
    // that produces the asked-for world scale through a zero, and dividing
    // would produce an infinity that turns every world position in the
    // subtree into a NaN. NaN positions are silent - the sprites simply stop
    // being drawn, for as long as the parent is flat, which is usually one
    // frame of an animation.
    void SetWorldScale(Vec2 world);

    // ---- which way it faces -----------------------------------------------
    //
    // Unity's transform.right / transform.up, in 2D. Right is the direction
    // the object's own +X axis points in the world, which for a sprite is
    // "forwards" - this is what a bullet should be fired along and what a
    // car should be driven along.
    //
    // NORMALISED, so a scaled object still reports a unit direction. The
    // alternative - scaling the direction too - means a bullet from a
    // half-size gun travels at half speed, which nobody ever wants and which
    // reads as a physics bug.
    Vec2 Right() const;
    Vec2 Up() const;

    // Turn to face a world point. The angle is measured so that Right() ends
    // up pointing at `worldTarget`.
    //
    // A TARGET AT EXACTLY THIS OBJECT'S POSITION LEAVES THE ROTATION ALONE.
    // There is no direction to face, and atan2(0,0) is zero - so the
    // unguarded version snaps to facing right, which looks like a glitch
    // every time two things touch.
    void LookAt(Vec2 worldTarget);

    // Turn around a world point rather than around this object's own origin.
    // An orbit, a door on its hinge, a camera swinging about a target.
    //
    // Both halves, because turning without moving is already Rotate() and
    // this is specifically the one that does both.
    void RotateAround(Vec2 worldPivot, float radians);

    // Converting a point or a direction between this object's own frame of
    // reference and the world's. A POINT is affected by the object's position;
    // a DIRECTION is not. See Mat3.h for why those are separate.
    Vec2 LocalToWorldPoint(Vec2 local) const;
    Vec2 WorldToLocalPoint(Vec2 world) const;
    Vec2 LocalToWorldVector(Vec2 local) const;
    Vec2 WorldToLocalVector(Vec2 world) const;

private:
    void AddChild(Transform2D* child);
    void RemoveChild(Transform2D* child);

    Vec2  m_position{0.0f, 0.0f};
    float m_rotation = 0.0f;
    Vec2  m_scale{1.0f, 1.0f};

    Transform2D*              m_parent = nullptr;
    std::vector<Transform2D*> m_children;
};

} // namespace eng
