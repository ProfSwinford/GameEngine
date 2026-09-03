// ============================================================================
//  DeferredOps.cpp - the spawn and destroy queues. See DeferredOps.h for the
//  four rules this file implements.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Scene.h>

#include <algorithm>
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

} // namespace

void DeferredOps::QueueDestroy(EntityId id) {
    if (id.IsNull()) {
        return;
    }
    // RULE 3: queueing the same destroy twice is ignored rather than treated
    // as a mistake. insert() returns a pair whose .second says whether
    // anything was actually added, which makes this a one-line check.
    if (!g_pendingDestroy.insert(id).second) {
        return;
    }
    g_destroys.push_back(id);
}

bool DeferredOps::IsPendingDestroy(EntityId id) {
    return !id.IsNull() && g_pendingDestroy.contains(id);
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
}

std::size_t DeferredOps::PendingDestroyCount() { return g_destroys.size(); }

} // namespace eng
