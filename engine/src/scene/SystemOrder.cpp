// ============================================================================
//  SystemOrder.cpp - the system scheduler. See SystemOrder.h for the order it
//  keeps and why that order matters.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/SystemOrder.h>

#include <algorithm>
#include <vector>

namespace eng {
namespace {

std::vector<System*> g_systems;

// The list is only re-sorted when something has actually changed, rather than
// once per frame.
bool g_needsSort = false;

void SortIfNeeded() {
    if (!g_needsSort) {
        return;
    }
    // std::stable_sort rather than std::sort: two systems registered at the
    // same stage number keep the order they were added in. Plain sort is free
    // to put them either way round, which would mean the game behaved slightly
    // differently on different machines - exactly the kind of bug this file
    // exists to prevent.
    std::stable_sort(g_systems.begin(), g_systems.end(),
                     [](const System* a, const System* b) {
                         return a->Order() < b->Order();
                     });
    g_needsSort = false;
}

} // namespace

void SystemScheduler::Register(System* system) {
    if (system == nullptr) {
        return;
    }
    g_systems.push_back(system);
    g_needsSort = true;
}

void SystemScheduler::Unregister(System* system) {
    // std::erase removes every matching element from a container in one call.
    std::erase(g_systems, system);
}

void SystemScheduler::Clear() {
    g_systems.clear();
    g_needsSort = false;
}

void SystemScheduler::UpdateRange(int minOrder, int maxOrder, float deltaSeconds) {
    SortIfNeeded();

    // The list is COPIED before it is walked, because a system's Update is
    // allowed to register or unregister another one - and adding to a vector
    // while looping over it can move the whole thing elsewhere in memory.
    // Working from a copy sidesteps that entirely.
    std::vector<System*> running = g_systems;

    for (System* system : running) {
        const int order = system->Order();
        if (order < minOrder || order >= maxOrder) {
            continue;
        }
        system->Update(deltaSeconds);
    }
}

void SystemScheduler::Simulate(float fixedStepSeconds) {
    UpdateRange(0, SystemStage::kFirstRenderStage, fixedStepSeconds);
}

void SystemScheduler::RenderPass(float realDeltaSeconds) {
    UpdateRange(SystemStage::kFirstRenderStage, 1'000'000, realDeltaSeconds);
}

void SystemScheduler::LogOrder() {
    SortIfNeeded();

    ENGINE_LOG_INFO(Channels::kScene, "systems update in this order:");
    for (const System* system : g_systems) {
        const bool perFrame = system->Order() >= SystemStage::kFirstRenderStage;
        ENGINE_LOG_INFO(Channels::kScene, "  {:>4}  {}  ({})", system->Order(),
                        system->Name(), perFrame ? "per frame" : "per fixed step");
    }
}

void SystemScheduler::ForEach(const std::function<void(System&)>& fn) {
    SortIfNeeded();
    for (System* system : g_systems) {
        fn(*system);
    }
}

std::size_t SystemScheduler::Count() {
    return g_systems.size();
}

} // namespace eng
