// ============================================================================
//  DeferredOps.cpp - the spawn and destroy queues. See DeferredOps.h for the
//  four rules this file implements.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace eng {
namespace {

std::vector<EntityId> g_destroys;

// The same set of ids as g_destroys, kept separately so that "is this one
// already queued?" is a quick lookup rather than a search through the whole
// list. std::set can do that because EntityId knows how to compare itself -
// see the <=> line in EntityId.h.
std::set<EntityId> g_pendingDestroy;

// ---- the fuses, and they are a SEPARATE LIST on purpose --------------------
//
// An entity waiting on a fuse is NOT pending destruction: it collides, draws
// and updates normally for the whole delay, and only joins g_destroys when the
// fuse runs out. DeferredOps.h's QueueDestroyAfter has the full argument for
// why - the short version is that marking it pending would silently switch its
// collisions off for the entire fuse.
std::map<EntityId, float> g_fuses;

} // namespace

void DeferredOps::QueueDestroy(EntityId id) {
    if (id.IsNull()) {
        return;
    }

    // A fuse on this entity is no longer relevant - it is going now. Erased
    // before the rule-3 check below, so that fusing something and then
    // destroying it outright does not leave a fuse behind to fire against a
    // reused slot later.
    g_fuses.erase(id);

    // RULE 3: queueing the same destroy twice is ignored rather than treated
    // as a mistake. insert() returns a pair whose .second says whether
    // anything was actually added, which makes this a one-line check.
    if (!g_pendingDestroy.insert(id).second) {
        return;
    }
    g_destroys.push_back(id);
}

void DeferredOps::QueueDestroy(Entity& entity) {
    QueueDestroy(entity.Id());
}

bool DeferredOps::IsPendingDestroy(EntityId id) {
    return !id.IsNull() && g_pendingDestroy.contains(id);
}

void DeferredOps::QueueDestroyAfter(EntityId id, float delaySeconds) {
    if (id.IsNull()) {
        return;
    }
    // Zero or less is exactly QueueDestroy, so there is no edge case for a
    // caller to remember and no fuse that fires on its first tick.
    if (delaySeconds <= 0.0f) {
        QueueDestroy(id);
        return;
    }
    // Already on its way out, and a fuse cannot call that off - only
    // CancelQueuedDestroy can, and it is explicit about doing so.
    if (g_pendingDestroy.contains(id)) {
        return;
    }
    // operator[] REPLACES rather than adding a second entry, which is what
    // makes "ask again to reset the timer" work.
    g_fuses[id] = delaySeconds;
}

void DeferredOps::QueueDestroyAfter(Entity& entity, float delaySeconds) {
    QueueDestroyAfter(entity.Id(), delaySeconds);
}

float DeferredOps::SecondsUntilDestroy(EntityId id) {
    if (id.IsNull()) {
        return -1.0f;
    }
    if (g_pendingDestroy.contains(id)) {
        return 0.0f;   // this tick
    }
    const auto fuse = g_fuses.find(id);
    return fuse != g_fuses.end() ? fuse->second : -1.0f;
}

bool DeferredOps::CancelQueuedDestroy(EntityId id) {
    if (id.IsNull()) {
        return false;
    }

    bool cancelled = g_fuses.erase(id) > 0;

    if (g_pendingDestroy.erase(id) > 0) {
        std::erase(g_destroys, id);
        cancelled = true;
    }
    return cancelled;
}

std::size_t DeferredOps::PendingFuseCount() { return g_fuses.size(); }

void DeferredOps::TickFuses(float fixedStepSeconds) {
    if (g_fuses.empty()) {
        return;
    }

    // Collected first, then queued, because QueueDestroy erases from g_fuses -
    // and erasing from the map being iterated is the exact hazard this whole
    // file exists to prevent.
    std::vector<EntityId> expired;
    for (auto& [id, remaining] : g_fuses) {
        remaining -= fixedStepSeconds;
        if (remaining <= 0.0f) {
            expired.push_back(id);
        }
    }
    for (const EntityId id : expired) {
        QueueDestroy(id);
    }
}

void DeferredOps::Apply(Scene& scene) {
    // RULE 4: take the queue away and drain the copy ONCE. Anything queued
    // while draining lands in the now-empty original and happens next frame.
    // swap() is used because it hands over the contents without copying them -
    // it just exchanges what the two vectors point at.
    std::vector<EntityId> destroys;
    destroys.swap(g_destroys);

    for (const EntityId id : destroys) {
        // Checked again: something else may have destroyed it between the
        // queueing and now. Being able to ask that question at all is exactly
        // what the generation number in EntityId is for.
        if (scene.IsValid(id)) {
            scene.DestroyEntityImmediate(id);
        }
    }

    g_pendingDestroy.clear();
}

void DeferredOps::Clear() {
    g_destroys.clear();
    g_pendingDestroy.clear();

    // The fuses go too, and this is the one that matters most across a scene
    // change. A fresh scene hands out slot indices from zero at generation 1,
    // so a leftover fuse would stand a real chance of matching a brand new
    // entity - and would then destroy something nobody asked to destroy,
    // seconds into the next level, with no connection to its cause. See
    // Engine::ApplyPendingSceneRequest, which is what calls this.
    g_fuses.clear();
}

std::size_t DeferredOps::PendingDestroyCount() { return g_destroys.size(); }

} // namespace eng
