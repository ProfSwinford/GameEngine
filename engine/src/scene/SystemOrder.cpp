// =============================================================================
//  SystemOrder.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. SystemOrder.h explains WHAT each
//  function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/scene/SystemOrder.h>

namespace eng {

// TODO: keep the registered systems SORTED BY Order() and run them in that
// order. SystemOrder.h explains what an accidental order costs: movement after
// collision means fast objects pass through walls with nothing ever reported.
//
// The scheduler only BORROWS a system pointer, so Unregister has to be called
// before a system is destroyed.
void SystemScheduler::Register(System* /*system*/) {}

void SystemScheduler::Unregister(System* /*system*/) {}

void SystemScheduler::Clear() {}

// TODO: run every system whose stage number is in [minOrder, maxOrder).
//
// Walk a COPY of the list: a system's Update is allowed to register or
// unregister another one, and adding to a vector while looping over it can
// move the whole thing elsewhere in memory.
void SystemScheduler::UpdateRange(int /*minOrder*/, int /*maxOrder*/,
                                  float /*deltaSeconds*/) {}

void SystemScheduler::Simulate(float /*fixedStepSeconds*/) {}

void SystemScheduler::RenderPass(float /*realDeltaSeconds*/) {}

// The engine calls this once at start-up so the running order is written down
// in the Console rather than being a guess. With nothing registered it says
// so, which is the honest answer.
void SystemScheduler::LogOrder() {
    ENGINE_LOG_INFO(Channels::kScene,
                    "no systems are registered - SystemScheduler is still a shell");
}

void SystemScheduler::ForEach(const std::function<void(System&)>& /*fn*/) {}

std::size_t SystemScheduler::Count() { return 0; }

} // namespace eng
