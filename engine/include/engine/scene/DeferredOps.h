#pragma once

// ============================================================================
//  DeferredOps.h - creating and destroying entities SAFELY, at one known point
//  in the frame.
//
//  ==========================================================================
//  THE PROBLEM
//
//  A system is walking its list of components. One entity's update spawns a
//  bullet and destroys an enemy. Both of those change the very lists that are
//  currently being walked.
//
//  In some languages, modifying a collection while looping over it raises an
//  error immediately. In C++ it does not. Adding to a std::vector can move all
//  of its contents somewhere else in memory, and the loop carries on reading
//  the old location. Sometimes that works. Sometimes it reads a destroyed
//  object. Sometimes it works for months and then stops.
//
//  Concretely here: removing a sprite moves the LAST entry in the render list
//  into the hole. If that happens mid-draw, one sprite gets drawn twice and
//  another gets skipped. That is the mild version.
//
//  THE FIX
//  Nothing structural happens immediately. Spawns and destroys go into QUEUES
//  which are applied at ONE point - stage 600 in the system order, after every
//  system has finished and after messages have been delivered.
//  ==========================================================================
//
//  FOUR RULES, WRITTEN DOWN
//
//  1. AN ENTITY DESTROYED THIS FRAME STILL EXISTS FOR THE REST OF IT.
//     It still draws (one extra frame of a dead thing is invisible at 60
//     frames per second) and it still updates (its own update is what asked
//     to be destroyed; cutting it off halfway would leave whatever it was
//     doing half done). It does NOT collide - "destroyed but still hurting
//     you" is a genuinely confusing bug.
//
//  2. AN ENTITY SPAWNED THIS FRAME STARTS NEXT FRAME. It is created after
//     every system has run, so its first update is on the following tick.
//
//  3. DESTROYING SOMETHING TWICE IS HARMLESS. Game code does this constantly -
//     two bullets hit the same enemy on the same tick - so the queue ignores
//     the duplicate instead of treating it as an error.
//
//  4. THE QUEUE IS DRAINED ONCE PER TICK. Anything spawned WHILE draining
//     happens next frame instead. Draining repeatedly until empty risks never
//     finishing, because something that spawns a copy of itself is a perfectly
//     reasonable thing to write.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/scene/EntityId.h>

namespace eng {

class Scene;

class DeferredOps {
public:
    // Ask for an entity to be destroyed. It happens at stage 600, not now.
    //
    // To CREATE an entity, call Scene::CreateEntity directly. Creating is safe
    // at any time because it only ever appends; destroying is the dangerous
    // half, because it removes entries from the very lists systems are
    // walking, and that is what this queue exists to make safe.
    static void QueueDestroy(EntityId id);

    // True between QueueDestroy and the moment the queue is applied. Systems
    // that must not act on something already dying check this - see rule 1.
    static bool IsPendingDestroy(EntityId id);

    // Applies everything queued. Called once per simulation step, at stage
    // 600, and never from inside a system's Update.
    static void Apply(Scene& scene);

    static void Clear();

    static std::size_t PendingDestroyCount();
};

} // namespace eng
