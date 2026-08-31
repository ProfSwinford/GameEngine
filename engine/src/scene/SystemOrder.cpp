// WEEK 10 - the system scheduler. See SystemOrder.h for the declared order.

#include <engine/core/Log.h>
#include <engine/debug/ScopedTimer.h>
#include <engine/scene/SystemOrder.h>

#include <algorithm>

namespace eng {
namespace {

std::vector<System*> g_systems;
bool                 g_dirty = false;

void SortIfNeeded() {
    if (!g_dirty) {
        return;
    }
    // STABLE sort: two systems registered at the same priority keep their
    // registration order rather than swapping around between runs. A
    // non-deterministic update order for equal priorities would be a bug that
    // reproduces on one machine and not another, which is the exact class of
    // problem this file exists to eliminate.
    std::stable_sort(g_systems.begin(), g_systems.end(),
                     [](const System* a, const System* b) { return a->Order() < b->Order(); });
    g_dirty = false;
}

} // namespace

void SystemScheduler::Register(System* system) {
    if (system == nullptr) {
        return;
    }
    g_systems.push_back(system);
    g_dirty = true;
}

void SystemScheduler::Unregister(System* system) {
    std::erase(g_systems, system);
}

void SystemScheduler::Clear() {
    g_systems.clear();
    g_dirty = false;
}

void SystemScheduler::UpdateRange(i32 minOrder, i32 maxOrder, f32 deltaSeconds) {
    SortIfNeeded();

    // Snapshot, because a system's Update may register or unregister another
    // one - the gate game's own system does exactly that when it spawns - and
    // push_back on the live vector would invalidate the iteration. Same
    // problem DeferredOps solves for entities, at the system level.
    static std::vector<System*> running;
    running.assign(g_systems.begin(), g_systems.end());

    for (System* system : running) {
        const i32 order = system->Order();
        if (order < minOrder || order >= maxOrder) {
            continue;
        }
        // Every system gets its own timer site, which is what puts collision
        // on the profiler HUD as its own line item - a Week 10 verification.
        ScopedTimer timer(system->Name());
        system->Update(deltaSeconds);
    }
}

void SystemScheduler::Simulate(f32 fixedStepSeconds) {
    UpdateRange(0, SystemStage::kFirstRenderStage, fixedStepSeconds);
}

void SystemScheduler::RenderPass(f32 realDeltaSeconds) {
    UpdateRange(SystemStage::kFirstRenderStage, 1'000'000, realDeltaSeconds);
}

void SystemScheduler::LogOrder() {
    SortIfNeeded();

    // The DECLARED order, logged once at startup - which the Week 10 evidence
    // document asks for as a paste, and which Phase 2 will want when something
    // happens a frame late.
    //
    // The built-in stages are listed alongside the registered systems on
    // purpose. Several of them are called directly by Engine rather than
    // through a System object (input sampling, message dispatch, the deferred
    // drain, the sprite pass, debug draw), and a log that showed only the
    // registered ones would say "1 system" for an engine that plainly does
    // more than one thing per tick. This is what the frame ACTUALLY does.
    struct Builtin { i32 order; const char* name; };
    static constexpr Builtin kBuiltins[] = {
        {SystemStage::kInput,             "Input sampling      (Engine::BeginFrame)"},
        {SystemStage::kCollisionResponse, "Message dispatch    (MessageBus::Dispatch)"},
        {SystemStage::kDeferred,          "Deferred spawn/destroy (DeferredOps::Apply)"},
        {SystemStage::kRender,            "Sprite render       (SpriteRenderSystem)"},
        {SystemStage::kDebugDraw,         "Debug draw          (DebugDraw::Render)"},
    };

    ENGINE_LOG_INFO(Channels::kScene,
                    "declared system update order ({} registered system(s) plus the "
                    "engine's built-in stages):", g_systems.size());

    usize next = 0;
    for (const Builtin& builtin : kBuiltins) {
        while (next < g_systems.size() && g_systems[next]->Order() <= builtin.order) {
            const System* system = g_systems[next];
            ENGINE_LOG_INFO(Channels::kScene, "  {:>4}  {}{}", system->Order(),
                            system->Name(),
                            system->Order() >= SystemStage::kFirstRenderStage
                                ? "   (per frame)"
                                : "   (per fixed step)");
            ++next;
        }
        ENGINE_LOG_INFO(Channels::kScene, "  {:>4}  {}{}", builtin.order, builtin.name,
                        builtin.order >= SystemStage::kFirstRenderStage
                            ? "   (per frame)"
                            : "   (per fixed step)");
    }
    for (; next < g_systems.size(); ++next) {
        ENGINE_LOG_INFO(Channels::kScene, "  {:>4}  {}   (per frame)",
                        g_systems[next]->Order(), g_systems[next]->Name());
    }
}

void SystemScheduler::ForEach(const std::function<void(System&)>& fn) {
    SortIfNeeded();
    for (System* system : g_systems) {
        fn(*system);
    }
}

usize SystemScheduler::Count() {
    return g_systems.size();
}

} // namespace eng
