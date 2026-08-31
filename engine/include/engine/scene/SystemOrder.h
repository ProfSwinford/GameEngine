#pragma once

// ============================================================================
//  SystemOrder.h - systems run in a written-down order, not whichever order
//  they happened to be created in.
//
//  WHY THAT MATTERS, CONCRETELY
//  Run collision BEFORE movement and it checks where everything was LAST
//  frame, so a fast object passes straight through a wall and no collision is
//  ever reported. Run input AFTER movement and the controls lag by a frame -
//  a small, horrible, hard-to-describe problem that players feel immediately.
//
//  An accidental order works right up until somebody adds a system, and then
//  it breaks something apparently unrelated.
//
//  ==========================================================================
//  THE ORDER. Written to the log once at start-up, so it is never a guess.
//
//    100 Input              read what the player wants this tick
//    200 Gameplay / AI      decide what everything is going to do
//    300 Movement           actually move things
//    400 Collision          check overlaps at the NEW positions
//    500 CollisionResponse  deliver the queued messages
//    600 Deferred           create and destroy entities
//    700 Camera             follow whatever it follows, after it has moved
//    800 Render             draw the settled result
//    900 Gizmos             draw helper shapes on top
//
//  Stages 100-700 run once per FIXED SIMULATION STEP. Stages 800 and above run
//  once per drawn frame however many steps that frame took, because drawing
//  the same scene three times would cost three times as much for one picture.
//
//  THE PAIR THAT MATTERS MOST: Movement (300) before Collision (400). Swap
//  them and a fast object is tested where it WAS, moves through the wall, and
//  is tested again on the far side - so it passes through solid objects with
//  no collision ever firing.
//
//  Note stage 600. Entities are created and destroyed at ONE known point,
//  never in the middle of a system walking its own list. See DeferredOps.h.
//  ==========================================================================
// ============================================================================

#include <functional>

namespace eng {

// The stage numbers. A plain integer priority is entirely adequate here; the
// alternative (declaring which system depends on which and sorting that out
// automatically) is a lot of machinery for a list this short.
namespace SystemStage {
inline constexpr int kInput             = 100;
inline constexpr int kGameplay          = 200;
inline constexpr int kMovement          = 300;
inline constexpr int kCollision         = 400;
inline constexpr int kCollisionResponse = 500;
inline constexpr int kDeferred          = 600;
inline constexpr int kCamera            = 700;
inline constexpr int kRender            = 800;
inline constexpr int kGizmos            = 900;

// Everything below this runs per simulation step; everything at or above it
// runs once per drawn frame.
inline constexpr int kFirstRenderStage = kRender;
} // namespace SystemStage

// Anything that needs to run every tick. Subclass it, say which stage it
// belongs to, and register it with the scheduler.
class System {
public:
    virtual ~System() = default;

    // `deltaSeconds` is the FIXED step for simulation systems and the real
    // frame time for render-stage ones.
    //
    // It is handed IN rather than looked up. A system that read a clock for
    // itself would make the simulation depend on the frame rate again, which
    // is the whole thing this design exists to avoid.
    virtual void Update(float deltaSeconds) = 0;

    virtual const char* Name() const  = 0;
    virtual int         Order() const = 0;
};

class SystemScheduler {
public:
    // The scheduler does NOT own the system - it only borrows the pointer, so
    // a system must Unregister before it is destroyed.
    static void Register(System* system);
    static void Unregister(System* system);
    static void Clear();

    // Runs every system whose stage number is in [minOrder, maxOrder).
    static void UpdateRange(int minOrder, int maxOrder, float deltaSeconds);

    // Simulation stages, once per fixed step.
    static void Simulate(float fixedStepSeconds);
    // Render stages, once per drawn frame.
    static void RenderPass(float realDeltaSeconds);

    // Writes the running order to the log at start-up. Worth having the first
    // time something happens a frame later than expected.
    static void LogOrder();

    static void        ForEach(const std::function<void(System&)>& fn);
    static std::size_t Count();
};

} // namespace eng
