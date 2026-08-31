// ============================================================================
//  DeferredOps.cpp - the spawn and destroy queues. See DeferredOps.h for the
//  four rules this file implements.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <set>
#include <vector>

namespace eng {
namespace {

struct QueuedSpawn {
    DeferredOps::SpawnParams  params;
    DeferredOps::SpawnBuilder builder;
    bool                      fromPrefab = true;
};

std::vector<QueuedSpawn> g_spawns;
std::vector<EntityId>    g_destroys;

// The same set of ids as g_destroys, kept separately so that "is this one
// already queued?" is a quick lookup rather than a search through the whole
// list. std::set can do that because EntityId knows how to compare itself -
// see the <=> line in EntityId.h.
std::set<EntityId> g_pendingDestroy;

// Used to invent unique names for spawned entities.
int g_nameCounter = 0;

} // namespace

void DeferredOps::QueueSpawn(const SpawnParams& params) {
    QueuedSpawn queued;
    queued.params     = params;
    queued.fromPrefab = true;
    g_spawns.push_back(std::move(queued));
}

void DeferredOps::QueueSpawn(SpawnBuilder builder) {
    QueuedSpawn queued;
    queued.builder    = std::move(builder);
    queued.fromPrefab = false;
    g_spawns.push_back(std::move(queued));
}

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
    // RULE 4: take the queues away and drain the copies ONCE. Anything queued
    // by a builder below lands in the now-empty originals and happens next
    // frame. swap() is used because it hands over the contents without copying
    // them - it just exchanges what the two vectors point at.
    std::vector<QueuedSpawn> spawns;
    std::vector<EntityId>    destroys;
    spawns.swap(g_spawns);
    destroys.swap(g_destroys);

    // Spawns first, then destroys. The other way round would let a destroy
    // free a slot that a spawn in the same batch immediately reuses. That is
    // still correct, but it makes two entities from the same frame share an
    // index number, which is confusing to read in a log for no benefit.
    for (QueuedSpawn& queued : spawns) {
        EntityId id;

        if (!queued.fromPrefab) {
            if (queued.builder) {
                id = queued.builder(scene);
            }
        } else {
            std::string name = queued.params.name;
            if (name.empty() || !scene.Find(name).IsNull()) {
                // Names have to be unique because the scene looks entities up
                // by name. A number is added rather than the spawn refused -
                // code firing fifty bullets should not have to invent fifty
                // names.
                name += "#" + std::to_string(++g_nameCounter);
            }

            std::string error;
            id = scene.InstantiatePrefab(queued.params.prefab, name, error);
            if (id.IsNull()) {
                ENGINE_LOG_ERROR(Channels::kScene, "could not spawn: {}", error);
                continue;
            }

            if (Entity* entity = scene.Get(id); entity != nullptr) {
                Transform2D& transform = entity->Transform();
                transform.SetLocalPosition(queued.params.position);
                transform.SetLocalRotation(queued.params.rotation);
                transform.SetLocalScale(queued.params.scale);
            }
        }
    }

    for (const EntityId id : destroys) {
        // Checked again: something else may have destroyed it between the
        // queueing and now. Being able to ask that question at all is exactly
        // what the generation number in EntityId is for.
        if (!scene.IsValid(id)) {
            continue;
        }
        MessageBus::UnsubscribeAll(id);
        scene.DestroyEntityImmediate(id);
    }

    g_pendingDestroy.clear();
}

void DeferredOps::Clear() {
    g_spawns.clear();
    g_destroys.clear();
    g_pendingDestroy.clear();
}

std::size_t DeferredOps::PendingSpawnCount()   { return g_spawns.size(); }
std::size_t DeferredOps::PendingDestroyCount() { return g_destroys.size(); }

} // namespace eng
