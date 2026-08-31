#pragma once

// =============================================================================
//  WEEK 10 - the fixed timestep. Ch. 8.5. This replaces Week 1's naive loop.
//
//  Nine weeks is a long time to live with a placeholder, and it was
//  deliberate: by now the reason the naive version is wrong is obvious.
//
//  ---------------------------------------------------------------------------
//  WHY A FIXED TIMESTEP. The naive loop steps the simulation once per rendered
//  frame by however much real time elapsed, so physics behaves differently at
//  30 and 144 FPS - objects tunnel through walls on slow machines, jumps reach
//  different heights, and a bug on a classmate's laptop cannot be reproduced.
//
//  The accumulator:
//      accumulator += realElapsed
//      while (accumulator >= kFixedStep) { Simulate(kFixedStep); accumulator -= kFixedStep; }
//      Render()
//
//  Simulation advances in identical discrete steps regardless of frame rate.
//  Render as often as you can; simulate at a fixed rate.
//
//  ---------------------------------------------------------------------------
//  THE SPIRAL OF DEATH, and the clamp.
//
//  If one simulation step takes longer than the step it represents, the
//  accumulator grows faster than the loop drains it, so the next frame does
//  more steps, which takes longer still. The engine locks up hard and never
//  recovers.
//
//  MaxStepsPerFrame clamps it and the surplus accumulator is DISCARDED, which
//  means the simulation falls behind real time. That is the correct trade: a
//  game running in slow motion is recoverable, a frozen one is not. Every
//  clamp is logged at Warning, because silently running in slow motion is its
//  own confusing bug.
//
//  ---------------------------------------------------------------------------
//  THREE TIMELINES, and they are genuinely different things:
//
//    REAL time  - wall clock. Never pauses, never scales. Profilers use it.
//    GAME time  - affected by pause and time scale. Gameplay uses it.
//    LOCAL time - per-entity, so one object can be slowed while the world runs
//                 normally. Exposed as a scale a caller multiplies by, rather
//                 than as a clock per entity, which would be a clock per
//                 entity.
//
//  The accessors are named so that using the wrong one has to be deliberate:
//  RealSeconds() and GameSeconds(), never Time() and OtherTime().
// =============================================================================

#include <engine/core/Types.h>

namespace eng {

class GameClock {
public:
    void Init();

    // Called once per frame with real elapsed seconds. Returns HOW MANY
    // simulation steps to run - clamped, see the spiral-of-death note.
    i32 BeginFrame(f64 realDeltaSeconds);

    // Called by the engine after each simulation step, so the clock can
    // advance game time and the tick counter.
    void OnStepConsumed();

    // --- control ----------------------------------------------------------
    void SetTimeScale(f32 scale);
    f32  TimeScale() const { return m_timeScale; }

    void Pause()  { m_paused = true; }
    void Resume() { m_paused = false; }
    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }

    // Advances EXACTLY ONE simulation tick while paused. Not "approximately
    // one" - BeginFrame returns exactly 1 and the accumulator is untouched, so
    // stepping ten times advances exactly ten ticks. Stepping the accumulator
    // instead is the bug where single-step advances a variable amount.
    //
    // This is the best debugging tool in the engine: it lets you watch a
    // collision happen one tick at a time.
    void RequestSingleStep() { m_singleStepRequested = true; }

    // --- the three timelines ----------------------------------------------
    f64 RealSeconds() const { return m_realSeconds; }
    f64 GameSeconds() const { return m_gameSeconds; }
    f32 RealDeltaSeconds() const { return m_realDelta; }

    // The step size, in GAME seconds. This is what a simulation step is handed
    // and it never varies - that is the whole point.
    f32 FixedStepSeconds() const { return m_fixedStep; }
    void SetFixedStepSeconds(f32 seconds);

    // A per-entity clock is a scale applied to the fixed step by whatever is
    // integrating. Kept as a helper rather than as state, because state would
    // mean a clock per entity.
    f32 LocalStepSeconds(f32 localTimeScale) const { return m_fixedStep * localTimeScale; }

    u64 TickCount() const { return m_ticks; }

    i32  MaxStepsPerFrame() const { return m_maxSteps; }
    void SetMaxStepsPerFrame(i32 steps);
    u64  ClampEventCount() const { return m_clampEvents; }

    // accumulator / fixedStep, in [0, 1). Lets the renderer interpolate
    // BETWEEN simulation ticks instead of snapping to them.
    //
    // IMPLEMENTED AND EXPOSED, BUT NOT USED BY THE DEFAULT RENDER PATH, and
    // that is noted rather than left silent: interpolating means drawing every
    // sprite at a position no simulation tick ever held, which makes a paused
    // single-step comparison between the Inspector's numbers and the screen
    // disagree by up to one step. For an engine whose main debugging tool this
    // week is pause-and-step, that trade is not worth the smoothness. A
    // gameplay layer that wants it can multiply by Alpha() itself.
    f32 Alpha() const;

private:
    f32 m_fixedStep = 1.0f / 60.0f;
    f32 m_timeScale = 1.0f;
    i32 m_maxSteps  = 5;

    f64 m_accumulator = 0.0;
    f64 m_realSeconds = 0.0;
    f64 m_gameSeconds = 0.0;
    f32 m_realDelta   = 0.0f;
    u64 m_ticks       = 0;
    u64 m_clampEvents = 0;

    bool m_paused = false;
    bool m_singleStepRequested = false;
};

} // namespace eng
