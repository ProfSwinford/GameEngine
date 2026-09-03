// =============================================================================
//  Overlap.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Overlap.h is the specification; read it first.
//
//  One rule runs through all of it: TOUCHING COUNTS AS OVERLAPPING, in every
//  shape combination.
// =============================================================================

#include <engine/math/Overlap.h>

namespace eng {

// Grows this box just enough to contain the point. Used to build a box around
// a set of corners.
void AABB::Encapsulate(Vec2 /*point*/) {
}

// Are two boxes touching or overlapping?
bool Overlaps(const AABB& /*a*/, const AABB& /*b*/) {
    return false;
}

// Are two circles touching or overlapping?
bool Overlaps(const Circle& /*a*/, const Circle& /*b*/) {
    return false;
}

// Is a box touching or overlapping a circle? The nearest point on the box to
// the circle's centre is what decides it.
bool Overlaps(const AABB& /*box*/, const Circle& /*circle*/) {
    return false;
}

// The same test written the other way round, so callers never have to remember
// which order the arguments go in.
bool Overlaps(const Circle& /*circle*/, const AABB& /*box*/) {
    return false;
}

// Is the point inside the box, or exactly on its edge?
bool Contains(const AABB& /*box*/, Vec2 /*point*/) {
    return false;
}

// Is the point inside the circle, or exactly on its rim?
bool Contains(const Circle& /*circle*/, Vec2 /*point*/) {
    return false;
}

// The point on or inside the box nearest to the given point. This is the one
// piece of geometry the box-versus-circle test is built out of.
Vec2 ClosestPointOnAABB(const AABB& /*box*/, Vec2 /*point*/) {
    return Vec2{};
}

} // namespace eng
