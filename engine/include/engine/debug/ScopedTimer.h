#pragma once

// =============================================================================
//  WEEK 4 - the instrument the engine uses on itself. Ch. 10.8.
//
//  From this week on, "is that slow?" has a number for an answer rather than
//  an opinion.
//
//  RAII again, and not a coincidence: the timer starts in its constructor and
//  stops in its destructor, so it measures exactly the scope it was declared
//  in - INCLUDING every early return out of that scope. You cannot build this
//  reliably in C# without a `using` block and the discipline to always write
//  one.
//
//  ---------------------------------------------------------------------------
//  WHICH CLOCK: std::chrono::steady_clock.
//
//  The three are system_clock, steady_clock and high_resolution_clock.
//  system_clock is the wall clock and CAN GO BACKWARDS - NTP corrects it, the
//  user changes the timezone - which makes it useless for elapsed time and
//  capable of producing negative durations. high_resolution_clock is permitted
//  to be an alias for either, and on MSVC it is steady_clock while on some
//  libstdc++ configurations it is system_clock; "it depends which standard
//  library you built with" is not a property you want in a measurement tool.
//  steady_clock is the only one guaranteed monotonic, and its resolution on
//  every platform this course targets is well under a microsecond.
//
//  ---------------------------------------------------------------------------
//  COST: measured, not assumed. An empty ENGINE_SCOPED_TIMER scope costs about
//  55 ns on this machine, of which the map lookup in Submit is the bulk. That
//  is 0.3% of a 16.6 ms frame at 1,000 timed scopes per frame, so it is cheap
//  enough to leave in - which is the requirement. The method and the number
//  are in docs/week04-layout-report.md, follow-up 4.
// =============================================================================

#include <engine/core/Types.h>

#include <chrono>
#include <functional>

namespace eng {

// Accumulated statistics for one named measurement site.
struct TimerStats {
    f64 minMs   = 0.0;
    f64 maxMs   = 0.0;
    f64 totalMs = 0.0;
    u64 samples = 0;

    f64 AverageMs() const { return samples ? totalMs / static_cast<f64>(samples) : 0.0; }
};

class TimerRegistry {
public:
    // Records one sample against a named site. `name` must outlive the
    // registry - a string literal, which every call site uses.
    static void Submit(const char* name, f64 elapsedMs);

    // Reports every site through the Week 3 logger: name, min, average, max,
    // sample count. Called at shutdown at minimum.
    static void Report();

    // Clears all accumulated statistics. Week 10's per-frame HUD calls this;
    // the editor's Profiler panel has a button for it.
    static void Reset();

    static TimerStats Get(const char* name);

    // Enumeration, so the Profiler panel can build its table without being
    // told what to ask for. Same lesson as the CVar registry in Week 8: a
    // registry that only answers lookups cannot be browsed.
    static void ForEach(const std::function<void(const char*, const TimerStats&)>& fn);
    static usize SiteCount();

    // A ring of the last N frame times, for the Profiler panel's plot.
    //
    // THE PLOT EARNS ITS PLACE because an average hides the thing you care
    // about: a frame that takes 4 ms ninety-nine times and 40 ms once has a
    // fine average and a visible stutter. Same argument as reporting min/avg/
    // max rather than a single number, rendered.
    static void SubmitFrameTime(f32 milliseconds);
    static usize FrameHistoryCount();
    static const f32* FrameHistory();   // oldest first, FrameHistoryCount entries
    static constexpr usize kFrameHistoryCapacity = 240;   // four seconds at 60 Hz
};

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name)
        : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        const auto elapsed = std::chrono::steady_clock::now() - m_start;
        const f64  ms = std::chrono::duration<f64, std::milli>(elapsed).count();
        TimerRegistry::Submit(m_name, ms);
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char*                           m_name = nullptr;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace eng

// -----------------------------------------------------------------------------
//  ENGINE_SCOPED_TIMER("name") - instrumenting a scope is one short line.
//
//  Two requirements, both met:
//
//   1. TWO TIMERS IN ONE SCOPE MUST NOT COLLIDE. The generated variable name
//      is built from __LINE__ with the token-pasting operator ##, so two
//      timers on different lines get different names. The two-level macro
//      (ENGINE_TIMER_CONCAT -> ENGINE_TIMER_CONCAT_) is required because ##
//      suppresses macro expansion of its operands: with one level you get a
//      variable literally called `timer__LINE__`, and the second timer in the
//      scope is a redefinition error. Same family of trick as the Week 3
//      assert macro.
//
//   2. IT MUST BE POSSIBLE TO COMPILE THEM ALL OUT. Define
//      ENGINE_DISABLE_TIMERS and every one becomes nothing, the same way
//      asserts do in release. They are left ON in release by default, because
//      Week 4's whole argument is that the numbers you want are the optimised
//      ones.
// -----------------------------------------------------------------------------
#define ENGINE_TIMER_CONCAT_(a, b) a##b
#define ENGINE_TIMER_CONCAT(a, b)  ENGINE_TIMER_CONCAT_(a, b)

#ifdef ENGINE_DISABLE_TIMERS
    #define ENGINE_SCOPED_TIMER(name) do { } while (false)
#else
    #define ENGINE_SCOPED_TIMER(name)                                          \
        ::eng::ScopedTimer ENGINE_TIMER_CONCAT(engineScopedTimer_, __LINE__)(name)
#endif
