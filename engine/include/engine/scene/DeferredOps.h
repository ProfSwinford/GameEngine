#pragma once

// =============================================================================
//  WEEK 10 - deferred spawn and destroy. Ch. 8.6.
//
//  ---------------------------------------------------------------------------
//  THE PROBLEM, stated precisely because it is the subtlest thing this week.
//
//  A system is iterating its component array. One entity's update spawns a
//  bullet and destroys an enemy. Both operations modify the arrays that are
//  CURRENTLY BEING ITERATED.
//
//  In C#, modifying a collection during foreach throws
//  InvalidOperationException and you find out immediately. In C++, mutating a
//  container invalidates iterators and the loop keeps running over memory that
//  may have been reallocated or shuffled. Sometimes it works. Sometimes it
//  reads a destroyed object. Sometimes it works for months and then does not.
//
//  Concretely in THIS engine: SpriteRenderSystem::Unregister does a swap-and-
//  pop, so destroying an entity mid-render-iteration moves a record the loop
//  has already passed into a slot it has not, and one sprite is drawn twice
//  while another is skipped. That is the mild version.
//
//  This is the C# safety net that is gone, and it is arguably the single most
//  dangerous difference between the two languages for a gameplay programmer.
//
//  THE FIX: nothing structural happens immediately. Spawn and destroy go into
//  QUEUES applied at one defined point - stage 600 in the system order, after
//  every simulation system and after message dispatch.
//
// =============================================================================
//  *** THE FOUR DECISIONS, WRITTEN DOWN. Every one shows up in the
//      1000-frame stress test. ***
//
//  1. AN ENTITY DESTROYED THIS FRAME STILL EXISTS FOR THE REST OF IT. It still
//     RENDERS (one extra frame of a dead thing is invisible at 60 Hz) and it
//     still UPDATES (its own update is what asked to be destroyed, and cutting
//     it short mid-tick would half-apply whatever it was doing). It does NOT
//     COLLIDE: the collision system skips anything IsPendingDestroy, because
//     "destroyed but still colliding" produces an enemy that goes on damaging
//     the player after it visibly died, and that is a genuinely confusing bug.
//
//  2. AN ENTITY SPAWNED THIS FRAME STARTS NEXT FRAME. It is created at the
//     drain point, which is after every simulation system has run, so its
//     first update is the following tick. Simpler, and it is the usual answer.
//
//  3. DESTROYING SOMETHING ALREADY DESTROYED THIS FRAME IS HARMLESS. Gameplay
//     does this constantly - two bullets hit the same enemy in the same tick -
//     so it is deduplicated on queue and re-checked on apply. It does not
//     assert. An assert here would fire during ordinary correct gameplay.
//
//  4. THE QUEUE IS DRAINED ONCE. Spawns created WHILE draining go to the NEXT
//     frame. Draining in a loop until empty risks never terminating - a spawn
//     that spawns is a legitimate thing to write - and one frame of latency on
//     a chain reaction is not observable.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/math/Vec2.h>
#include <engine/scene/Entity.h>

#include <functional>
#include <string>

namespace eng {

class Scene;

class DeferredOps {
public:
    struct SpawnParams {
        StringId    prefab;                 // a "prefabs" entry in the scene file
        std::string name;                   // must be unique; a suffix is added if not
        Vec2        position{0.0f, 0.0f};
        f32         rotation = 0.0f;
        Vec2        scale{1.0f, 1.0f};
    };

    // Queue a spawn from a prefab name and a transform.
    static void QueueSpawn(const SpawnParams& params);

    // Queue a spawn built by a callback. The callback runs at the drain point,
    // on the main thread, with the scene in a state where structural change is
    // safe. This is what gameplay code in the sandbox uses when its entities
    // are not worth a prefab entry - and it is public API, so the gate
    // exercise never needed to touch engine internals to spawn anything.
    using SpawnBuilder = std::function<EntityHandle(Scene&)>;
    static void QueueSpawn(SpawnBuilder builder);

    static void QueueDestroy(EntityHandle handle);

    // True between the QueueDestroy and the drain. Systems that must not act
    // on a dying entity check this - see decision 1.
    static bool IsPendingDestroy(EntityHandle handle);

    // Applies every queued operation. Called ONCE per simulation step, at
    // stage 600, and never from inside a system update.
    static void Apply(Scene& scene);

    static void Clear();

    // Instrumentation for the editor and for the stress test.
    static usize PendingSpawnCount();
    static usize PendingDestroyCount();
    static u64   TotalSpawned();
    static u64   TotalDestroyed();
};

} // namespace eng
