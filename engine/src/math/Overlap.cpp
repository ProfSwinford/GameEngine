// ============================================================================
//  Overlap.cpp - the shape tests declared in Overlap.h.
//
//  Every comparison below uses <= or >= rather than < or >, because this
//  engine has decided that touching counts as overlapping. If you ever change
//  that, change all of them together and update the note in the header.
//
//  <algorithm> is included for std::min, std::max and std::clamp - all three
//  are standard-library functions, and writing them by hand with an if/else
//  only creates somewhere for a typo to hide.
// ============================================================================

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
    // Two upright rectangles miss each other if there is a gap on EITHER axis.
    // So instead of proving they overlap, prove they cannot: if one box ends
    // before the other begins on x, or on y, there is no contact.
    if (a.max.x < b.min.x || b.max.x < a.min.x) { return false; }
    if (a.max.y < b.min.y || b.max.y < a.min.y) { return false; }
    return true;
}

bool Overlaps(const Circle& a, const Circle& b) {
    // Two circles touch when the distance between their centres is no more
    // than the sum of their radii.
    const float reach = a.radius + b.radius;

    // Both sides are squared so that no square root is needed. Squaring does
    // not change which of two non-negative numbers is larger, so the answer is
    // identical and the work is less.
    return DistanceSquared(a.center, b.center) <= reach * reach;
}

Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point) {
    // std::clamp(v, lo, hi) returns v pinned into the range [lo, hi]. Doing
    // that on each axis independently is exactly "the nearest point in the
    // rectangle", and it needs no branches or special cases.
    return Vec2{std::clamp(point.x, box.min.x, box.max.x),
                std::clamp(point.y, box.min.y, box.max.y)};
}

bool Overlaps(const AABB& box, const Circle& circle) {
    // Find the point of the box nearest the circle's centre, then ask whether
    // that point is within one radius.
    //
    // This one expression handles all three situations without any branching:
    //   * the centre is off one flat side  -> nearest point is on that side
    //   * the centre is off a corner       -> nearest point is the corner
    //   * the centre is inside the box     -> nearest point IS the centre, so
    //                                         the distance is zero and it
    //                                         always counts as an overlap
    const Vec2 closest = ClosestPointOnAABB(box, circle.center);
    return DistanceSquared(closest, circle.center) <= circle.radius * circle.radius;
}

bool Overlaps(const Circle& circle, const AABB& box) {
    // The same question with the arguments swapped, so calling code never has
    // to remember an order.
    return Overlaps(box, circle);
}

bool Contains(const AABB& box, Vec2 point) {
    return point.x >= box.min.x && point.x <= box.max.x &&
           point.y >= box.min.y && point.y <= box.max.y;
}

bool Contains(const Circle& circle, Vec2 point) {
    return DistanceSquared(circle.center, point) <= circle.radius * circle.radius;
}

} // namespace eng
