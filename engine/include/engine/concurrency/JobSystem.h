#pragma once

// =============================================================================
//  WEEK 5 - worker threads with a lifecycle.
//
//  They do not need real work yet; what they need is to come up AFTER the
//  logger and go down BEFORE it, and to make the engine EXIT. Not "usually
//  exit". If shutdown hangs even occasionally, Stop() is not doing its job and
//  the Week 9 bug has been found three weeks early, which is the best possible
//  outcome.
//
//  Week 9 gives them real work: FileSystem::ReadFileAsync pushes reads here.
//
//  ---------------------------------------------------------------------------
//  THE COUNTERS ARE ATOMIC, AND THE REASON IS THE JOBS PANEL.
//
//  The main thread draws that panel while these threads mutate the very
//  numbers it reads. That is textbook shared mutable state and reading them
//  naively is a data race the thread sanitizer will report - even though, on
//  x86, an unsynchronised u64 read would "work". Atomics with relaxed
//  ordering cost nothing here (the counters have no ordering relationship with
//  anything else) and make the race go away for real rather than by luck.
//
//  The panel also must NOT hold the queue's mutex while drawing: a UI that
//  blocks producers changes the behaviour it claims to observe, which is the
//  worst kind of debug tool. Everything below is either an atomic load or a
//  copy taken under a lock that is released before returning.
// =============================================================================

#include <engine/core/Types.h>

#include <functional>
#include <string>
#include <vector>

namespace eng {

using Job = std::function<void()>;

class JobSystem {
public:
    // Spawns `workerCount` threads. 0 means "one per hardware thread, less
    // one for the main thread", clamped to at least one.
    static bool Init(u32 workerCount);

    // Stops the queue, joins every worker. Safe to call twice.
    static void Shutdown();

    static bool IsRunning();

    static void Enqueue(Job job);

    // Blocks until the queue is empty AND no worker is mid-job. Used by tests
    // and by scene loading; NOT by the frame loop, where blocking the main
    // thread on workers defeats the point.
    static void WaitForIdle();

    // --- instrumentation for the Jobs panel --------------------------------
    static u32   WorkerCount();
    static bool  IsWorkerBusy(u32 index);
    static usize QueueDepth();          // SizeApprox: display only, never branch
    static u64   JobsPushed();
    static u64   JobsCompleted();

    // The three numbers from the Week 5 platform measurement report, measured
    // once at startup and displayed by the panel so they are always at hand.
    struct PlatformCosts {
        f64 threadCreateJoinMicros = 0.0;
        f64 contextSwitchMicros    = 0.0;
        f64 firstTouchPerPageMicros = 0.0;
        bool measured = false;
    };
    static void                 SetPlatformCosts(const PlatformCosts& costs);
    static const PlatformCosts& GetPlatformCosts();
};

} // namespace eng
