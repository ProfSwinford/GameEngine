// WEEK 10 - the fixed timestep. See GameClock.h.

#include <engine/core/GameClock.h>
#include <engine/core/Log.h>

#include <algorithm>

namespace eng {

void GameClock::Init() {
    m_accumulator = 0.0;
    m_realSeconds = 0.0;
    m_gameSeconds = 0.0;
    m_ticks       = 0;
    m_clampEvents = 0;
}

void GameClock::SetFixedStepSeconds(f32 seconds) {
    // Clamped away from zero: a fixed step of 0 makes the while loop in
    // BeginFrame infinite, which is the spiral of death arrived at by a
    // different road.
    m_fixedStep = std::clamp(seconds, 1.0f / 1000.0f, 1.0f);
}

void GameClock::SetTimeScale(f32 scale) {
    m_timeScale = std::clamp(scale, 0.0f, 16.0f);
}

void GameClock::SetMaxStepsPerFrame(i32 steps) {
    m_maxSteps = std::clamp(steps, 1, 60);
}

i32 GameClock::BeginFrame(f64 realDeltaSeconds) {
    // REAL time advances unconditionally. It is not affected by pause, by time
    // scale, or by the clamp - that is what makes it the timeline a profiler
    // can trust.
    m_realDelta    = static_cast<f32>(realDeltaSeconds);
    m_realSeconds += realDeltaSeconds;

    if (m_paused) {
        // SINGLE STEP: exactly one tick, and the accumulator is left alone so
        // that resuming does not suddenly owe a burst of steps. Stepping the
        // accumulator instead is the bug where single-step advances a variable
        // amount.
        if (m_singleStepRequested) {
            m_singleStepRequested = false;
            return 1;
        }
        return 0;
    }

    // GAME time is real time through the time scale. Scale 0.5 halves how much
    // is accumulated per frame, so half as many fixed steps run - the steps
    // themselves never change size, which is the property the whole design
    // exists to protect.
    m_accumulator += realDeltaSeconds * static_cast<f64>(m_timeScale);

    i32 steps = 0;
    while (m_accumulator >= static_cast<f64>(m_fixedStep)) {
        m_accumulator -= static_cast<f64>(m_fixedStep);
        ++steps;
        if (steps >= m_maxSteps) {
            break;
        }
    }

    // THE CLAMP. If the accumulator still owes more than a step after the
    // limit, the surplus is DISCARDED and the simulation falls behind real
    // time. Logged every time, because silently running in slow motion is its
    // own confusing bug.
    if (m_accumulator >= static_cast<f64>(m_fixedStep)) {
        ++m_clampEvents;
        ENGINE_LOG_WARN(Channels::kCore,
                        "frame clamped at {} simulation steps; discarding {:.1f} ms of "
                        "accumulated time (the simulation is now behind real time)",
                        m_maxSteps, m_accumulator * 1000.0);
        m_accumulator = 0.0;
    }

    return steps;
}

void GameClock::OnStepConsumed() {
    m_gameSeconds += static_cast<f64>(m_fixedStep);
    ++m_ticks;
}

f32 GameClock::Alpha() const {
    return static_cast<f32>(m_accumulator / static_cast<f64>(m_fixedStep));
}

} // namespace eng
