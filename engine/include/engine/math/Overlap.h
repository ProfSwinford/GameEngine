#pragma once

// ============================================================================
//  Overlap.h - the shape maths. "Are these two things touching, and if so,
//  which way do I push to separate them?"
//
//  Everything here is a PURE FUNCTION: it reads its arguments, returns an
//  answer, and changes nothing anywhere. The same inputs always give the same
//  result. That is what lets physics/Collider.h build components, layers,
//  triggers and events on top of these without ever needing to change this
//  file.
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
//
//  ==========================================================================
//  THE SHAPES, AND THE ONE THAT MATTERS MOST
//
//    AABB        an Axis-Aligned Bounding Box - a rectangle that is never
//                rotated. Still the fastest test there is, and still what the
//                broad phase and the camera use.
//    Circle      a centre and a radius.
//    OBB         an Oriented Bounding Box - a rectangle that CAN be rotated.
//    Capsule     a line segment with a radius; a rectangle with round ends.
//    Segment     a line segment with no thickness.
//
//    ConvexShape ALL OF THE ABOVE, in one representation.
//
//  ConvexShape is the important one. It is up to eight points plus a radius,
//  which is enough to describe every shape in the list:
//
//    circle   = 1 point,  radius r      capsule = 2 points, radius r
//    segment  = 2 points, radius 0      box     = 4 points, radius 0
//    polygon  = n points, radius 0      rotated box = 4 rotated points
//
//  That matters because collision between two arbitrary shapes is normally a
//  table of special cases - box/box, box/circle, circle/capsule, and so on,
//  growing as the square of the number of shapes. Converting everything to
//  one representation first means there is ONE collision routine, and adding
//  a sixth shape costs a converter rather than five new intersection tests.
//
//  The cost is honest and worth stating: a dedicated box-versus-box test
//  would be faster than the general one. The broad phase below is what pays
//  that back, by making sure the general test only ever runs on pairs that
//  are already close.
// ============================================================================

#include <engine/math/Vec2.h>

namespace eng {

// ---------------------------------------------------------------------------
//  AABB - a rectangle that is never rotated.
//
//  It stores two corners rather than a centre and a size because the overlap
//  test is then four straight comparisons with no arithmetic at all.
// ---------------------------------------------------------------------------
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

    // Grows the box outward by `amount` on every side. The broad phase uses
    // this to give fast-moving things a margin.
    AABB Expanded(float amount) const;
};

struct Circle {
    Vec2  center;
    float radius = 0.0f;
};

// An Oriented Bounding Box: a rectangle that can be rotated. `rotation` is in
// radians, anticlockwise, matching Mat3::Rotation and Transform2D.
struct OBB {
    Vec2  center;
    Vec2  halfExtents;
    float rotation = 0.0f;
};

// A line segment with thickness - the shape a capsule collider has. Described
// by the two ends of its CORE SEGMENT plus a radius, which is the same way
// ConvexShape describes it, so the conversion is free.
struct Capsule {
    Vec2  a;
    Vec2  b;
    float radius = 0.0f;
};

// A line segment with no thickness.
struct Segment {
    Vec2 a;
    Vec2 b;
};

// A ray for queries: where it starts, which way it points, how far it reaches.
//
// `direction` is expected to be a UNIT vector, because `distance` is then
// measured in world units and a hit's distance means what it says. Normalize
// it at the point where the query is written rather than inside every test.
struct Ray {
    Vec2  origin;
    Vec2  direction{1.0f, 0.0f};
    float distance = 1.0f;
};

// ---------------------------------------------------------------------------
//  ConvexShape - every shape above, in one representation.
//
//  The convex hull of up to eight points, grown outward by `radius` in every
//  direction. See the note at the top of this file for why one representation
//  rather than a table of special cases.
//
//  THE POINTS ARE IN WORLD SPACE and must be in order around the shape -
//  anticlockwise - because the edge normals are worked out by walking from one
//  point to the next. A shuffled list describes a different shape, and will
//  not report an error; it will simply collide strangely.
// ---------------------------------------------------------------------------
struct ConvexShape {
    // Eight is enough for a rotated box, a hexagon or an octagon, and keeps
    // the whole shape a fixed-size value that can be copied and returned
    // without allocating. A polygon collider with more points than this is
    // reported and trimmed rather than silently losing its tail.
    static constexpr int kMaxPoints = 8;

    Vec2  points[kMaxPoints]{};
    int   count  = 0;
    float radius = 0.0f;

    // The upright box that contains this shape, radius included.
    AABB Bounds() const;

