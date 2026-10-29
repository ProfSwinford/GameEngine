// ============================================================================
//  test_time.cpp - the clock, the two deltas, and which hooks a script has.
//
//  These three things are tested together because they are one mechanism seen
//  from three places: GameClock decides how many fixed steps a frame owes,
//  Time reports which of the two deltas the caller should be using, and the
//  hook table decides which of a script's functions get called at each rate.
//  A change to any one of them that the other two do not agree with is a bug,
//  and it is the kind that shows up as "my player moves at the wrong speed"
//  rather than as a crash.
//
//  ==========================================================================
//  NOTHING HERE STARTS THE ENGINE. Engine::Get() hands back a singleton that
//  is created on first use, with a default-constructed GameClock inside it -
//  so the clock can be driven directly without a window, a renderer or a
//  scene. That is what makes this a unit test rather than a game.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/Engine.h>
#include <engine/core/GameClock.h>
#include <engine/core/Time.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptHooks.h>

namespace {

// Puts the shared clock back how it was found.
//
// The engine singleton outlives any one test case, so a case that left the
// clock paused or scaled would silently change the meaning of every case that
// ran after it - and which ones those are depends on link order, which is the
// worst possible way to discover a test is order-dependent.
struct ClockGuard {
    eng::GameClock& clock = eng::Engine::Get().Clock();
    const float     scale = clock.TimeScale();
    const float     step  = clock.FixedStepSeconds();
    const bool      paused = clock.IsPaused();

    ~ClockGuard() {
        clock.SetTimeScale(scale);
        clock.SetFixedStepSeconds(step);
        clock.SetPaused(paused);
        eng::Time::EndFixedStep();
    }
};

// ---- scripts that exist only to be looked at by the compiler --------------

struct FixedOnly : eng::ScriptBehaviour {
    void OnFixedUpdate() {}
};

struct FrameOnly : eng::ScriptBehaviour {
    void OnUpdate() {}
};

struct BothUpdates : eng::ScriptBehaviour {
    void OnUpdate() {}
    void OnFixedUpdate() {}
};

struct CollisionOnly : eng::ScriptBehaviour {
    void OnCollisionEnter(eng::EntityId) {}
};

// The signature this engine used to have. It must NOT be mistaken for the new
// per-frame hook - see the migration note in ScriptHooks.h.
struct LegacyUpdate : eng::ScriptBehaviour {
    void OnUpdate(float) {}
};

} // namespace

// ---------------------------------------------------------------------------
//  Which hooks a script turns out to have
// ---------------------------------------------------------------------------

TEST_CASE("the hook table distinguishes the two updates") {
    const eng::ScriptHooks fixedOnly = eng::MakeScriptHooks<FixedOnly>();
    CHECK(fixedOnly.fixedUpdate != nullptr);
    CHECK(fixedOnly.update == nullptr);

    const eng::ScriptHooks frameOnly = eng::MakeScriptHooks<FrameOnly>();
    CHECK(frameOnly.update != nullptr);
    CHECK(frameOnly.fixedUpdate == nullptr);

    const eng::ScriptHooks both = eng::MakeScriptHooks<BothUpdates>();
    CHECK(both.update != nullptr);
    CHECK(both.fixedUpdate != nullptr);
}

TEST_CASE("a collision-only script has neither update") {
    // This is the property the two ticking lists are built on: such a script
    // must end up in neither of them, so it costs nothing per frame and
    // nothing per step.
    const eng::ScriptHooks hooks = eng::MakeScriptHooks<CollisionOnly>();
    CHECK(hooks.update == nullptr);
    CHECK(hooks.fixedUpdate == nullptr);
    CHECK(hooks.AnyCollision());
}

TEST_CASE("the old OnUpdate(float) is not picked up as either hook") {
    // The whole point of removing the argument. If this ever starts passing a
    // non-null pointer, every script written for the old engine silently
    // begins running at the wrong rate with no diagnostic anywhere.
    //
    // ENGINE_REGISTER_SCRIPT additionally refuses to compile such a class;
    // that cannot be checked from here, because a failing static_assert is a
    // broken build rather than a failing test.
    CHECK(!eng::hooks::HasOnUpdate<LegacyUpdate>);
    CHECK(eng::hooks::TakesDeltaOnUpdate<LegacyUpdate>);

    const eng::ScriptHooks hooks = eng::MakeScriptHooks<LegacyUpdate>();
    CHECK(hooks.update == nullptr);
    CHECK(hooks.fixedUpdate == nullptr);
}

TEST_CASE("DescribeHooks names both updates separately") {
    CHECK(eng::DescribeHooks(eng::MakeScriptHooks<FixedOnly>()) == "OnFixedUpdate");
    CHECK(eng::DescribeHooks(eng::MakeScriptHooks<FrameOnly>()) == "OnUpdate");
    CHECK(eng::DescribeHooks(eng::MakeScriptHooks<BothUpdates>()) ==
          "OnUpdate, OnFixedUpdate");
}

// ---------------------------------------------------------------------------
//  Which delta Time reports
// ---------------------------------------------------------------------------

