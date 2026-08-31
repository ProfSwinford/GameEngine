// WEEK 6 - overlap tests. Pure functions; see Overlap.h for the
// touching-counts-as-overlap decision that every one of them honours.

#include <engine/math/Overlap.h>

#include <algorithm>

namespace eng {

void AABB::Encapsulate(Vec2 point) {
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
}

bool Overlaps(const AABB& a, const AABB& b) {
    // Separating axis, in its simplest possible form. If either axis
    // separates them, they cannot overlap - that is the whole test, and the
    // provided "separation on one axis is enough" case checks it.
    //
    // The comparisons are <= and >= because TOUCHING COUNTS. Change these to
    // < and > and the recorded convention has to change with them, along with
    // kTouchingCounts in the test file.
    if (a.max.x < b.min.x || b.max.x < a.min.x) { return false; }
    if (a.max.y < b.min.y || b.max.y < a.min.y) { return false; }
    return true;
}

bool Overlaps(const Circle& a, const Circle& b) {
    const f32 reach = a.radius + b.radius;
    // Squared comparison: no square root, and the ordering is identical.
    // Exactly touching gives distanceSquared == reach*reach, and <= keeps it
    // an overlap.
    return DistanceSquared(a.center, b.center) <= reach * reach;
}

Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point) {
    return Vec2{std::clamp(point.x, box.min.x, box.max.x),
                std::clamp(point.y, box.min.y, box.max.y)};
}

bool Overlaps(const AABB& box, const Circle& circle) {
    // Clamp the circle's centre into the box, then measure. This handles all
    // three configurations with no branching: centre outside on a face,
    // outside near a corner, and inside the box entirely (where the clamped
    // point IS the centre and the distance is zero).
    //
    // Comparing bounding boxes instead would report a false positive for a
    // circle sitting diagonally off a corner - which is the provided test.
    const Vec2 closest = ClosestPointOnAABB(box, circle.center);
    return DistanceSquared(closest, circle.center) <= circle.radius * circle.radius;
}

bool Overlaps(const Circle& circle, const AABB& box) {
    return Overlaps(box, circle);
}

bool Contains(const AABB& box, Vec2 point) {
    // The third place the touching decision shows up: a point exactly on the
    // boundary is contained.
    return point.x >= box.min.x && point.x <= box.max.x &&
           point.y >= box.min.y && point.y <= box.max.y;
}

bool Contains(const Circle& circle, Vec2 point) {
    return DistanceSquared(circle.center, point) <= circle.radius * circle.radius;
}

} // namespace eng
