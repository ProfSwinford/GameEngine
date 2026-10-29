#pragma once

// ============================================================================
//  Physics2D.h - asking the world questions, and the world's own settings.
//
//  Collision events tell you about things that touched you. These functions
//  are the other direction: they let a script ask about places it is NOT
//  touching, which is how most game logic that feels responsive is written.
//
//      Is there ground under my feet?           Raycast down
//      Can I jump here without hitting a roof?  OverlapBox above
//      What is in range of this explosion?      OverlapCircle
//      What did the gun hit?                    Raycast forward
//
//  None of these need a collider on the asker, and none of them wait for a
//  collision to happen. That is the point: a character controller built on
//  raycasts knows about the floor BEFORE it lands on it, and so never sinks
//  in by a frame.
//
//  ==========================================================================
//  WHAT A QUERY SEES
//
//  Every function takes a LAYER to look for, defaulting to "All", and a flag
//  for whether triggers count. The defaults are the forgiving ones, because a
//  query that silently finds nothing is harder to debug than one that finds
//  too much.
//
//  A DISABLED COLLIDER IS INVISIBLE to every query here, which is what makes
//  SetEnabled(false) a complete switch rather than a half one.
//
//  ==========================================================================
//  ALL QUERIES READ THE WORLD AS IT IS RIGHT NOW
//
//  There is no cached spatial structure to go stale: a query walks the live
//  collider list and tests against the shapes' current world positions. So
//  moving something and immediately querying gives the answer for where it
//  now is, with nothing to flush or rebuild.
//
//  The cost is that a query is linear in the number of colliders. For the
//  scale this engine is built for that is the right trade, and it is the same
//  trade the collision system's broad phase documents.
// ============================================================================

#include <engine/math/Overlap.h>
#include <engine/physics/Collider.h>
#include <engine/scene/EntityId.h>

#include <string_view>

namespace eng {

// ---------------------------------------------------------------------------
//  What a query found. Unity's RaycastHit2D.
//
//  `collider` is a POINTER AND NOT AN OWNER, valid until the next time
//  something is destroyed. Read what you need from it and do not keep it -
//  the same rule the collision events give for EntityId, for the same reason.
// ---------------------------------------------------------------------------
struct RaycastHit2D {
    ColliderComponent* collider = nullptr;
    EntityId           entity{};

    Vec2  point{};
    Vec2  normal{};
    float distance = 0.0f;
    float fraction = 0.0f;

    // Lets a hit be used directly in an `if`, which reads better than
    // comparing the collider against null at every call site.
    explicit operator bool() const { return collider != nullptr; }
};

// Which colliders a query is allowed to see.
//
// A small struct rather than four defaulted arguments, because
// `Raycast(from, dir, 10.0f, {"Ground"})` says what it filters on and
// `Raycast(from, dir, 10.0f, "Ground", false, true)` does not.
struct QueryFilter {
    // The layer to look for. "All" means every layer.
    std::string_view layer = kCollisionLayerAll;

    // Whether trigger colliders can be found. Unity's
    // Physics2D.queriesHitTriggers, per query rather than global - a global
    // switch that changes the meaning of every query in the project is a
    // setting people get bitten by once and then set permanently.
    bool hitTriggers = true;

    // An entity to ignore, normally the one asking. Without it, a raycast
    // started inside your own collider finds yourself first, every time, and
    // the usual fix is to offset the start point - which then misses things
    // that are genuinely close.
    EntityId ignore{};
};

class Physics2D {
public:
    // ---- the world's settings ---------------------------------------------

    // Defaults to Unity's (0, -9.81). Y is up in this engine, so gravity is
    // negative.
    //
    // WORTH KNOWING: a 2D game's units are usually PIXELS, and -9.81 pixels
    // per second squared is a very gentle drift. A jump that feels right at
    // this scale usually wants gravity in the hundreds. config/engine.json
    // sets it, so it can be tuned without a rebuild.
    static Vec2 Gravity();
    static void SetGravity(Vec2 gravity);

    // ---- rays --------------------------------------------------------------

    // The nearest thing along the ray, or a false hit when there is nothing.
    //
    // `direction` need not be normalized - it is normalized here, because
    // forgetting to is otherwise a silent change in what `distance` means.
    static RaycastHit2D Raycast(Vec2 origin, Vec2 direction, float distance,
                                const QueryFilter& filter = {});

    // Every hit along the ray, nearest first. Returns how many were written.
    static int RaycastAll(Vec2 origin, Vec2 direction, float distance,
                          RaycastHit2D* out, int maxOut,
                          const QueryFilter& filter = {});

    // ---- overlaps ----------------------------------------------------------

    // The first collider containing this point, or null.
    static ColliderComponent* OverlapPoint(Vec2 point, const QueryFilter& filter = {});

    static int OverlapPointAll(Vec2 point, ColliderComponent** out, int maxOut,
                               const QueryFilter& filter = {});

    static int OverlapCircle(Vec2 center, float radius, ColliderComponent** out,
                             int maxOut, const QueryFilter& filter = {});

    // `angle` is in radians, so a rotated box query is possible - which is
    // what makes this usable for "is there room for me here?" on a tilted
    // character.
    static int OverlapBox(Vec2 center, Vec2 size, float angle,
                          ColliderComponent** out, int maxOut,
                          const QueryFilter& filter = {});

    static int OverlapCapsule(Vec2 center, Vec2 size, CapsuleDirection direction,
                              float angle, ColliderComponent** out, int maxOut,
                              const QueryFilter& filter = {});

    // The general form the four above are built on: anything that can be a
    // ConvexShape can be an overlap query.
    static int OverlapShape(const ConvexShape& shape, ColliderComponent** out,
                            int maxOut, const QueryFilter& filter = {});

    // ---- sweeps ------------------------------------------------------------

    // A circle slid along a direction until it hits something. What a thick
    // raycast is for: a bullet with a radius, or checking whether a character
    // can move to a spot without a thin gap letting it through.
    //
    // EXACT, not stepped. Growing a shape by a radius and firing a thin ray at
    // it gives the same answer as sliding a circle into it - and ConvexShape
    // already carries a radius, so the swept test IS the ordinary raycast
    // against a slightly fatter shape. That equivalence is the reason the
    // unified shape representation was worth having.
    static RaycastHit2D CircleCast(Vec2 origin, float radius, Vec2 direction,
                                   float distance, const QueryFilter& filter = {});

    // The same idea for a box. STEPPED rather than exact, because the swept
    // volume of a rotated box is not a box grown by a radius, and computing it
    // properly means building a convex hull per pair. The step is a quarter of
    // the box's smaller side, then refined - so the error is far below a pixel
    // at any sane size, and it is an approximation stated rather than hidden.
    static RaycastHit2D BoxCast(Vec2 origin, Vec2 size, float angle, Vec2 direction,
                                float distance, const QueryFilter& filter = {});

    // ---- distance ----------------------------------------------------------

    // The gap between two colliders, and the nearest points on each. Negative
    // when they overlap. Unity's Collider2D.Distance.
    struct Distance2D {
        bool  overlapped = false;
        float distance   = 0.0f;
        Vec2  pointA{};
        Vec2  pointB{};
        Vec2  normal{};
    };
    static Distance2D Distance(const ColliderComponent& a, const ColliderComponent& b);

    // ---- counters, for the editor -----------------------------------------
    static std::size_t QueriesThisFrame();
    static void        ResetQueryCounter();
};

} // namespace eng
