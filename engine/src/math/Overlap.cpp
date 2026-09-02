// =============================================================================
//  Overlap.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/math/Overlap.h>

namespace eng {

// TODO: grow this box just enough to contain `point`.
void AABB::Encapsulate(Vec2 /*point*/) {}

// TODO: all of these. Remember the rule from Overlap.h - TOUCHING COUNTS AS
// OVERLAPPING, in every shape combination. Returning false everywhere means
// nothing ever collides, which is exactly what the editor will show you.
bool Overlaps(const AABB& /*a*/, const AABB& /*b*/)          { return false; }
bool Overlaps(const Circle& /*a*/, const Circle& /*b*/)      { return false; }
bool Overlaps(const AABB& /*box*/, const Circle& /*circle*/) { return false; }
bool Overlaps(const Circle& /*circle*/, const AABB& /*box*/) { return false; }

bool Contains(const AABB& /*box*/, Vec2 /*point*/)      { return false; }
bool Contains(const Circle& /*circle*/, Vec2 /*point*/) { return false; }

// TODO: the point on (or in) the box nearest to `point`.
Vec2 ClosestPointOnAABB(const AABB& /*box*/, Vec2 point) { return point; }

} // namespace eng
