#pragma once

// =============================================================================
//  WEEK 6 - 2D collision primitives, MATH ONLY.
//
//  These are PURE FUNCTIONS. No state, no side effects, no engine dependency
//  beyond Vec2. Given the same inputs they return the same answer, forever.
//
//  That property is why Week 10 builds an entire collision SYSTEM -
//  components, layers, masks, events - on top of them without touching this
//  file. Week 10 wrote no new intersection math, which was the point.
//
// =============================================================================
//  *** THE DECISION, RECORDED BEFORE ANY CODE WAS WRITTEN: ***
//
//      Touching counts as overlap:  YES.
//
//  Two boxes sharing exactly one edge overlap. Two circles touching at exactly
//  one point overlap. A point exactly on a boundary is Contained.
//
//  There is no universally right answer, only a consistent one. This engine
//  picked "yes" because every comparison then reads as <= / >=, so there is
//  exactly one relational operator to keep straight instead of a mixture, and
//  because a trigger volume that fails to fire when the player is exactly on
//  its edge is a bug report waiting to happen.
//
//  EVERY function below agrees with this, and tests/src/test_overlap.cpp pins
//  it down in all four shape combinations plus Contains. If they ever
//  disagree, Week 10 produces collision events that fire on one axis and not
//  the other, and it takes a long time to believe that is what is happening.
// =============================================================================

#include <engine/math/Vec2.h>

namespace eng {

struct AABB {
    Vec2 min;
    Vec2 max;

    // INVARIANT: min <= max on both axes. ASSUMED, not enforced - these are
    // pure functions on plain data and a constructor that validated would make
    // them not-plain-data. Every function here is written so that a degenerate
    // box (min > max) simply reports no overlap rather than doing something
    // undefined, and FromCenterHalfExtents cannot produce one for a
    // non-negative half-extent.
    //
    // A ZERO-SIZE box (min == max) is legal and is a point. The provided test
    // requires that a zero-size box inside a larger one overlaps it, which
    // follows directly from "touching counts".
    static constexpr AABB FromCenterHalfExtents(Vec2 center, Vec2 halfExtents) {
        return AABB{Vec2{center.x - halfExtents.x, center.y - halfExtents.y},
                    Vec2{center.x + halfExtents.x, center.y + halfExtents.y}};
    }

    static constexpr AABB FromMinMax(Vec2 lo, Vec2 hi) { return AABB{lo, hi}; }

    constexpr Vec2 Center() const {
        return Vec2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    }
    constexpr Vec2 Size() const { return Vec2{max.x - min.x, max.y - min.y}; }
    constexpr Vec2 Extents() const { return Vec2{Size().x * 0.5f, Size().y * 0.5f}; }

    constexpr bool IsValid() const { return min.x <= max.x && min.y <= max.y; }

    // Grows the box to contain a point. Used by the collision system when it
    // takes the axis-aligned bounds of a rotated box.
    void Encapsulate(Vec2 point);
};

struct Circle {
    Vec2 center;
    f32  radius = 0.0f;
};

// The four shape-pair tests. Each has both argument orders so that callers
// never have to remember which way round the overload was declared - the
// provided test checks that order does not matter.
bool Overlaps(const AABB& a, const AABB& b);
bool Overlaps(const Circle& a, const Circle& b);
bool Overlaps(const AABB& box, const Circle& circle);
bool Overlaps(const Circle& circle, const AABB& box);

bool Contains(const AABB& box, Vec2 point);
bool Contains(const Circle& circle, Vec2 point);

// The nearest point on (or inside) a box to an arbitrary point. This is the
// function that makes AABB-vs-Circle correct: comparing bounding boxes instead
// of clamping to this point is the single most common AABB-circle bug, and the
// provided test "a circle near a box corner is not fooled by the bounding box"
// exists to catch exactly it.
Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point);

} // namespace eng
