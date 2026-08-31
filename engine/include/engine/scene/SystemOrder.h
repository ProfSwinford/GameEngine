#pragma once

// =============================================================================
//  WEEK 10 - systems update in a DECLARED order, not in whatever order they
//  happened to register in. Ch. 8.3.
//
//  Why it matters concretely: collision before movement tests entities at last
//  frame's positions and lets fast objects pass through each other. Input
//  after movement makes the player's controls lag by one frame - a small,
//  awful, hard-to-diagnose feel problem.
//
//  Incidental ordering works until someone adds a system, and then it breaks
//  something unrelated. Same lesson as Week 7's boot ordering, one level up.
//
// =============================================================================
//  *** THE DECLARED ORDER. *** Logged once at startup - see
//  SystemScheduler::LogOrder and the paste in docs/week10-milestone4.md.
//
//    100 Input              sample the player's intent for this tick
//    200 Gameplay / AI      decide what everything wants to do
//    300 Movement           integrate positions
//    400 Collision          detect overlaps at the NEW positions
//    500 CollisionResponse  dispatch queued messages
//    600 Deferred           apply queued structural changes
//    700 Camera             follow whatever it follows, after it has moved
//    800 Render             draw the settled state
//    900 DebugDraw          on top of everything
//
//  Stages 100-700 run inside each FIXED SIMULATION STEP. Stages 800+ run once
//  per rendered frame, however many steps that frame ran - because drawing the
//  same scene three times because the accumulator owed three steps would be
//  three times the cost for one picture.
//
//  ONE PAIR WHOSE ORDER MATTERS, and what breaks if reversed: Movement (300)
//  before Collision (400). Reversed, collision tests last frame's positions,
//  so a fast entity is tested where it WAS, moves through a wall, and is
//  tested again on the far side - tunnelling, at any speed above one collider
//  width per tick, with no collision event ever firing.
//
//  Note stage 600. Structural changes are applied at ONE defined point, never
//  in the middle of a system's iteration. That is DeferredOps.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/core/Types.h>

#include <functional>
#include <string>
#include <vector>

namespace eng {

// An integer priority is entirely adequate. A dependency graph with a
// topological sort is a fine Phase 2 project and a bad use of Week 10.
namespace SystemStage {
inline constexpr i32 kInput             = 100;
inline constexpr i32 kGameplay          = 200;
inline constexpr i32 kMovement          = 300;
inline constexpr i32 kCollision         = 400;
inline constexpr i32 kCollisionResponse = 500;
inline constexpr i32 kDeferred          = 600;
inline constexpr i32 kCamera            = 700;
inline constexpr i32 kRender            = 800;
inline constexpr i32 kDebugDraw         = 900;

// Everything below this runs per simulation step; everything at or above it
// runs once per rendered frame.
inline constexpr i32 kFirstRenderStage  = kRender;
} // namespace SystemStage

class System {
public:
    virtual ~System() = default;

    // `deltaSeconds` is the FIXED step for simulation systems and the real
    // frame delta for render-stage systems. Systems do not get to choose;
    // reading a clock inside an update is the bug that makes the simulation
    // frame-rate dependent again.
    virtual void Update(f32 deltaSeconds) = 0;

    virtual const char* Name() const = 0;
    virtual i32         Order() const = 0;
};

class SystemScheduler {
public:
    // The scheduler does NOT own the system. Registration is a borrow, and the
    // system must Unregister before it dies - which is the same contract as a
    // component and its render system, for the same reason.
    static void Register(System* system);
    static void Unregister(System* system);
    static void Clear();

    // Runs every system whose order falls in [minOrder, maxOrder).
    static void UpdateRange(i32 minOrder, i32 maxOrder, f32 deltaSeconds);

    // Simulation stages, once per fixed step.
    static void Simulate(f32 fixedStepSeconds);
    // Render stages, once per rendered frame.
    static void RenderPass(f32 realDeltaSeconds);

    // Logged once at startup. Week 10's verification asks for this paste, and
    // Phase 2 will ask why something happens a frame late.
    static void LogOrder();

    static void ForEach(const std::function<void(System&)>& fn);
    static usize Count();
};

} // namespace eng
