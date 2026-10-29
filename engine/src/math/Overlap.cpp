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

        // A positive separation on ANY axis is proof they do not touch, so
        // there is nothing to gain from the remaining axes.
        if (bestSeparation > 0.0f) {
            contact.touching   = false;
            contact.normal     = bestNormal;
            contact.separation = bestSeparation;
            contact.point      = Support(a, bestNormal) + bestNormal * a.radius;
            return contact;
        }
    }

    // No axis separated them, so they overlap by -bestSeparation along
    // bestNormal, which points from A toward B.
    contact.touching   = true;
    contact.normal     = bestNormal;
    contact.separation = bestSeparation;

    // Roughly where. The two surface points along the contact normal, averaged
    // - exact for a circle, and close enough to the middle of a face-to-face
    // contact to put a torque in the right direction.
    const Vec2 onA = Support(a, bestNormal) + bestNormal * a.radius;
    const Vec2 onB = Support(b, -bestNormal) - bestNormal * b.radius;
    contact.point  = (onA + onB) * 0.5f;
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
        // from the centre - unless the point IS the centre, where no direction
        // exists and the centre itself is as good an answer as any.
        const Vec2  toPoint = point - shape.points[0];
        const float length  = toPoint.Length();
        if (length <= kTinyLength) {
            return shape.points[0];
        }
        const float reach = std::min(length, shape.radius);
        return shape.points[0] + toPoint * (reach / length);
    }

    // Walk every edge of the core and keep the nearest foot. For a convex
    // shape that is the nearest point on its boundary, and for a point INSIDE
    // the core it is still the nearest boundary point - which is what
    // Collider2D.ClosestPoint means.
    Vec2  best        = shape.points[0];
    float bestDistSq  = std::numeric_limits<float>::max();
    for (int i = 0; i < shape.count; ++i) {
        const Segment edge{shape.points[i], shape.points[(i + 1) % shape.count]};
        const Vec2    foot   = ClosestPointOnSegment(edge, point);
        const float   distSq = DistanceSquared(foot, point);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best       = foot;
        }
    }

    if (shape.radius <= kTinyLength) {
        return best;
    }

    // Rounded: step out from the core's surface by the radius, toward the
    // query point.
    const Vec2  outward = point - best;
    const float length  = outward.Length();
    if (length <= kTinyLength) {
        return best;
    }
    const float reach = std::min(length, shape.radius);
    return best + outward * (reach / length);
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

    // ---- a capsule: the swept circle of its core segment
    if (shape.radius > kTinyLength) {
        // Walked in small steps rather than solved. The exact answer is a
        // quadratic per side plus one per end cap, and the stepping version is
        // a dozen lines instead of sixty for an error no larger than the step.
        //
        // THE STEP IS IN WORLD UNITS, not a fixed count, so a long ray is not
        // sampled more coarsely than a short one. The refinement afterwards is
        // what keeps the reported point on the surface rather than up to one
        // step inside it.
        const Segment core{shape.points[0], shape.points[shape.count - 1]};
        const float   step = std::max(shape.radius * 0.25f, 0.01f);

        float travelled = 0.0f;
        bool  found     = false;
        while (travelled <= ray.distance) {
            const Vec2 at = ray.origin + ray.direction * travelled;
            if (DistanceSquared(ClosestPointOnSegment(core, at), at) <=
                shape.radius * shape.radius) {
                found = true;
                break;
            }
            travelled += step;
        }
        if (!found) {
            return false;
        }

        // Binary search back over the last step for the crossing point.
        float outside = std::max(0.0f, travelled - step);
        float inside  = travelled;
        for (int i = 0; i < 16; ++i) {
            const float mid = (outside + inside) * 0.5f;
            const Vec2  at  = ray.origin + ray.direction * mid;
            if (DistanceSquared(ClosestPointOnSegment(core, at), at) <=
                shape.radius * shape.radius) {
                inside = mid;
            } else {
                outside = mid;
            }
        }

        outHit.hit      = true;
        outHit.distance = inside;
        outHit.fraction = ray.distance > kTinyLength ? inside / ray.distance : 0.0f;
        outHit.point    = ray.origin + ray.direction * inside;

        const Vec2 onCore = ClosestPointOnSegment(core, outHit.point);
        outHit.normal     = (inside <= 0.0f) ? -ray.direction
                                             : (outHit.point - onCore).Normalized();
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

    // A 2-point core is a bare segment with no interior, so "inside every
    // half-plane" is not a hit - only an actual crossing is. The clip above
    // cannot express that, so it is rejected here.
    if (shape.count == 2 && nearest <= 0.0f) {
        return false;
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