    bool IsValid() const { return count > 0 && count <= kMaxPoints; }
};

// ---------------------------------------------------------------------------
//  What a collision IS, once two shapes are found to be touching.
//
//  This is the engine's ContactPoint2D. Three numbers turn "they are touching"
//  into something a physics step can act on:
//
//    normal      which way to push, as a unit vector. ALWAYS POINTS FROM A
//                TOWARD B - so A separates by moving along -normal, and B by
//                moving along +normal. Getting this backwards pulls things
//                together instead of apart, which looks like magnetism and is
//                the classic first bug in a physics engine.
//    separation  how far apart they are. NEGATIVE when overlapping, and the
//                size of it is how deep. Zero means exactly touching.
//    point       roughly where the contact is, for drawing and for torque.
// ---------------------------------------------------------------------------
struct Contact {
    bool  touching   = false;
    Vec2  normal{};
    float separation = 0.0f;
    Vec2  point{};
};

// What a ray found.
//
// `fraction` is the hit distance as a share of the ray's length, from 0 at the
// origin to 1 at the far end. It is what lets a caller compare hits from rays
// of different lengths, and what makes "the nearest hit" one comparison.
struct RaycastHit {
    bool  hit      = false;
    Vec2  point{};
    Vec2  normal{};
    float distance = 0.0f;
    float fraction = 0.0f;
};

// ---------------------------------------------------------------------------
//  Turning a specific shape into the general one
// ---------------------------------------------------------------------------
ConvexShape ToConvex(const AABB& box);
ConvexShape ToConvex(const Circle& circle);
ConvexShape ToConvex(const OBB& box);
ConvexShape ToConvex(const Capsule& capsule);
ConvexShape ToConvex(const Segment& segment);

// Points must already be in world space and wound anticlockwise. More than
// kMaxPoints are dropped - the caller is expected to have reported that.
ConvexShape ToConvex(const Vec2* points, int count, float radius = 0.0f);

// ---------------------------------------------------------------------------
//  The general collision routine, and the cheap question
// ---------------------------------------------------------------------------

// Full answer: touching, which way, how deep, roughly where.
Contact Collide(const ConvexShape& a, const ConvexShape& b);

// Just the yes or no. Cheaper only in that it stops at the first separating
// axis; prefer Collide when the answer is going to be acted on.
bool Overlaps(const ConvexShape& a, const ConvexShape& b);

// ---------------------------------------------------------------------------
//  The fast paths, kept because the broad phase and the camera lean on them
// ---------------------------------------------------------------------------
bool Overlaps(const AABB& a, const AABB& b);
bool Overlaps(const Circle& a, const Circle& b);
bool Overlaps(const AABB& box, const Circle& circle);
bool Overlaps(const Circle& circle, const AABB& box);

bool Contains(const AABB& box, Vec2 point);
bool Contains(const Circle& circle, Vec2 point);
bool Contains(const ConvexShape& shape, Vec2 point);

// The point on (or inside) the box that is nearest to `point`.
//
// This is the function that makes box-versus-circle correct. The tempting
// shortcut is to put a box around the circle and compare the two boxes, but
// that reports a hit when a circle is near a box CORNER while still being too
// far away to touch it. Measuring to the closest point instead has no such
// blind spot.
Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point);

// The same question for the general shape, radius included. This is the
// engine's Collider2D.ClosestPoint.
Vec2 ClosestPoint(const ConvexShape& shape, Vec2 point);

Vec2 ClosestPointOnSegment(const Segment& segment, Vec2 point);

// The nearest pair of points on two segments. Used for capsule-versus-capsule
// and written out separately because the degenerate cases - parallel segments,
// a segment of zero length - are easy to get wrong and worth having in one
// place.
void ClosestPointsBetweenSegments(const Segment& first, const Segment& second,
                                  Vec2& outOnFirst, Vec2& outOnSecond);

// ---------------------------------------------------------------------------
//  Rays
// ---------------------------------------------------------------------------

// Shoots `ray` at `shape`. Returns false and leaves `outHit` untouched when
// it misses.
//
// A ray that STARTS INSIDE the shape reports a hit at distance 0 with the
// normal facing back along the ray. The alternative - reporting a miss - is
// defensible, but it makes "am I standing in lava?" a question a raycast
// cannot answer, and that is the question people ask.
bool Raycast(const Ray& ray, const ConvexShape& shape, RaycastHit& outHit);

bool Raycast(const Ray& ray, const AABB& box, RaycastHit& outHit);

} // namespace eng
