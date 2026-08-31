#pragma once

// ============================================================================
//  Overlap.h - "are these two shapes touching?", and nothing else.
//
//  Two shapes are supported, which is all a 2D game usually needs:
//    * AABB   - an Axis-Aligned Bounding Box. A rectangle that is never
//               rotated, described by its bottom-left and top-right corners.
//    * Circle - a centre and a radius.
//
//  Everything here is a PURE FUNCTION: it reads its arguments, returns an
//  answer, and changes nothing anywhere. The same inputs always give the same
//  result. That is what lets the collision system in physics/Collider.h build
//  triggers, layers and enter/exit events on top of these without ever needing
//  to change this file.
//
//  ==========================================================================
//  ONE DECISION, APPLIED EVERYWHERE: TOUCHING COUNTS AS OVERLAPPING.
//
//  Two boxes that share exactly one edge overlap. Two circles touching at one
//  point overlap. A point sitting exactly on a boundary is inside.
//
//  Neither answer is more "correct" than the other; what matters is picking
//  one and never wavering. This engine picked yes for two reasons: every
//  comparison below then reads as <= or >=, so there is only one operator to
//  keep straight, and a trigger that refuses to fire when the player is
//  exactly on its edge is a bug report waiting to happen.
//  ==========================================================================
// ============================================================================

#include <engine/math/Vec2.h>

namespace eng {

// A rectangle that is never rotated.
//
// It stores two corners rather than a centre and a size because the overlap
// test is then four straight comparisons with no arithmetic at all.
struct AABB {
    Vec2 min;   // bottom-left
    Vec2 max;   // top-right

    // Building a box from a centre point and its half-width/half-height, which
    // is how a collider on an entity is described.
    static constexpr AABB FromCenterHalfExtents(Vec2 center, Vec2 halfExtents) {
        return AABB{Vec2{center.x - halfExtents.x, center.y - halfExtents.y},
                    Vec2{center.x + halfExtents.x, center.y + halfExtents.y}};
    }

    static constexpr AABB FromMinMax(Vec2 lo, Vec2 hi) { return AABB{lo, hi}; }

    constexpr Vec2 Center() const {
        return Vec2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    }
    constexpr Vec2 Size()    const { return Vec2{max.x - min.x, max.y - min.y}; }
    constexpr Vec2 Extents() const { return Vec2{Size().x * 0.5f, Size().y * 0.5f}; }

    // A box with min above max on either axis is inside-out and cannot contain
    // anything. Nothing forces this to be true - it is simply that every
    // function below reports "no overlap" for such a box rather than doing
    // something unpredictable.
    constexpr bool IsValid() const { return min.x <= max.x && min.y <= max.y; }

    // Grows the box just enough to include `point`. Used when working out the
    // upright box that surrounds a rotated shape.
    void Encapsulate(Vec2 point);
};

struct Circle {
    Vec2  center;
    float radius = 0.0f;
};

// The four shape pairings. Each has both argument orders so that calling code
// never has to remember which way round it was declared.
bool Overlaps(const AABB& a, const AABB& b);
bool Overlaps(const Circle& a, const Circle& b);
bool Overlaps(const AABB& box, const Circle& circle);
bool Overlaps(const Circle& circle, const AABB& box);

bool Contains(const AABB& box, Vec2 point);
bool Contains(const Circle& circle, Vec2 point);

// The point on (or inside) the box that is nearest to `point`.
//
// This is the function that makes box-versus-circle correct. The tempting
// shortcut is to put a box around the circle and compare the two boxes, but
// that reports a hit when a circle is near a box CORNER while still being too
// far away to touch it. Measuring to the closest point instead has no such
// blind spot.
Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point);

} // namespace eng
