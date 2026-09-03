// =============================================================================
//  SystemOrder.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. SystemOrder.h is the specification; read it
//  first.
//
//  Systems run in a written-down order, not whichever order they happened to be
//  created in. Run collision before movement and a fast object passes straight
//  through a wall with nothing ever reported.
// =============================================================================

#include <engine/scene/SystemOrder.h>

namespace eng {

// Adds a system to the schedule. The scheduler only BORROWS the pointer, so a
// system must unregister before it is destroyed.
void SystemScheduler::Register(System* /*system*/) {
}

// Takes a system back out of the schedule.
void SystemScheduler::Unregister(System* /*system*/) {
}

// Forgets every registered system.
void SystemScheduler::Clear() {
}

// Runs every system whose stage number falls in the range, in order. Walk a
// COPY of the list: a system's Update may register or unregister another one.
void SystemScheduler::UpdateRange(int /*minOrder*/, int /*maxOrder*/,
                                  float /*deltaSeconds*/) {
}

// Runs the simulation stages - everything below the first render stage - once
// per fixed step.
void SystemScheduler::Simulate(float /*fixedStepSeconds*/) {
}

// Runs the render stages, once per drawn frame rather than once per step.
void SystemScheduler::RenderPass(float /*realDeltaSeconds*/) {
}

// Writes the running order to the log at start-up, so which system runs when is
// never a guess.
void SystemScheduler::LogOrder() {
}

// Visits every registered system in order.
void SystemScheduler::ForEach(const std::function<void(System&)>& /*fn*/) {
}

// How many systems are registered.
std::size_t SystemScheduler::Count() {
    return 0;
}

} // namespace eng
