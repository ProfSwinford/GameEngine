// ============================================================================
//  Physics2D.cpp - the queries, and the world's gravity. See Physics2D.h.
//
//  Every function here walks the live collider list and tests against the
//  shapes' CURRENT world positions. There is no cached spatial structure, so
//  there is nothing to go stale and nothing to rebuild after something moves.
// ============================================================================

#include <engine/physics/Physics2D.h>

#include <engine/core/Log.h>
#include <engine/physics/Collider.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <cmath>

namespace eng {
namespace {

// Unity's default. See the note in the header about why a pixel-scale game
// usually wants a much larger number than this.
Vec2 g_gravity{0.0f, -9.81f};

std::size_t g_queries = 0;

// Can a query see this collider at all?
bool Visible(const ColliderComponent& collider, const QueryFilter& filter) {
    if (!collider.Enabled()) {
        return false;   // a disabled collider is invisible to everything
    }
    if (!filter.hitTriggers && collider.IsTrigger()) {
        return false;
    }
    if (filter.layer != kCollisionLayerAll && collider.Layer() != filter.layer) {
        return false;
    }
    if (!filter.ignore.IsNull() && collider.OwnerId() == filter.ignore) {
        return false;
    }

    // The same rule the collision system applies: something queued for
    // destruction this step is already gone as far as gameplay is concerned.
    if (DeferredOps::IsPendingDestroy(collider.OwnerId())) {
        return false;
    }
    return true;
}

} // namespace

Vec2 Physics2D::Gravity() { return g_gravity; }

void Physics2D::SetGravity(Vec2 gravity) { g_gravity = gravity; }

std::size_t Physics2D::QueriesThisFrame() { return g_queries; }

void Physics2D::ResetQueryCounter() { g_queries = 0; }

// ---------------------------------------------------------------------------
//  Rays
// ---------------------------------------------------------------------------

int Physics2D::RaycastAll(Vec2 origin, Vec2 direction, float distance,
                          RaycastHit2D* out, int maxOut, const QueryFilter& filter) {
    ++g_queries;
    if (out == nullptr || maxOut <= 0) {
        return 0;
    }

    // Normalized HERE rather than expected of the caller. Forgetting to is
    // otherwise a silent change in what `distance` means, and a ray of length
    // 10 that only reaches 3 units is a confusing thing to debug.
    const Vec2 unit = direction.Normalized();
    if (unit.LengthSquared() <= 0.0f) {
        return 0;   // no direction at all
    }

    const Ray ray{origin, unit, std::max(distance, 0.0f)};

    int written = 0;
    std::vector<ConvexShape> shapes;   // reused across the loop; see GatherWorldShapes

    for (ColliderComponent* collider : CollisionSystem::All()) {
        if (collider == nullptr || !Visible(*collider, filter)) {
            continue;
        }

        const int count = GatherWorldShapes(*collider, shapes);

        // The NEAREST hit on this collider. An edge collider is many segments
        // and a ray may cross several of them; only the first one it reaches
        // is where it actually stopped.
        RaycastHit best;
        for (int i = 0; i < count; ++i) {
            RaycastHit hit;
            if (eng::Raycast(ray, shapes[i], hit) &&
                (!best.hit || hit.distance < best.distance)) {
                best = hit;
            }
        }
        if (!best.hit) {
            continue;
        }

        if (written < maxOut) {
            RaycastHit2D entry;
            entry.collider = collider;
            entry.entity   = collider->OwnerId();
            entry.point    = best.point;
            entry.normal   = best.normal;
            entry.distance = best.distance;
            entry.fraction = best.fraction;
            out[written++] = entry;
        }
    }

    // Nearest first, which is the order every caller wants and the order that
    // makes out[0] meaningful on its own.
    std::sort(out, out + written, [](const RaycastHit2D& lhs, const RaycastHit2D& rhs) {
        return lhs.distance < rhs.distance;
    });
    return written;
}

RaycastHit2D Physics2D::Raycast(Vec2 origin, Vec2 direction, float distance,
                                const QueryFilter& filter) {
    // Collects every hit and keeps the nearest, rather than stopping at the
    // first collider that happens to be early in the list. "The nearest thing"
    // is the only useful meaning of a single-result raycast.
    RaycastHit2D hits[64];
    const int    count = RaycastAll(origin, direction, distance, hits, 64, filter);
    return (count > 0) ? hits[0] : RaycastHit2D{};
}

// ---------------------------------------------------------------------------
//  Overlaps
// ---------------------------------------------------------------------------

int Physics2D::OverlapShape(const ConvexShape& shape, ColliderComponent** out,
                            int maxOut, const QueryFilter& filter) {
    ++g_queries;
    if (out == nullptr || maxOut <= 0) {
        return 0;
    }

    const AABB queryBounds = shape.Bounds();

    int         written = 0;
    std::vector<ConvexShape> shapes;   // reused across the loop; see GatherWorldShapes

    for (ColliderComponent* collider : CollisionSystem::All()) {
        if (collider == nullptr || !Visible(*collider, filter)) {
            continue;
        }

        // The cheap box test before the real one, exactly as the collision
        // system's broad phase does.
        if (!Overlaps(queryBounds, collider->WorldBounds())) {
            continue;
        }

        const int count = GatherWorldShapes(*collider, shapes);
        for (int i = 0; i < count; ++i) {
            if (Overlaps(shape, shapes[i])) {
                out[written++] = collider;
                break;   // one entry per collider, not one per sub-shape
            }
        }
        if (written >= maxOut) {
            break;
        }
    }
    return written;
}

ColliderComponent* Physics2D::OverlapPoint(Vec2 point, const QueryFilter& filter) {
    ColliderComponent* found = nullptr;
    const int count = OverlapPointAll(point, &found, 1, filter);
    return (count > 0) ? found : nullptr;
}

int Physics2D::OverlapPointAll(Vec2 point, ColliderComponent** out, int maxOut,
                               const QueryFilter& filter) {
    return OverlapShape(ToConvex(Circle{point, 0.0f}), out, maxOut, filter);
}

int Physics2D::OverlapCircle(Vec2 center, float radius, ColliderComponent** out,
                             int maxOut, const QueryFilter& filter) {
    return OverlapShape(ToConvex(Circle{center, std::max(radius, 0.0f)}), out, maxOut,
                        filter);
}

int Physics2D::OverlapBox(Vec2 center, Vec2 size, float angle, ColliderComponent** out,
                          int maxOut, const QueryFilter& filter) {
    return OverlapShape(ToConvex(OBB{center, size * 0.5f, angle}), out, maxOut, filter);
}

int Physics2D::OverlapCapsule(Vec2 center, Vec2 size, CapsuleDirection direction,
                              float angle, ColliderComponent** out, int maxOut,
                              const QueryFilter& filter) {
    // The same arithmetic CapsuleColliderComponent does: the radius is half
    // the short side and the core is whatever length is left.
    const bool  vertical = direction == CapsuleDirection::Vertical;
    const float breadth  = vertical ? size.x : size.y;
    const float length   = vertical ? size.y : size.x;
    const float radius   = breadth * 0.5f;
    const float half     = std::max(length - breadth, 0.0f) * 0.5f;

    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const Vec2  along = vertical ? Vec2{-s * half, c * half} : Vec2{c * half, s * half};

    return OverlapShape(ToConvex(Capsule{center - along, center + along, radius}), out,
                        maxOut, filter);
}

// ---------------------------------------------------------------------------
//  Sweeps
// ---------------------------------------------------------------------------

RaycastHit2D Physics2D::CircleCast(Vec2 origin, float radius, Vec2 direction,
                                   float distance, const QueryFilter& filter) {
    ++g_queries;

    const Vec2 unit = direction.Normalized();
    if (unit.LengthSquared() <= 0.0f) {
        return RaycastHit2D{};
    }

    const Ray ray{origin, unit, std::max(distance, 0.0f)};

    // EXACT, AND THIS IS THE TRICK WORTH UNDERSTANDING.
    //
    // Sliding a circle of radius r into a shape stops at exactly the same
    // place as firing a thin ray at that shape grown outward by r. That is the
    // Minkowski sum, and because ConvexShape already carries a radius, growing
    // a shape is adding one number to it. No stepping, no approximation.
    RaycastHit2D best;
    std::vector<ConvexShape> shapes;   // reused across the loop; see GatherWorldShapes

    for (ColliderComponent* collider : CollisionSystem::All()) {
        if (collider == nullptr || !Visible(*collider, filter)) {
            continue;
        }

        const int count = GatherWorldShapes(*collider, shapes);
        for (int i = 0; i < count; ++i) {
            ConvexShape grown = shapes[i];
            grown.radius += std::max(radius, 0.0f);

            RaycastHit hit;
            if (!eng::Raycast(ray, grown, hit)) {
                continue;
            }
            if (best.collider == nullptr || hit.distance < best.distance) {
                best.collider = collider;
                best.entity   = collider->OwnerId();
                best.point    = hit.point;
                best.normal   = hit.normal;
                best.distance = hit.distance;
                best.fraction = hit.fraction;
            }
        }
    }
    return best;
}

RaycastHit2D Physics2D::BoxCast(Vec2 origin, Vec2 size, float angle, Vec2 direction,
                                float distance, const QueryFilter& filter) {
    ++g_queries;

    const Vec2 unit = direction.Normalized();
    if (unit.LengthSquared() <= 0.0f) {
        return RaycastHit2D{};
    }

    // STEPPED, NOT EXACT, and stated rather than hidden.
    //
    // The circle cast above works because growing a shape by a radius is one
    // number. A box has no single radius, so its swept volume is a genuine
    // convex hull of two rotated rectangles - a per-pair computation this
    // engine does not have. Walking the box along the ray and testing for an
    // overlap gets the same answer to within a step, and the refinement
    // afterwards drops the error far below a pixel.
    const float span = std::max(std::min(size.x, size.y), 0.01f);
    const float step = span * 0.25f;

    ColliderComponent* hits[32];

    const auto overlapsAt = [&](float travelled) -> ColliderComponent* {
        const Vec2 at = origin + unit * travelled;
        const int  found =
            OverlapBox(at, size, angle, hits, 32, filter);
        return (found > 0) ? hits[0] : nullptr;
    };

    float              travelled = 0.0f;
    ColliderComponent* found     = nullptr;
    while (travelled <= distance) {
        found = overlapsAt(travelled);
        if (found != nullptr) {
            break;
        }
        travelled += step;
    }
    if (found == nullptr) {
        return RaycastHit2D{};
    }

    // Binary search back over the last step for where contact began.
    float clear    = std::max(0.0f, travelled - step);
    float blocked  = travelled;
    for (int i = 0; i < 16; ++i) {
        const float mid = (clear + blocked) * 0.5f;
        if (overlapsAt(mid) != nullptr) {
            blocked = mid;
        } else {
            clear = mid;
        }
    }

    found = overlapsAt(blocked);
    if (found == nullptr) {
        return RaycastHit2D{};
    }

    RaycastHit2D result;
    result.collider = found;
    result.entity   = found->OwnerId();
    result.distance = blocked;
    result.fraction = distance > 0.0f ? blocked / distance : 0.0f;
    result.point    = found->ClosestPoint(origin + unit * blocked);

    // The surface direction, taken from the shape rather than from the sweep:
    // the line from the contact back toward the box's centre.
    const Vec2 toCentre = (origin + unit * blocked) - result.point;
    result.normal = (toCentre.LengthSquared() > 1e-8f) ? toCentre.Normalized() : -unit;
    return result;
}

// ---------------------------------------------------------------------------
//  Distance
// ---------------------------------------------------------------------------

Physics2D::Distance2D Physics2D::Distance(const ColliderComponent& a,
                                          const ColliderComponent& b) {
    ++g_queries;

    std::vector<ConvexShape> shapesA;
    std::vector<ConvexShape> shapesB;
    const int countA = GatherWorldShapes(a, shapesA);
    const int countB = GatherWorldShapes(b, shapesB);

    Distance2D result;
    bool       first = true;

    for (int i = 0; i < countA; ++i) {
        for (int j = 0; j < countB; ++j) {
            const Contact contact = Collide(shapesA[i], shapesB[j]);

            // Collide reports a positive separation when apart and a negative
            // one when overlapping, so the nearest pair of sub-shapes is
            // simply the smallest number - no branch on touching needed.
            if (first || contact.separation < result.distance) {
                first             = false;
                result.overlapped = contact.touching;
                result.distance   = contact.separation;
                result.normal     = contact.normal;
                result.pointA     = contact.point;
                result.pointB     = contact.point;
            }
        }
    }

    if (!first) {
        // Refined from the shapes themselves, because Collide's contact point
        // is a midpoint and Distance is asked precisely when the two ends
        // matter separately.
        result.pointA = a.ClosestPoint(result.pointB);
        result.pointB = b.ClosestPoint(result.pointA);
    }
    return result;
}

} // namespace eng
