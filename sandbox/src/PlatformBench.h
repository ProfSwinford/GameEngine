#pragma once

// =============================================================================
//  WEEK 5 - the three platform measurements.
//
//  Thread creation, context switch, and first-touch page fault, measured with
//  the Week 4 scoped timers, in a Release build. Reported into
//  JobSystem::PlatformCosts so the editor's Jobs panel can display them, and
//  written up with their IMPLICATIONS in docs/week05-os-measurements.md - the
//  implications being the part that is actually graded.
//
//  These exist so that when someone later says threading is free, the
//  disagreement can be backed by evidence from this machine.
// =============================================================================

#include <engine/concurrency/JobSystem.h>

namespace bench {

// Runs all three and logs them. Returns the costs so the caller can hand them
// to JobSystem for display.
eng::JobSystem::PlatformCosts RunPlatformMeasurements();

} // namespace bench
