// WEEK 10 - deferred spawn and destroy. See DeferredOps.h for the four
// recorded decisions.

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace eng {
namespace {

struct QueuedSpawn {
    DeferredOps::SpawnParams  params;
    DeferredOps::SpawnBuilder builder;
    bool                      fromPrefab = true;
};

std::vector<QueuedSpawn>   g_spawns;
std::vector<EntityHandle>  g_destroys;
std::unordered_set<u32>    g_pendingDestroy;   // handle values, for O(1) lookup
u64                        g_totalSpawned   = 0;
u64                        g_totalDestroyed = 0;
u32                        g_nameCounter    = 0;

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

void DeferredOps::QueueDestroy(EntityHandle handle) {
    if (handle.IsNull()) {
        return;
    }
    // DECISION 3: deduplicated here and re-checked on apply. Two bullets
    // hitting the same enemy in one tick is ordinary correct gameplay, not a
    // programmer error, so this does not assert.
    if (!g_pendingDestroy.insert(handle.value).second) {
        return;
    }
    g_destroys.push_back(handle);
}

bool DeferredOps::IsPendingDestroy(EntityHandle handle) {
    return !handle.IsNull() && g_pendingDestroy.contains(handle.value);
}

void DeferredOps::Apply(Scene& scene) {
    // DECISION 4: the queues are taken by SWAP and drained ONCE. Anything
    // queued by a builder below lands in the now-empty member queues and is
    // applied next frame. Draining in a loop until empty risks never
    // terminating - a spawn that spawns is a legitimate thing to write.
    std::vector<QueuedSpawn>  spawns;
    std::vector<EntityHandle> destroys;
    spawns.swap(g_spawns);
    destroys.swap(g_destroys);

    // SPAWNS FIRST, then destroys. The other order would let a destroy free a
    // slot that a spawn in the same batch immediately reuses, which is
    // correct but makes a handle from this frame and a handle from last frame
    // share an index - harder to read in a log for no benefit.
    for (QueuedSpawn& queued : spawns) {
        EntityHandle handle;

        if (!queued.fromPrefab) {
            if (queued.builder) {
                handle = queued.builder(scene);
            }
        } else {
            std::string name = queued.params.name;
            if (name.empty() || !scene.Find(name).IsNull()) {
                // Names must be unique because the scene keeps a name->handle
                // map. A suffix rather than a rejection: gameplay code
                // spawning fifty bullets should not have to invent fifty
                // names.
                name += "#" + std::to_string(++g_nameCounter);
            }

            std::string error;
            handle = scene.InstantiatePrefab(queued.params.prefab, name, error);
            if (handle.IsNull()) {
                ENGINE_LOG_ERROR(Channels::kScene, "deferred spawn failed: {}", error);
                continue;
            }

            if (Entity* entity = scene.Get(handle); entity != nullptr) {
                Transform2D& transform = entity->Transform();
                transform.SetLocalPosition(queued.params.position);
                transform.SetLocalRotation(queued.params.rotation);
                transform.SetLocalScale(queued.params.scale);
            }
        }

        if (!handle.IsNull()) {
            ++g_totalSpawned;
        }
    }

    for (EntityHandle handle : destroys) {
        // Re-checked: something else may have destroyed it between the queue
        // and here, and IsValid on a stale handle is the whole point of the
        // generation counter.
        if (!scene.IsValid(handle)) {
            continue;
        }
        MessageBus::UnsubscribeAll(handle);
        scene.DestroyEntityImmediate(handle);
        ++g_totalDestroyed;
    }

    g_pendingDestroy.clear();
}

void DeferredOps::Clear() {
    g_spawns.clear();
    g_destroys.clear();
    g_pendingDestroy.clear();
}

usize DeferredOps::PendingSpawnCount()   { return g_spawns.size(); }
usize DeferredOps::PendingDestroyCount() { return g_destroys.size(); }
u64   DeferredOps::TotalSpawned()        { return g_totalSpawned; }
u64   DeferredOps::TotalDestroyed()      { return g_totalDestroyed; }

} // namespace eng