TEST_CASE("DeltaTime answers for the phase it is called from") {
    ClockGuard      guard;
    eng::GameClock& clock = guard.clock;

    clock.SetPaused(false);
    clock.SetTimeScale(1.0f);
    clock.SetFixedStepSeconds(1.0f / 60.0f);

    // A frame that took much longer than one step, so the two deltas cannot
    // be confused with each other.
    clock.BeginFrame(0.05);

    eng::Time::BeginFixedStep();
    CHECK(eng::Time::InFixedStep());
    CHECK(eng::Time::DeltaTime() == doctest::Approx(1.0f / 60.0f));

    eng::Time::EndFixedStep();
    CHECK(!eng::Time::InFixedStep());
    CHECK(eng::Time::DeltaTime() == doctest::Approx(0.05f));
}

TEST_CASE("FixedDeltaTime does not depend on the phase") {
    ClockGuard guard;
    guard.clock.SetFixedStepSeconds(1.0f / 50.0f);
    guard.clock.BeginFrame(0.05);

    eng::Time::BeginFixedStep();
    const float inside = eng::Time::FixedDeltaTime();
    eng::Time::EndFixedStep();
    const float outside = eng::Time::FixedDeltaTime();

    CHECK(inside == doctest::Approx(1.0f / 50.0f));
    CHECK(inside == outside);
}

TEST_CASE("the time scale affects DeltaTime and not UnscaledDeltaTime") {
    ClockGuard guard;
    guard.clock.SetPaused(false);
    guard.clock.SetTimeScale(0.5f);
    guard.clock.BeginFrame(0.02);

    eng::Time::EndFixedStep();   // a drawn frame, not a simulation step
    CHECK(eng::Time::DeltaTime() == doctest::Approx(0.01f));
    CHECK(eng::Time::UnscaledDeltaTime() == doctest::Approx(0.02f));
}

TEST_CASE("at a time scale of zero DeltaTime stops and the real clock does not") {
    // The pause-menu case. A script animating on UnscaledDeltaTime keeps
    // moving while everything driven by DeltaTime is frozen.
    ClockGuard guard;
    guard.clock.SetPaused(false);
    guard.clock.SetTimeScale(0.0f);

    const int steps = guard.clock.BeginFrame(0.02);

    eng::Time::EndFixedStep();
    CHECK(steps == 0);   // nothing to simulate...
    CHECK(eng::Time::DeltaTime() == doctest::Approx(0.0f));
    CHECK(eng::Time::UnscaledDeltaTime() == doctest::Approx(0.02f));
}

// ---------------------------------------------------------------------------
//  The clock itself
// ---------------------------------------------------------------------------

TEST_CASE("frames and simulation steps are counted separately") {
    ClockGuard guard;
    guard.clock.SetPaused(false);
    guard.clock.SetTimeScale(1.0f);
    guard.clock.SetFixedStepSeconds(1.0f / 60.0f);

    const unsigned long long framesBefore = guard.clock.FrameCount();
    const unsigned long long ticksBefore  = guard.clock.TickCount();

    // Four frames of a 30 fps machine: each owes two steps of 1/60.
    int stepsRun = 0;
    for (int frame = 0; frame < 4; ++frame) {
        const int steps = guard.clock.BeginFrame(1.0 / 30.0);
        for (int step = 0; step < steps; ++step) {
            guard.clock.OnStepConsumed();
            ++stepsRun;
        }
    }

    CHECK(guard.clock.FrameCount() - framesBefore == 4);
    CHECK(stepsRun == 8);
    CHECK(guard.clock.TickCount() - ticksBefore == 8);
}

TEST_CASE("the same real time produces the same number of steps at any frame rate") {
    // THE CLAIM THE FIXED STEP IS FOR. One second of real time is 60 steps
    // whether the machine draws 30 frames in that second or 144 - which is
    // why movement written against the fixed step covers the same distance
    // per second on both.
    ClockGuard guard;
    guard.clock.SetPaused(false);
    guard.clock.SetTimeScale(1.0f);
    guard.clock.SetFixedStepSeconds(1.0f / 60.0f);

    const auto stepsInThreeSeconds = [&guard](int framesPerSecond) {
        guard.clock.Init();
        int total = 0;
        for (int frame = 0; frame < framesPerSecond * 3; ++frame) {
            total += guard.clock.BeginFrame(1.0 / framesPerSecond);
        }
        return total;
    };

    const int at30  = stepsInThreeSeconds(30);
    const int at60  = stepsInThreeSeconds(60);
    const int at144 = stepsInThreeSeconds(144);

    // THE CLAIM ITSELF: the frame rate does not change how much gets
    // simulated. Checked as equality between the three rather than against a
    // constant, because the constant is the part that is allowed to wobble.
    CHECK(at30 == at60);
    CHECK(at60 == at144);

    // And it is the right amount - within one step of three seconds' worth.
    //
    // NOT exactly 180, and the reason is worth knowing. The step is a float,
    // and 1.0f/60.0f is 0.016666668 - very slightly MORE than a sixtieth. So
    // a whole number of seconds holds a fraction less than 60 steps each,
    // and the shortfall sits in the accumulator waiting for the next frame
    // rather than being lost. The simulation does not drift; it just does not
    // land on a round number when you stop and count.
    CHECK(at60 >= 179);
    CHECK(at60 <= 180);
}

TEST_CASE("a paused clock owes no steps, and a single step owes exactly one") {
    ClockGuard guard;
    guard.clock.Init();
    guard.clock.SetPaused(true);

    CHECK(guard.clock.BeginFrame(1.0 / 60.0) == 0);

    guard.clock.RequestSingleStep();
    CHECK(guard.clock.BeginFrame(1.0 / 60.0) == 1);

    // And only one: the request is consumed rather than latched.
    CHECK(guard.clock.BeginFrame(1.0 / 60.0) == 0);
}
