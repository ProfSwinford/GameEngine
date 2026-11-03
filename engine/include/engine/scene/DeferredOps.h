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

class Entity;
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

    // The same, for when you are holding the thing rather than its id. Saves
    // writing QueueDestroy(entity.Id()) and, more to the point, saves the
    // Owner()-might-be-null dance when a script wants to remove itself.
    static void QueueDestroy(Entity& entity);

    // True between QueueDestroy and the moment the queue is applied. Systems
    // that must not act on something already dying check this - see rule 1.
    static bool IsPendingDestroy(EntityId id);

    // ========================================================================
    //  DESTROY IT LATER - Unity's Destroy(obj, t), and the usual way a bullet
    //  that hits nothing stops existing.
    //
    //  WHILE THE FUSE BURNS, NOTHING HAS CHANGED. The entity is not pending
    //  destruction, IsPendingDestroy is FALSE for it, and it collides, draws
    //  and updates exactly as it did before.
    //
    //  That sentence is the whole design, and the obvious implementation gets
    //  it wrong. Storing the fuse by marking the entity pending and checking
    //  the clock later would make IsPendingDestroy true for the entire delay -
    //  and CollisionSystem skips anything pending (rule 1, enforced in
    //  physics/Collider.cpp). So QueueDestroyAfter(bullet, 2.0f) would produce
    //  a bullet that passes straight through everything for two seconds and
    //  then vanishes. It would look like a collision-layer mistake, there
    //  would be no message, and the fuse is the last thing anybody would
    //  suspect. So fuses are a SEPARATE list and only move into the destroy
    //  queue when they run out.
    //
    //  ON THE GAME CLOCK, NOT THE WALL CLOCK. A fuse stops burning while the
    //  game is paused, because a grenade that goes off behind a pause menu is
    //  not what anybody means by a three-second fuse. Time::FixedDeltaTime is
    //  what advances it, which also makes it identical on every machine.
    //
    //  ASKING TWICE REPLACES THE FUSE rather than queueing a second one, so
    //  "reset the timer" is just asking again. A delay of zero or less is
    //  exactly QueueDestroy, so there is no edge case to remember.
    // ========================================================================
    static void QueueDestroyAfter(EntityId id, float delaySeconds);
    static void QueueDestroyAfter(Entity& entity, float delaySeconds);

    // How long until it goes, or a NEGATIVE number when nothing is waiting on
    // it. Negative rather than zero, because zero is a real answer - it means
    // "this tick" - and a caller cannot tell those apart otherwise.
    static float SecondsUntilDestroy(EntityId id);

    // Calls off a destroy that has not happened yet, fused or queued. False
    // when there was nothing to call off.
    //
    // THIS IS THE ONE PLACE THE FOUR RULES BEND, so it is worth saying why: a
    // shield that absorbs the killing blow later in the same step is an
    // ordinary thing to write, and without this the only way to express it is
    // to not queue the destroy in the first place and hope nothing else did.
    //
    // It cannot resurrect something already destroyed. Once Apply has run the
    // entity is gone and the id is stale, so this answers false - which is the
    // honest answer rather than a half-undo.
    static bool CancelQueuedDestroy(EntityId id);

    static std::size_t PendingFuseCount();

    // Advances the fuses, moving any that have run out into the destroy
    // queue. Called once per simulation step by Engine, immediately before
    // Apply, and never from inside a system's Update.
    //
    // `fixedStepSeconds` is the FIXED STEP, not a scaled delta. GameClock
    // applies the time scale to its accumulator, so the step itself is
    // constant and multiplying by TimeScale again here would scale it twice.
    static void TickFuses(float fixedStepSeconds);

    // Applies everything queued. Called once per simulation step, at stage
    // 600, and never from inside a system's Update.
    static void Apply(Scene& scene);

    static void Clear();

    static std::size_t PendingDestroyCount();
};

} // namespace eng
