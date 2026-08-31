#pragma once
// =============================================================================
//  WEEK 5 PANEL - worker threads and queue depth.
//
//  Worker count and each thread's state, queue depth plotted over time,
//  running totals of items pushed and completed, and the three platform
//  measurements displayed once so they are always at hand.
//
//  ---------------------------------------------------------------------------
//  *** THIS PANEL IS A RACE CONDITION WAITING TO HAPPEN, WHICH IS WHY IT IS
//  ASSIGNED. ***
//
//  The main thread draws it. Worker threads mutate the very counters it reads.
//  That is textbook shared mutable state, and reading them naively is a data
//  race the thread sanitizer reports even though it would appear to "work" on
//  x86.
//
//  What this panel does about it:
//   - THE COUNTERS ARE ATOMICS, inside JobSystem. Every accessor here is an
//     atomic load with relaxed ordering.
//   - IT NEVER HOLDS THE QUEUE'S MUTEX WHILE DRAWING. QueueDepth() takes the
//     lock, reads a size, and releases it before returning. A UI that blocked
//     producers would change the behaviour it claims to observe.
//   - SizeApprox() IS DISPLAYED, NEVER BRANCHED ON. Displaying a value that
//     may be one item stale is honest; making a decision on one is not - and
//     that distinction is exactly what its comment in ThreadSafeQueue.h says.
//
//  This is the first panel where "the tool is part of the system" stops being
//  an abstract point.
//
//  The history ring is FIXED SIZE for the same reason the Memory panel's is:
//  a debug panel that grows every frame becomes the leak it exists to detect.
// =============================================================================
#include "Panel.h"

#include <engine/core/Types.h>

namespace editor {

class JobPanel final : public Panel {
public:
    const char* Title() const override { return "Jobs"; }
    void        Draw() override;

private:
    static constexpr int kHistory = 180;   // three seconds at 60 Hz

    float m_depthHistory[kHistory] = {};
    int   m_head = 0;
};

} // namespace editor
