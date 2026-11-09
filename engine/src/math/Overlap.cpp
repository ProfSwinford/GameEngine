// ============================================================================
//  Overlap.cpp - the shape tests declared in Overlap.h.
//
//  Every comparison on the fast paths uses <= or >= rather than < or >,
//  because this engine has decided that touching counts as overlapping. If
//  you ever change that, change all of them together and update the note in
//  the header.
//
//  <algorithm> is included for std::min, std::max and std::clamp - all three
//  are standard-library functions, and writing them by hand with an if/else
//  only creates somewhere for a typo to hide.
// ============================================================================

#include <engine/math/Overlap.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eng {
namespace {

// Below this, a vector is treated as having no direction at all. Used when a
// capsule has zero length, when two shape centres sit exactly on top of each
// other, and anywhere else a normalize would otherwise divide by nothing.
constexpr float kTinyLength = 1e-6f;

// ---------------------------------------------------------------------------
//  Projecting a shape onto an axis - the one operation SAT is built from.
//
//  The shadow a shape casts on a line. For a rounded shape the shadow is the
//  core's shadow stretched by the radius at both ends, which is the whole
//  reason one routine can handle circles and polygons together: a circle is a
//  single point whose shadow is a point, widened by r.
// ---------------------------------------------------------------------------
struct Interval {
    float min = 0.0f;
    float max = 0.0f;
};

Interval Project(const ConvexShape& shape, Vec2 axis) {
    float lo = Dot(shape.points[0], axis);
    float hi = lo;
    for (int i = 1; i < shape.count; ++i) {
        const float d = Dot(shape.points[i], axis);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return Interval{lo - shape.radius, hi + shape.radius};
}

// The point of the core furthest along `axis`. Where a contact point comes
// from once the normal is known.
Vec2 Support(const ConvexShape& shape, Vec2 axis) {
    Vec2  best     = shape.points[0];
    float bestDist = Dot(best, axis);
    for (int i = 1; i < shape.count; ++i) {
        const float d = Dot(shape.points[i], axis);
        if (d > bestDist) {
            bestDist = d;
            best     = shape.points[i];
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
//  TWO EXACT RAY PRIMITIVES, which everything rounded is built out of.
//
//  Both answer with the ray parameter at the crossing, or a NEGATIVE number
//  for no crossing - so a caller can compare candidates with one `<` and
//  range-check the winner once. Neither bounds the result by the ray's
//  length; that is the caller's business, because a candidate past the end
//  still has to lose to a nearer one rather than being silently dropped.
// ---------------------------------------------------------------------------

// Four cross products. `u` is how far along the segment the crossing is, and
// it is what makes this a segment rather than the infinite line through it.
float RaySegmentT(const Ray& ray, Vec2 from, Vec2 to) {
    const Vec2  along = to - from;
    const float denom = Cross(ray.direction, along);
    if (std::fabs(denom) < kTinyLength) {
        return -1.0f;   // parallel: no single crossing to report
    }

    const Vec2  toStart = from - ray.origin;
    const float t       = Cross(toStart, along) / denom;
    const float u       = Cross(toStart, ray.direction) / denom;

    if (t < 0.0f || u < 0.0f || u > 1.0f) {
        return -1.0f;
    }
    return t;
}

// The NEAR crossing of a circle. A ray starting inside one has its near
// crossing behind it and is handled by the caller, which is the only place
// that knows what "inside" should mean for the whole shape.
float RayCircleT(const Ray& ray, Vec2 centre, float radius) {
    const Vec2  toCentre = centre - ray.origin;
    const float along    = Dot(toCentre, ray.direction);
    const float radiusSq = radius * radius;
    const float distSq   = toCentre.LengthSquared() - along * along;

    if (distSq > radiusSq) {
        return -1.0f;
    }
    const float enter = along - std::sqrt(std::max(0.0f, radiusSq - distSq));
    return enter >= 0.0f ? enter : -1.0f;
}

// The same shape with its rounding taken off, which is what "the core" means
// everywhere in this file.
ConvexShape CoreOf(const ConvexShape& shape) {
    ConvexShape core = shape;
    core.radius      = 0.0f;
    return core;
}

// The outward normal of edge `i`, for a core wound ANTICLOCKWISE - which is
// what MakeAnticlockwise guarantees for a box and what a polygon collider is
// required to be. Turning the edge clockwise is Perpendicular() negated.
Vec2 EdgeNormal(const ConvexShape& shape, int i) {
    const Vec2 from = shape.points[i];
    const Vec2 to   = shape.points[(i + 1) % shape.count];
    return -(to - from).Perpendicular().Normalized();
}

// ---------------------------------------------------------------------------
//  WHERE TWO OVERLAPPING SHAPES MEET: the MIDDLE OF THE REGION THEY SHARE.
//
//  THIS USED TO BE THE AVERAGE OF THE TWO SUPPORT VERTICES, and that is wrong
//  in the most ordinary case there is. Support returns a single vertex, and
//  along a flat face two vertices tie - so it keeps whichever comes first in
//  the point list. For a crate resting on a floor that is the FLOOR'S FAR
//  CORNER and the CRATE'S NEAR CORNER, and their average sits
//  (widthA - widthB) / 2 away from where the two shapes actually touch. On a
//  40-wide box resting on an 800-wide floor that is 190 units, outside the
//  crate altogether.
//
//  It never looked wrong because for two EQUAL-width boxes the two tie-broken
//  corners fall on opposite sides and cancel exactly. Stack identical crates
//  and the answer is perfect; rest one on a floor and it is nonsense.
//
//  WHAT IT COST, both measured before this was changed:
//
//    A BOX RESTING ON FLAT GROUND SPUN UP AND FELL THROUGH IT. The contact
//    solver applies its impulse at this point, so a point 190 units off
//    centre is a 190-unit lever arm on a body that is doing nothing at all.
//    Angular velocity reached 13.6 rad/s in four seconds.
//
//    AND IT SANK. The impulse needed to stop a body is divided by an
//    effective mass that includes invInertia * arm^2; at arm 190 that is 136
//    times the real value, so the contact pushed back 136 times too weakly.
//
//  HOW IT IS DONE NOW. The normal is known, so the contact lies on the line
//  perpendicular to it. Project both shapes onto that line, intersect the two
//  intervals - that intersection IS the overlap region, the thing a proper
//  manifold would clip a face down to - and take its middle. Along the normal
//  itself, sit halfway between the two surfaces.
//
//  STILL EXACT FOR A CIRCLE, which is what the old comment claimed for the
//  average: two circles touching along the line of centres share one tangent
//  coordinate, so the intersection collapses to it.
//
//  STILL "ROUGHLY WHERE" FOR A CORNER, which is all Overlap.h promises. A box
//  landing on another box's corner gets the middle of a small overlap region
//  rather than the deepest point of it, and the difference is a fraction of
//  the overlap.
// ---------------------------------------------------------------------------
Vec2 ContactPoint(const ConvexShape& a, const ConvexShape& b, Vec2 normal) {
    // ACROSS the contact: the shared span, and its middle.
    //
    // Project already accounts for each shape's radius, so a rounded box and
    // a capsule need no special case here.
    const Vec2     tangent = normal.Perpendicular();
    const Interval alongA  = Project(a, tangent);
    const Interval alongB  = Project(b, tangent);

    const float lo = std::max(alongA.min, alongB.min);
    const float hi = std::min(alongA.max, alongB.max);

    // ALONG the contact: halfway between the two surfaces. For an overlap
    // that is inside both of them, which is where a contact belongs; for a
    // pair that is apart it is the middle of the gap.
    const Interval acrossA = Project(a, normal);
    const Interval acrossB = Project(b, normal);

    // Rebuilt from its two coordinates in the (tangent, normal) frame, which
    // is orthonormal because Perpendicular is a quarter turn of a unit vector.
    return tangent * ((lo + hi) * 0.5f) + normal * ((acrossA.max + acrossB.min) * 0.5f);
}

// ---------------------------------------------------------------------------
//  THE AXES THAT HAVE TO BE TESTED, and why this particular set is complete.
//
//  The separating axis theorem says two convex shapes are apart if and only
//  if there is some direction in which their shadows do not touch. For two
//  POLYGONS it is enough to try each polygon's edge normals - a finite list.
//
//  Rounded shapes need more. Growing a polygon by a radius rounds off its
//  corners, and the gap between two rounded corners can be missed by every
//  edge normal. The extra directions needed are exactly the ones between a
//  vertex of one core and a vertex of the other, so those get added too.
//
//  For two circles that collapses to a single axis - the line between their
//  centres - which is precisely the test anyone would write by hand.
//
//  Up to 8+8 edge normals and 8x8 vertex pairs is at most 80 axes, and the
//  broad phase makes sure this only runs on pairs that are already close.
// ---------------------------------------------------------------------------
int GatherAxes(const ConvexShape& a, const ConvexShape& b, Vec2* out, int maxOut) {
    int count = 0;

    const auto add = [&](Vec2 axis) {
        if (count >= maxOut) {
            return;
        }
        if (axis.LengthSquared() <= kTinyLength) {
            return;
        }
        out[count++] = axis.Normalized();
    };

    // Edge normals. A 2-point core (a capsule or a segment) has one edge, and
    // both of its perpendiculars matter, which the loop produces naturally
    // because it walks the points in a cycle.
    const auto addEdgeNormals = [&](const ConvexShape& shape) {
        if (shape.count < 2) {
            return;   // a single point has no edges
        }
        for (int i = 0; i < shape.count; ++i) {
            const Vec2 edge = shape.points[(i + 1) % shape.count] - shape.points[i];
            add(edge.Perpendicular());
        }
    };

    addEdgeNormals(a);
    addEdgeNormals(b);

    // Vertex-to-vertex directions, which is what makes rounded corners work.
    for (int i = 0; i < a.count; ++i) {
        for (int j = 0; j < b.count; ++j) {
            add(b.points[j] - a.points[i]);
        }
    }

    return count;
}

} // namespace

// ---------------------------------------------------------------------------
//  AABB
// ---------------------------------------------------------------------------

void AABB::Encapsulate(Vec2 point) {
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
}

AABB AABB::Expanded(float amount) const {
    return AABB{Vec2{min.x - amount, min.y - amount},
                Vec2{max.x + amount, max.y + amount}};
}

// ---------------------------------------------------------------------------
//  The fast paths
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
//  Segments
// ---------------------------------------------------------------------------

Vec2 ClosestPointOnSegment(const Segment& segment, Vec2 point) {
    const Vec2  along  = segment.b - segment.a;
    const float lengthSq = along.LengthSquared();
    if (lengthSq <= kTinyLength) {
        return segment.a;   // the segment is a point
    }

    // How far along the segment the perpendicular foot falls, as a share of
    // its length - then pinned to [0, 1] so the answer never leaves the
    // segment.
    const float t = std::clamp(Dot(point - segment.a, along) / lengthSq, 0.0f, 1.0f);
    return segment.a + along * t;
}

void ClosestPointsBetweenSegments(const Segment& first, const Segment& second,
                                  Vec2& outOnFirst, Vec2& outOnSecond) {
    const Vec2  d1 = first.b - first.a;
    const Vec2  d2 = second.b - second.a;
    const Vec2  r  = first.a - second.a;

    const float a = d1.LengthSquared();
    const float e = d2.LengthSquared();
    const float f = Dot(d2, r);

    // Either or both segments can be a single point, and each case has to be
    // handled before the general one divides by a length.
    if (a <= kTinyLength && e <= kTinyLength) {
        outOnFirst  = first.a;
        outOnSecond = second.a;
        return;
    }
    if (a <= kTinyLength) {
        outOnFirst  = first.a;
        outOnSecond = ClosestPointOnSegment(second, first.a);
        return;
    }
    if (e <= kTinyLength) {
        outOnFirst  = ClosestPointOnSegment(first, second.a);
        outOnSecond = second.a;
        return;
    }

    const float c = Dot(d1, r);
    const float b = Dot(d1, d2);

    // The denominator is zero exactly when the segments are PARALLEL, and then
    // there is no single nearest pair - any point along the overlap is as
    // close as any other. Picking the start of the first segment and
    // projecting it is as good an answer as exists, and it is stable.
    const float denom = a * e - b * b;

    float s = 0.0f;
    if (denom > kTinyLength) {
        s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
    }

    // Having fixed s, the matching point on the other segment is the
    // projection of it - clamped, which is what keeps a near-parallel pair
    // from producing a point off the end.
    float t = (b * s + f) / e;
    if (t < 0.0f) {
        t = 0.0f;
        s = std::clamp(-c / a, 0.0f, 1.0f);
    } else if (t > 1.0f) {
        t = 1.0f;
        s = std::clamp((b - c) / a, 0.0f, 1.0f);
    }

    outOnFirst  = first.a + d1 * s;
    outOnSecond = second.a + d2 * t;
}

// ---------------------------------------------------------------------------
//  Building the general shape out of a specific one
// ---------------------------------------------------------------------------

ConvexShape ToConvex(const AABB& box) {
    ConvexShape shape;
    shape.count     = 4;
    shape.radius    = 0.0f;
    shape.points[0] = Vec2{box.min.x, box.min.y};
    shape.points[1] = Vec2{box.max.x, box.min.y};
    shape.points[2] = Vec2{box.max.x, box.max.y};
    shape.points[3] = Vec2{box.min.x, box.max.y};
    return shape;
}

ConvexShape ToConvex(const Circle& circle) {
    // ONE POINT, and the radius does the rest. A circle is not approximated by
    // a polygon anywhere in this engine - that would make it collide as a
    // many-sided shape, slightly too small, with flat spots.
    ConvexShape shape;
    shape.count     = 1;
    shape.radius    = circle.radius;
    shape.points[0] = circle.center;
    return shape;
}

ConvexShape ToConvex(const OBB& box) {
    const float c = std::cos(box.rotation);
    const float s = std::sin(box.rotation);

    // The box's own axes, turned by its rotation. Walking the four corners as
    // (-,-), (+,-), (+,+), (-,+) keeps them anticlockwise, which is what the
    // edge normals rely on.
    const Vec2 right{c * box.halfExtents.x, s * box.halfExtents.x};
    const Vec2 up{-s * box.halfExtents.y, c * box.halfExtents.y};

    ConvexShape shape;
    shape.count     = 4;
    shape.radius    = 0.0f;
    shape.points[0] = box.center - right - up;
    shape.points[1] = box.center + right - up;
    shape.points[2] = box.center + right + up;
    shape.points[3] = box.center - right + up;
    return shape;
}

ConvexShape ToConvex(const Capsule& capsule) {
    ConvexShape shape;
    shape.radius    = capsule.radius;
    shape.points[0] = capsule.a;

    // A capsule whose ends coincide IS a circle, and saying so here means the
    // collision routine never sees a zero-length edge.
    if (DistanceSquared(capsule.a, capsule.b) <= kTinyLength) {
        shape.count = 1;
        return shape;
    }
    shape.count     = 2;
    shape.points[1] = capsule.b;
    return shape;
}

ConvexShape ToConvex(const Segment& segment) {
    return ToConvex(Capsule{segment.a, segment.b, 0.0f});
}

ConvexShape ToConvex(const Vec2* points, int count, float radius) {
    ConvexShape shape;
    shape.radius = radius;
    shape.count  = std::clamp(count, 0, ConvexShape::kMaxPoints);
    for (int i = 0; i < shape.count; ++i) {
        shape.points[i] = points[i];
    }
    return shape;
}

AABB ConvexShape::Bounds() const {
    if (count <= 0) {
        return AABB{Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}};
    }
    AABB bounds{points[0], points[0]};
    for (int i = 1; i < count; ++i) {
        bounds.Encapsulate(points[i]);
    }
    return bounds.Expanded(radius);
}

// ---------------------------------------------------------------------------
//  The general collision routine
// ---------------------------------------------------------------------------

Contact Collide(const ConvexShape& a, const ConvexShape& b) {
    Contact contact;
    if (!a.IsValid() || !b.IsValid()) {
        return contact;
    }

    // 8 + 8 edge normals and 8 x 8 vertex pairs.
    constexpr int kMaxAxes = ConvexShape::kMaxPoints * ConvexShape::kMaxPoints +
                             2 * ConvexShape::kMaxPoints;
    Vec2      axes[kMaxAxes];
    const int axisCount = GatherAxes(a, b, axes, kMaxAxes);

    if (axisCount == 0) {
        // Two shapes sitting exactly on top of each other with no usable
        // direction between them. Pushing straight up is arbitrary, but it is
        // better than returning a zero normal that the caller would multiply
        // by a depth and get nowhere with.
        contact.touching   = true;
        contact.normal     = Vec2{0.0f, 1.0f};
        contact.separation = -(a.radius + b.radius);
        contact.point      = a.points[0];
        return contact;
    }

    // The axis of LEAST penetration is the one to push along - it is the
    // shortest way out. While scanning, any axis with a positive separation
    // proves the shapes are apart, and the largest such gap is the most useful
    // thing to report as the distance between them.
    float bestSeparation = -std::numeric_limits<float>::max();
    Vec2  bestNormal{0.0f, 1.0f};

    for (int i = 0; i < axisCount; ++i) {
        const Vec2     axis = axes[i];
        const Interval ia   = Project(a, axis);
        const Interval ib   = Project(b, axis);

        // Two gaps to consider: B entirely past A along the axis, or A
        // entirely past B. Whichever is larger is this axis's separation, and
        // its sign tells us which way the normal points.
        const float bAfterA = ib.min - ia.max;   // normal points +axis (A to B)
        const float aAfterB = ia.min - ib.max;   // normal points -axis

        float separation = bAfterA;
        Vec2  normal     = axis;
        if (aAfterB > bAfterA) {
            separation = aAfterB;
            normal     = -axis;
        }

        if (separation > bestSeparation) {
            bestSeparation = separation;
            bestNormal     = normal;
        }
    }

    // ======================================================================
    //  EVERY AXIS IS TESTED, EVEN ONCE ONE OF THEM HAS SEPARATED THEM.
    //
    //  It used to stop at the first positive separation, on the reasoning
    //  that one is proof enough they do not touch. True for `touching`, and
    //  wrong for the other two fields: the GAP between two shapes is the
    //  LARGEST separation over all axes, not the first one found, and the
    //  normal that goes with it is the direction of that gap. Stopping early
    //  handed Physics2D::Distance - and every caller of it - whichever axis
    //  the loop happened to reach first.
    //
    //  The axis count is kMaxAxes - eight edge normals from each shape plus
    //  every vertex-to-vertex direction, so up to 80 for two octagons, not
    //  the sixteen this comment used to claim. The broad phase has already
    //  thrown away the pairs that are nowhere near each other, so the cost
    //  falls only on pairs that are close enough to be worth an exact answer
    //  - and for the box-on-box and circle-on-box pairs that make up almost
    //  every contact in practice it is eight axes and sixteen projections.
    // ======================================================================
    contact.touching   = bestSeparation <= 0.0f;
    contact.normal     = bestNormal;
    contact.separation = bestSeparation;
    contact.point      = ContactPoint(a, b, bestNormal);
    return contact;
}

bool Overlaps(const ConvexShape& a, const ConvexShape& b) {
    return Collide(a, b).touching;
}

bool Contains(const ConvexShape& shape, Vec2 point) {
    const ConvexShape asPoint = ToConvex(Circle{point, 0.0f});
    return Collide(shape, asPoint).touching;
}

Vec2 ClosestPoint(const ConvexShape& shape, Vec2 point) {
    if (!shape.IsValid()) {
        return point;
    }

    if (shape.count == 1) {
        // A circle. The nearest surface point is one radius along the line
        // from the centre, INSIDE OR OUT.
        //
        // The reach used to be clamped to how far away the query point was,
        // so a point inside came back UNCHANGED - which made a circle answer
        // this question differently from a box of the same footprint, and
        // contradicted both Collider.h ("for a point inside the collider, the
        // nearest point on the boundary") and this comment.
        const Vec2  toPoint = point - shape.points[0];
        const float length  = toPoint.Length();
        if (length <= kTinyLength) {
            // No direction exists at the exact centre, so any is as good as
            // another; +x is a point on the surface, which is the contract.
            return shape.points[0] + Vec2{shape.radius, 0.0f};
        }
        return shape.points[0] + toPoint * (shape.radius / length);
    }

    // Walk every edge of the core and keep the nearest foot. For a convex
    // shape that is the nearest point on its boundary, and for a point INSIDE
    // the core it is still the nearest boundary point - which is what
    // Collider2D.ClosestPoint means.
    Vec2  best       = shape.points[0];
    Vec2  bestOut{0.0f, 0.0f};
    float bestDistSq = std::numeric_limits<float>::max();

    // HOW FAR OUTSIDE THE CORE THE QUERY POINT IS, over all the edge
    // half-planes. Positive on any one of them means outside, which for a
    // convex core is the whole test - and it cannot be fooled by two edges
    // tying on distance, which is what a dot against the single kept normal
    // was. Meaningless for a 2-point core, which is handled below.
    float furthestOutside = -std::numeric_limits<float>::max();

    for (int i = 0; i < shape.count; ++i) {
        const Segment edge{shape.points[i], shape.points[(i + 1) % shape.count]};
        const Vec2    foot    = ClosestPointOnSegment(edge, point);
        const float   distSq  = DistanceSquared(foot, point);
        const Vec2    outwardI = EdgeNormal(shape, i);

        furthestOutside =
            std::max(furthestOutside, Dot(point - shape.points[i], outwardI));

        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best       = foot;
            bestOut    = outwardI;
        }
    }

    if (shape.radius <= kTinyLength) {
        return best;
    }

    // ======================================================================
    //  ROUNDED: ONE RADIUS OUT FROM THE CORE, AND "OUT" IS THE WHOLE
    //  QUESTION.
    //
    //  Outside the core, the direction from the core's nearest point toward
    //  the query point is outward - and it is also the right direction round
    //  a corner, so it is used wherever it applies, which is nearly
    //  everywhere.
    //
    //  INSIDE A POLYGON CORE that direction points the wrong way, and this
    //  once followed it anyway while clamping the distance to how far away
    //  the query point was. A rounded box with half-extents 10 and a radius
    //  of 5, asked about its own centre, answered (0, -5): ten units inside a
    //  surface that is at y = -15, and neither a boundary point nor the query
    //  point. For an interior point the outward EDGE NORMAL is used instead,
    //  which gives (0, -15).
    //
    //  ==================================================================
    //  AND ONLY FOR A POLYGON CORE, WHICH COST A REGRESSION TO LEARN.
    //
    //  A 2-POINT CORE - every capsule, and every edge collider with a
    //  thickness - HAS NO INTERIOR. A segment encloses nothing, so every
    //  query point off it is outside it and the toward-the-query direction is
    //  always right.
    //
    //  It also has TWO outward normals, the segment walked both ways, and the
    //  loop above can keep only one: the two produce an identical foot and an
    //  identical distance, and the comparison is strict. So consulting
    //  bestOut for a 2-point core answers HALF THE PLANE ON THE WRONG SIDE.
    //  Measured: a capsule 20 wide asked about a point 50 to its left
    //  answered 10 to its RIGHT, and a character standing on an edge collider
    //  was told the nearest point was five pixels UNDER the ground. The code
    //  this replaced got both right; the test that went with it only
    //  exercised a 4-point core, where bestOut really is the nearest edge's
    //  normal, which is why nothing caught it.
    //
    //  AND IT IS ASKED OF THE WHOLE CORE, NOT OF ONE EDGE. The first attempt
    //  tested the direction against the single normal the nearest-foot loop
    //  kept, which is the same proxy the 2-point case had just punished - and
    //  it failed again on a TIE. A query 9.999 above a corner is 400.0f from
    //  the top edge and 400.000001 from the side one, which float32 rounds to
    //  the same number: the side edge is kept, the dot against ITS normal is
    //  -0.001, and a point plainly outside the core was answered on the side
    //  face. furthestOutside above asks every edge, so no tie can move it.
    // ==================================================================
    const Vec2  outward = point - best;
    const float length  = outward.Length();

    // A 2-POINT CORE IS ALWAYS OUTSIDE: a segment encloses nothing.
    const bool outsideCore = shape.count <= 2 || furthestOutside > 0.0f;

    if (length > kTinyLength && outsideCore) {
        return best + outward * (shape.radius / length);
    }

    // Standing exactly on the core, or inside a polygon one: there is no
    // direction to take from the query point, so the surface normal at the
    // nearest point is used.
    return best + bestOut * shape.radius;
}

// ---------------------------------------------------------------------------
//  Rays
// ---------------------------------------------------------------------------

bool Raycast(const Ray& ray, const AABB& box, RaycastHit& outHit) {
    // The slab method: treat the box as two overlapping bands, one per axis,
    // and keep the window of distances that is inside BOTH. If that window
    // ever closes, the ray misses.
    float nearest  = 0.0f;
    float furthest = ray.distance;
    Vec2  normal{0.0f, 0.0f};

    const float origin[2]    = {ray.origin.x, ray.origin.y};
    const float direction[2] = {ray.direction.x, ray.direction.y};
    const float lo[2]        = {box.min.x, box.min.y};
    const float hi[2]        = {box.max.x, box.max.y};

    for (int axis = 0; axis < 2; ++axis) {
        if (std::fabs(direction[axis]) < kTinyLength) {
            // Travelling parallel to this pair of sides: either already
            // between them for the whole journey, or never.
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return false;
            }
            continue;
        }

        const float inverse = 1.0f / direction[axis];
        float       tEnter  = (lo[axis] - origin[axis]) * inverse;
        float       tExit   = (hi[axis] - origin[axis]) * inverse;

        float sign = -1.0f;
        if (tEnter > tExit) {
            std::swap(tEnter, tExit);
            sign = 1.0f;
        }

        if (tEnter > nearest) {
            nearest = tEnter;
            normal  = (axis == 0) ? Vec2{sign, 0.0f} : Vec2{0.0f, sign};
        }
        furthest = std::min(furthest, tExit);

        if (nearest > furthest) {
            return false;
        }
    }

    outHit.hit      = true;
    outHit.distance = nearest;
    outHit.fraction = ray.distance > kTinyLength ? nearest / ray.distance : 0.0f;
    outHit.point    = ray.origin + ray.direction * nearest;

    // A ray that started inside has no entry face, so the normal is turned
    // back along the ray. See the note in the header about why starting inside
    // is a hit at all.
    outHit.normal = (nearest <= 0.0f) ? -ray.direction : normal;
    return true;
}

bool Raycast(const Ray& ray, const ConvexShape& shape, RaycastHit& outHit) {
    if (!shape.IsValid()) {
        return false;
    }

    // ---- a circle: solve where the ray is exactly one radius from the centre
    if (shape.count == 1) {
        const Vec2  toCentre = shape.points[0] - ray.origin;
        const float along    = Dot(toCentre, ray.direction);
        const float distSq   = toCentre.LengthSquared() - along * along;
        const float radiusSq = shape.radius * shape.radius;

        if (distSq > radiusSq) {
            return false;   // the ray's line passes wide of the circle
        }

        const float half  = std::sqrt(std::max(0.0f, radiusSq - distSq));
        float       enter = along - half;

        // Starting inside: the first surface crossed is behind us, so the hit
        // is here, now, facing back.
        const bool inside = toCentre.LengthSquared() <= radiusSq;
        if (inside) {
            enter = 0.0f;
        }
        if (enter < 0.0f || enter > ray.distance) {
            return false;
        }

        outHit.hit      = true;
        outHit.distance = enter;
        outHit.fraction = ray.distance > kTinyLength ? enter / ray.distance : 0.0f;
        outHit.point    = ray.origin + ray.direction * enter;
        outHit.normal   = inside ? -ray.direction
                                 : (outHit.point - shape.points[0]).Normalized();
        return true;
    }

    // ---- ANYTHING WITH A RADIUS: its core, grown outward by that radius --
    //
    //  SOLVED, NOT SAMPLED. Physics2D::CircleCast is built on this branch and
    //  its header promises "EXACT, not stepped", so this has to be: the
    //  boundary of a convex core grown by a radius is a handful of offset
    //  segments and a handful of arcs, and both have a closed form.
    //
    //  This comment used to describe a stepping loop, because for a while
    //  that is what was here - and a comment claiming a quarter-radius
    //  sampling error on a routine that is exact is its own defect in a file
    //  whose comments are the specification.
    //
    //  ======================================================================
    //  THE CORE IS WHATEVER SHAPE IT IS, AND IT USED TO BE ASSUMED TO BE A
    //  SEGMENT.
    //
    //  This branch took Segment{points[0], points[count - 1]}, which is right
    //  for a capsule - two points - and wrong for everything else. For a
    //  4-point rounded box that segment is the line from corner 0 to corner
    //  3: ONE SIDE of the box. A ray at the other three sides missed.
    //
    //  AND IT WAS NOT A CORNER CASE, because of how CircleCast works.
    //  Physics2D::CircleCast slides a circle by GROWING THE TARGET'S RADIUS
    //  and firing a thin ray - the Minkowski trick, and the reason that
    //  function is exact. But growing the radius is exactly what sends a
    //  shape down this branch: so every BOX, POLYGON and TILEMAP collider in
    //  the scene became "a capsule around one of its edges" the moment a
    //  circle cast looked at it, and a character sweeping a circle at a wall
    //  could pass straight through it.
    // ----------------------------------------------------------------------
    if (shape.radius > kTinyLength) {
        const ConvexShape core = CoreOf(shape);

        // STARTING INSIDE is a hit here and now, facing back - the same rule
        // the circle branch above uses, and the same one the polygon clip
        // below uses when its entry parameter is not positive.
        if (Collide(core, ToConvex(Circle{ray.origin, shape.radius})).touching) {
            outHit.hit      = true;
            outHit.distance = 0.0f;
            outHit.fraction = 0.0f;
            outHit.point    = ray.origin;
            outHit.normal   = -ray.direction;
            return true;
        }

        // ==================================================================
        //  EVERY PIECE OF THE BOUNDARY, AND THE NEAREST CROSSING WINS.
        //
        //  The shape is the Minkowski sum of its core with a disc, and the
        //  boundary of that sum is made of exactly two kinds of piece: each
        //  CORE EDGE pushed outward by the radius, and an ARC of that radius
        //  around each CORE VERTEX. So the first crossing is the nearest of a
        //  ray-against-segment solve per edge and a ray-against-circle solve
        //  per vertex - at most eight of each, all exact, no stepping.
        //
        //  TAKING THE WHOLE CIRCLE RATHER THAN THE ARC IS HARMLESS. A point
        //  on a vertex circle that is not on its arc lies strictly INSIDE the
        //  shape, so the ray must have crossed the real boundary before
        //  reaching it - and the nearest candidate is the one taken.
        //
        //  WHAT THIS REPLACES. The branch used to collapse any core to
        //  Segment{points[0], points[count - 1]} and walk it in steps. For a
        //  capsule - two points - that segment IS the capsule, so it worked.
        //  For a 4-point rounded box it is the line from corner 0 to corner
        //  3: one side. And growing the radius is precisely what
        //  Physics2D::CircleCast does to every shape it tests, so every box,
        //  polygon and tilemap collider in the scene became "a capsule around
        //  one of its edges" the moment a circle cast looked at it - a
        //  character sweeping a circle at a wall could pass straight through.
        //
        //  It was then fixed by stepping against the real core, which was
        //  correct and still not good enough: Physics2D.h promises CircleCast
        //  is "EXACT ... No stepping, no approximation", and a promise in a
        //  header is a promise. A capsule falls out of the same arithmetic -
        //  two offset edges and two end circles - so that case is exact now
        //  as well, where the stepped version was good to a quarter of the
        //  radius and never sampled the last partial step.
        // ==================================================================
        float best       = -1.0f;
        Vec2  bestNormal = -ray.direction;

        const auto consider = [&](float t, Vec2 normal) {
            if (t >= 0.0f && (best < 0.0f || t < best)) {
                best       = t;
                bestNormal = normal;
            }
        };

        // THE OFFSET EDGES. For a 2-point core the two edges are the segment
        // and the same segment reversed, so their outward normals are
        // opposites and the pair of them is exactly a capsule's two sides.
        for (int i = 0; i < shape.count; ++i) {
            const Vec2 outward = EdgeNormal(shape, i);
            const Vec2 from    = shape.points[i] + outward * shape.radius;
            const Vec2 to = shape.points[(i + 1) % shape.count] + outward * shape.radius;
            consider(RaySegmentT(ray, from, to), outward);
        }

        // THE CORNER ARCS. The normal is radial, which is what makes a sweep
        // glance off a corner rather than stopping square against it.
        for (int i = 0; i < shape.count; ++i) {
            const float t = RayCircleT(ray, shape.points[i], shape.radius);
            if (t < 0.0f) {
                continue;
            }
            const Vec2 at   = ray.origin + ray.direction * t;
            const Vec2 away = at - shape.points[i];

            // A SQUARED LENGTH NEEDS A SQUARED EPSILON, and comparing it
            // against kTinyLength itself meant every radius below about a
            // thousandth of a unit fell back to -ray.direction: a glancing
            // hit on a corner arc reported a head-on normal, ninety degrees
            // out, which a script sliding or reflecting on it would follow.
            consider(t, away.LengthSquared() > kTinyLength * kTinyLength
                            ? away.Normalized()
                            : -ray.direction);
        }

        if (best < 0.0f || best > ray.distance) {
            return false;
        }

        outHit.hit      = true;
        outHit.distance = best;
        outHit.fraction = ray.distance > kTinyLength ? best / ray.distance : 0.0f;
        outHit.point    = ray.origin + ray.direction * best;
        outHit.normal   = bestNormal;
        return true;
    }

    // ---- A BARE SEGMENT: a real ray-against-segment crossing -------------
    //
    //  NOT THE HALF-PLANE CLIP BELOW, which cannot express this. A 2-point
    //  "polygon" has two edges - the segment, and the same segment traversed
    //  backwards - so the clip produces two EXACTLY OPPOSITE half-planes, and
    //  their intersection is the infinite LINE through the segment. Every ray
    //  that crossed that line counted as a hit, including one crossing it a
    //  mile past either end. An edge collider left at its default edgeRadius
    //  of 0 is exactly this shape, so a raycast looking for the floor could
    //  find a platform that was nowhere near it.
    //
    //  Solved rather than clipped, because for two segments it is four cross
    //  products: the ray hits at parameter t along itself and u along the
    //  segment, and it is a hit only when BOTH are in range.
    if (shape.count == 2) {
        const float t = RaySegmentT(ray, shape.points[0], shape.points[1]);
        if (t < 0.0f || t > ray.distance) {
            return false;
        }

        const Vec2 along = shape.points[1] - shape.points[0];
        Vec2       normal = along.Perpendicular().Normalized();
        if (Dot(normal, ray.direction) > 0.0f) {
            normal = -normal;   // always faces the ray
        }

        outHit.hit      = true;
        outHit.distance = t;
        outHit.fraction = ray.distance > kTinyLength ? t / ray.distance : 0.0f;
        outHit.point    = ray.origin + ray.direction * t;
        outHit.normal   = normal;
        return true;
    }

    // ---- a polygon: clip the ray against every edge's half-plane
    float nearest  = 0.0f;
    float furthest = ray.distance;
    Vec2  normal{0.0f, 0.0f};

    for (int i = 0; i < shape.count; ++i) {
        const Vec2 from = shape.points[i];
        const Vec2 to   = shape.points[(i + 1) % shape.count];

        // Anticlockwise winding means the outward normal is the edge turned
        // clockwise, which is Perpendicular() negated.
        const Vec2 outward = -(to - from).Perpendicular().Normalized();

        const float facing   = Dot(ray.direction, outward);
        const float distance = Dot(from - ray.origin, outward);

        if (std::fabs(facing) < kTinyLength) {
            // Parallel to this edge. Outside it means the ray can never be
            // inside the shape.
            if (distance < 0.0f) {
                return false;
            }
            continue;
        }

        const float t = distance / facing;
        if (facing < 0.0f) {
            // Heading inward across this edge: it is a candidate entry.
            if (t > nearest) {
                nearest = t;
                normal  = outward;
            }
        } else {
            // Heading outward: it bounds how far the ray can stay inside.
            furthest = std::min(furthest, t);
        }

        if (nearest > furthest) {
            return false;
        }
    }

    if (nearest > ray.distance) {
        return false;
    }

    outHit.hit      = true;
    outHit.distance = nearest;
    outHit.fraction = ray.distance > kTinyLength ? nearest / ray.distance : 0.0f;
    outHit.point    = ray.origin + ray.direction * nearest;
    outHit.normal   = (nearest <= 0.0f) ? -ray.direction : normal;
    return true;
}

} // namespace eng
