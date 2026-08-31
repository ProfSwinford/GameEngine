#pragma once

// ============================================================================
//  GameClock.h - how time is measured, and how often the game is simulated.
//
//  ==========================================================================
//  WHY THE SIMULATION RUNS AT A FIXED RATE
//
//  The obvious game loop advances the world by however much real time went by
//  since the last frame. That means the physics behaves differently at 30
//  frames per second than at 144: objects pass through walls on a slow
//  machine, jumps reach different heights, and a bug on a classmate's laptop
//  cannot be reproduced on yours.
//
//  The fix is to separate the two rates. Real time is collected in an
//  "accumulator", and the world is advanced in identical fixed-size steps:
//
//      accumulator += realTimeThisFrame
//      while (accumulator >= step) { Simulate(step); accumulator -= step; }
//      Draw()
//
//  Draw as often as you can; simulate at a fixed rate. Every step is the same
//  size on every machine, so the game behaves identically everywhere.
//
//  ==========================================================================
//  THE RUNAWAY PROBLEM, AND THE LIMIT
//
//  If one simulation step takes longer than the slice of time it represents,
//  the accumulator grows faster than the loop can drain it. The next frame
//  runs more steps, which takes even longer, and the program locks up
//  completely and never recovers.
//
//  MaxStepsPerFrame puts a ceiling on it, and any time left over is THROWN
//  AWAY - so the simulation falls behind real time. That is the right trade: a
//  game briefly running in slow motion is recoverable, a frozen one is not.
//  Every time it happens is written to the log, because silently running in
//  slow motion is its own baffling bug.
//
//  ==========================================================================
//  TWO DIFFERENT CLOCKS, AND THEY ARE GENUINELY DIFFERENT THINGS
//
//    REAL time - the wall clock. Never pauses, never speeds up or slows down.
//    GAME time - affected by pause and by the time-scale slider.
//
//  The accessors are named so that using the wrong one has to be deliberate:
//  RealSeconds() and GameSeconds(), never Time() and OtherTime().
// ============================================================================

namespace eng {

class GameClock {
public:
    void Init();

    // Called once per frame with how much real time has passed. Returns HOW
    // MANY simulation steps should run this frame - which may be 0, 1, or
    // several, and is capped by MaxStepsPerFrame.
    int BeginFrame(double realDeltaSeconds);

    // Called by the engine after each simulation step, so the clock can move
    // game time forward and count the tick.
    void OnStepConsumed();

    // ---- control (this is what the editor's toolbar drives) ---------------

    // 1.0 is normal speed, 0.5 is half speed, 2.0 is double.
    //
    // Notice what this does NOT do: it does not change the size of a step. It
    // changes how much time is collected per frame, so FEWER OR MORE steps
    // run. The steps themselves are always the same size, which is the whole
    // point of the design.
    void  SetTimeScale(float scale);
    float TimeScale() const { return m_timeScale; }

    void Pause()  { m_paused = true; }
    void Resume() { m_paused = false; }
    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }

    // Advances EXACTLY ONE step while paused. Not roughly one - stepping ten
    // times advances exactly ten ticks.
    //
    // This is the best debugging tool in the engine: it lets you watch a
    // collision happen one tick at a time.
    void RequestSingleStep() { m_singleStepRequested = true; }

    // ---- reading the time -------------------------------------------------
    double RealSeconds() const      { return m_realSeconds; }
    double GameSeconds() const      { return m_gameSeconds; }
    float  RealDeltaSeconds() const { return m_realDelta; }

    // The size of one simulation step, in seconds. This is the value handed to
    // every Update, and it never varies - that is the point.
    float FixedStepSeconds() const { return m_fixedStep; }
    void  SetFixedStepSeconds(float seconds);

    // How many simulation steps have run since the program started.
    unsigned long long TickCount() const { return m_ticks; }

    int  MaxStepsPerFrame() const { return m_maxSteps; }
    void SetMaxStepsPerFrame(int steps);

private:
    float m_fixedStep = 1.0f / 60.0f;   // 60 simulation steps per second
    float m_timeScale = 1.0f;
    int   m_maxSteps  = 5;

    double m_accumulator = 0.0;   // real time collected but not yet simulated
    double m_realSeconds = 0.0;
    double m_gameSeconds = 0.0;
    float  m_realDelta   = 0.0f;

    unsigned long long m_ticks = 0;

    bool m_paused              = false;
    bool m_singleStepRequested = false;
};

} // namespace eng
