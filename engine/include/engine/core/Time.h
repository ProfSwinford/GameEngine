#pragma once

// ============================================================================
//  Time.h - what time it is, asked from anywhere.
//
//  Scripts used to be HANDED their delta, as the argument to OnUpdate(float).
//  That works inside OnUpdate and nowhere else: a helper function called from
//  it had to have the number threaded down through every layer, and
//  OnCollisionEnter - which is handed an EntityId and nothing else - could not
//  find out how long the last frame took at all.
//
//  So the delta is no longer an argument. It is asked for:
//
//      void OnUpdate() { m_fade += Time::DeltaTime(); }
//
//  This is Unity's Time class, with Unity's names, for the same reason Unity
//  has one.
//
//  ==========================================================================
//  THERE ARE TWO DELTAS. THEY ARE DIFFERENT NUMBERS. THIS IS THE WHOLE FILE.
//
//  The engine draws as fast as the machine allows and simulates at a steady
//  rate, which means two different clocks are ticking:
//
//    THE FRAME        How long the last drawn frame took. Varies constantly -
//                     6.9 ms at 144 fps, 33.3 ms at 30, and a different
//                     number again the moment something stutters.
//                     -> OnUpdate(), once per drawn frame.
//
//    THE FIXED STEP   Always 1/60 of a second, on every machine, forever.
//                     Runs a whole number of times per frame: twice at 30 fps,
//                     usually zero and occasionally once at 144.
//                     -> OnFixedUpdate(), for anything physical.
//
//  Using the wrong one is the classic bug in every engine that has both, so
//  DeltaTime() ANSWERS FOR WHERE YOU ARE: inside OnFixedUpdate it is the fixed
//  step, and everywhere else it is the frame. The same line of movement code
//  is therefore correct in both hooks, which is exactly the property that
//  makes it safe to move code from one to the other.
//
//  When you want to be explicit - and in a physics calculation you should be -
//  FixedDeltaTime() and UnscaledDeltaTime() always mean one specific thing
//  regardless of where they are called from.
//
//  ==========================================================================
//  SCALED AND UNSCALED, AND WHY BOTH ARE NEEDED
//
//  The time scale is what Slow Motion and Pause are built out of. DeltaTime()
//  is affected by it; UnscaledDeltaTime() is not, and that difference is the
//  whole reason a pause menu can animate on top of a frozen game:
//
//      void OnUpdate() {
//          m_menuSlide += Time::UnscaledDeltaTime() * 4.0f;  // keeps moving
//          m_bulletX   += Time::DeltaTime() * 300.0f;        // frozen at scale 0
//      }
//
//  AT A TIME SCALE OF ZERO, OnUpdate STILL RUNS and DeltaTime() returns 0,
//  while OnFixedUpdate does not run at all - because no simulation time is
//  being collected for it to run on. That is Unity's behaviour, and it is the
//  behaviour that makes the two-hook split worth having.
//
//  The editor's Pause button is a different thing again: it stops BOTH hooks,
//  the way pausing in the Unity editor does, because the editor is not a
//  paused game - it is not a running game at all.
//
//  ==========================================================================
//  MAPPING FROM UNITY, for anyone arriving with the habit:
//
//    Time.deltaTime          -> Time::DeltaTime()
//    Time.fixedDeltaTime     -> Time::FixedDeltaTime()
//    Time.unscaledDeltaTime  -> Time::UnscaledDeltaTime()
//    Time.time               -> Time::ElapsedTime()
//    Time.unscaledTime       -> Time::UnscaledElapsedTime()
//    Time.timeScale          -> Time::TimeScale() / GameClock::SetTimeScale()
//    Time.frameCount         -> Time::FrameCount()
//    (no equivalent)         -> Time::TickCount(), simulation steps run
//
//  `time` could not keep its name: Time::Time() would declare a constructor.
//
//  ==========================================================================
//  THIS IS A READER. Everything here is const in spirit - it tells you what
//  the clock says and changes nothing. Pausing, stepping and setting the time
//  scale belong to GameClock, reached through Engine::Get().Clock(), because
//  those are decisions about how the game runs rather than questions about it.
// ============================================================================

namespace eng {

class Time {
public:
    // ---- the deltas -------------------------------------------------------

    // The right delta for wherever you are: the fixed step inside
    // OnFixedUpdate, the frame time everywhere else. Scaled by the time
    // scale, so it is 0 while the game is slowed to a stop.
    static float DeltaTime();

    // The simulation step, always - 1/60 of a second unless the config says
    // otherwise. Does not depend on where it is called from, which is what
    // makes it the honest choice in a physics calculation.
    static float FixedDeltaTime();

    // Real seconds the last drawn frame took, ignoring the time scale. This
    // is the one for anything that must keep moving while the game is paused
    // or slowed: menus, transitions, a cursor.
    static float UnscaledDeltaTime();

    // ---- the timelines ----------------------------------------------------

    // Game time: the sum of every simulation step that has run. Stops dead
    // while paused and runs slow at half speed, so two entities that measure
    // their age from it stay in agreement with each other and with the
    // physics.
    static double ElapsedTime();

    // Wall-clock seconds since start-up. Never pauses, never scales. For
    // measuring how long something actually took.
    static double UnscaledElapsedTime();

    // ---- counters ---------------------------------------------------------

    // Frames DRAWN since start-up, and simulation steps RUN since start-up.
    // They are different numbers and the gap between them is the thing this
    // whole file is about.
    static unsigned long long FrameCount();
    static unsigned long long TickCount();

    // ---- the state of the clock -------------------------------------------

    static float TimeScale();
    static bool  IsPaused();

    // Whether this code is currently running inside a fixed simulation step.
    // What DeltaTime() branches on, exposed because a helper shared by both
    // hooks occasionally has a genuine reason to care.
    static bool InFixedStep();

    // ---- called by Engine only --------------------------------------------
    //
    // Engine::Simulate brackets each fixed step with these, which is what
    // lets DeltaTime() know which answer to give. Nothing else should call
    // them - see GameClock::OnStepConsumed for the same arrangement.
    static void BeginFixedStep();
    static void EndFixedStep();
};

} // namespace eng
