// WEEK 9 - the resource manager. See ResourceManager.h.
//
// BMP only, via SDL's built-in surface loader. No SDL_image.

#include <engine/core/Assert.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/platform/Renderer.h>
#include <engine/platform/SdlHandles.h>
#include <engine/resource/ResourceManager.h>

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng {
namespace {

struct Slot {
    std::string   path;
    TexturePtr    texture;          // RAII, from Week 3
    Texture       record;
    u32           refCount   = 0;
    u32           generation = 1;   // never 0 - see Handle.h
    bool          occupied   = false;
};

std::vector<Slot>                          g_slots;
std::unordered_map<u64, Handle<Texture>>   g_byPath;   // keyed on StringId of the path
std::vector<u32>                           g_freeIndices;
Handle<Texture>                            g_missing{};
bool                                       g_loadingMissing = false;
bool                                       g_initialised = false;

// One pending async decode. The BYTES were read on a worker thread; the
// surface-to-texture step happens here, on the main thread, because creating a
// texture touches the renderer.
struct PendingUpload {
    Handle<Texture> handle;
    std::vector<u8> bytes;
    bool            success = false;
    std::string     error;
};

std::vector<PendingUpload> g_pending;

usize EstimateBytes(const Texture& texture) {
    // 4 bytes per pixel is what SDL's default texture format costs on every
    // backend this engine uses. An estimate, labelled as one - a precise
    // figure would need to ask the driver, and the number is for a HUD.
    return static_cast<usize>(texture.width) * static_cast<usize>(texture.height) * 4u;
}

// Turns raw file bytes into an SDL texture. Main thread only.
bool UploadTexture(Slot& slot, const std::vector<u8>& bytes, std::string& outError) {
    SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (stream == nullptr) {
        outError = SDL_GetError();
        return false;
    }

    // closeio = true: SDL closes the stream whether or not the load succeeds,
    // so there is exactly one exit path and no leak on the failure branch.
    SurfacePtr surface(SDL_LoadBMP_IO(stream, true));
    if (surface == nullptr) {
        outError = SDL_GetError();
        return false;
    }

    auto* renderer = static_cast<SDL_Renderer*>(Renderer::NativeRendererHandle());
    if (renderer == nullptr) {
        outError = "no renderer; textures cannot be created";
        return false;
    }

    slot.texture.reset(SDL_CreateTextureFromSurface(renderer, surface.get()));
    if (slot.texture == nullptr) {
        outError = SDL_GetError();
        return false;
    }

    // Nearest-neighbour: the test textures are checkerboards, and a linear
    // filter turns a crisp 32x32 checker into grey mush the moment it is
    // scaled. It is also what a 2D pixel-art game wants.
    SDL_SetTextureScaleMode(slot.texture.get(), SDL_SCALEMODE_NEAREST);

    slot.record.width  = surface->w;
    slot.record.height = surface->h;
    slot.record.native = slot.texture.get();
    slot.record.state  = ResourceState::Ready;
    slot.record.bytes  = EstimateBytes(slot.record);
    return true;
}

Slot* Resolve(Handle<Texture> handle, bool reportStale) {
    if (handle.IsNull()) {
        return nullptr;
    }
    const u32 index = handle.Index();
    if (index >= g_slots.size()) {
        if (reportStale) {
            ENGINE_LOG_WARN(Channels::kResource,
                            "handle out of range: index {} generation {} (only {} slots)",
                            index, handle.Generation(), g_slots.size());
        }
        return nullptr;
    }

    Slot& slot = g_slots[index];
    if (!slot.occupied || slot.generation != handle.Generation()) {
        if (reportStale) {
            // *** THE MILESTONE 3 REQUIREMENT: DETECTED AND REPORTED, NAMING
            // THE INDEX AND GENERATION, AND NOT DEREFERENCED. *** Not an
            // assert-and-die: a missing texture should be a magenta square,
            // not a crash.
            ENGINE_LOG_WARN(Channels::kResource,
                            "stale texture handle: index {} generation {} (slot is at "
                            "generation {}, {}) - returning null",
                            index, handle.Generation(), slot.generation,
                            slot.occupied ? "occupied by a newer asset" : "free");
        }
        return nullptr;
    }
    return &slot;
}

Handle<Texture> AllocateSlot(std::string_view path) {
    u32 index = 0;
    if (!g_freeIndices.empty()) {
        index = g_freeIndices.back();
        g_freeIndices.pop_back();
    } else {
        index = static_cast<u32>(g_slots.size());
        g_slots.emplace_back();
    }

    Slot& slot     = g_slots[index];
    slot.path.assign(path);
    slot.occupied  = true;
    slot.refCount  = 0;
    slot.record    = Texture{};
    slot.record.state = ResourceState::Empty;

    return MakeHandle<Texture>(index, slot.generation);
}

void UnloadSlot(u32 index) {
    Slot& slot = g_slots[index];
    ENGINE_LOG_DEBUG(Channels::kResource, "unloading '{}' (refcount reached 0)", slot.path);

    g_byPath.erase(StringId(slot.path).Value());
    slot.texture.reset();          // RAII: SDL_DestroyTexture, exactly once
    slot.record   = Texture{};
    slot.path.clear();
    slot.occupied = false;

    slot.generation = (slot.generation + 1) & Handle<Texture>::kMaxGeneration;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    g_freeIndices.push_back(index);
}

} // namespace

const char* ToString(ResourceState state) {
    switch (state) {
        case ResourceState::Empty:   return "Empty";
        case ResourceState::Loading: return "Loading";
        case ResourceState::Ready:   return "Ready";
        case ResourceState::Failed:  return "Failed";
    }
    return "?";
}

bool ResourceManager::Init() {
    g_initialised = true;
    ENGINE_LOG_INFO(Channels::kResource, "ResourceManager up");
    return true;
}

void ResourceManager::Shutdown() {
    // The placeholder's own reference, released before the audit so it does
    // not show up as a leak of the engine's own making.
    if (!g_missing.IsNull()) {
        Release(g_missing);
        g_missing = {};
    }

    // *** THE ASSERT THAT FINDS MORE BUGS THAN ANY TEST WRITTEN THIS WEEK. ***
    // Everything still resident is logged with its path and count FIRST, so
    // the diagnostic names the culprit rather than merely the fact.
    u64 leaked = 0;
    for (const Slot& slot : g_slots) {
        if (slot.occupied && slot.refCount > 0) {
            ENGINE_LOG_ERROR(Channels::kResource,
                             "leaked at shutdown: '{}' still has refcount {}", slot.path,
                             slot.refCount);
            leaked += slot.refCount;
        }
    }
    ENGINE_ASSERT_MSG(leaked == 0,
                      "ResourceManager::Shutdown with non-zero refcounts - some Acquire "
                      "has no matching Release");

    for (u32 i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].occupied) {
            g_slots[i].texture.reset();
            g_slots[i].occupied = false;
        }
    }
    g_slots.clear();
    g_byPath.clear();
    g_freeIndices.clear();
    g_pending.clear();
    g_initialised = false;

    ENGINE_LOG_INFO(Channels::kResource, "ResourceManager down");
}

Handle<Texture> ResourceManager::AcquireTexture(std::string_view virtualPath) {
    // KEYED ON THE StringId OF THE VIRTUAL PATH. This is where Week 8 pays
    // off: the same path acquired twice returns the SAME handle with a
    // refcount of 2, never two copies. The provided scene has 18 entities
    // sharing one texture, so a refcount that is not 18 says the lookup is not
    // finding the existing entry.
    const StringId key(virtualPath);

    if (const auto it = g_byPath.find(key.Value()); it != g_byPath.end()) {
        if (Slot* slot = Resolve(it->second, /*reportStale=*/false); slot != nullptr) {
            ++slot->refCount;
            return it->second;
        }
        g_byPath.erase(it);   // the slot was recycled underneath the map
    }

    const Handle<Texture> handle = AllocateSlot(virtualPath);
    Slot&                 slot   = g_slots[handle.Index()];
    slot.refCount = 1;
    g_byPath[key.Value()] = handle;

    std::vector<u8> bytes;
    std::string     error;
    if (!FileSystem::ReadFile(virtualPath, bytes, error)) {
        ENGINE_LOG_ERROR(Channels::kResource, "cannot read '{}': {}", virtualPath, error);
        slot.record.state = ResourceState::Failed;
        return handle;
    }

    if (!UploadTexture(slot, bytes, error)) {
        ENGINE_LOG_ERROR(Channels::kResource, "cannot decode '{}': {}", virtualPath, error);
        slot.record.state = ResourceState::Failed;
        return handle;
    }

    ENGINE_LOG_DEBUG(Channels::kResource, "loaded '{}' ({}x{})", virtualPath,
                     slot.record.width, slot.record.height);
    return handle;
}

Handle<Texture> ResourceManager::AcquireTextureAsync(std::string_view virtualPath) {
    const StringId key(virtualPath);

    if (const auto it = g_byPath.find(key.Value()); it != g_byPath.end()) {
        if (Slot* slot = Resolve(it->second, false); slot != nullptr) {
            ++slot->refCount;
            return it->second;
        }
        g_byPath.erase(it);
    }

    const Handle<Texture> handle = AllocateSlot(virtualPath);
    Slot&                 slot   = g_slots[handle.Index()];
    slot.refCount     = 1;
    slot.record.state = ResourceState::Loading;   // the THIRD state
    g_byPath[key.Value()] = handle;

    // The read runs on a worker thread; this callback runs on the MAIN thread
    // from FileSystem::PumpCompletions. See the block comment in FileSystem.h.
    FileSystem::ReadFileAsync(virtualPath,
                              [handle](bool success, std::vector<u8>&& bytes,
                                       const std::string& error) {
                                  PendingUpload pending;
                                  pending.handle  = handle;
                                  pending.bytes   = std::move(bytes);
                                  pending.success = success;
                                  pending.error   = error;
                                  g_pending.push_back(std::move(pending));
                              });

    // Returned IMMEDIATELY, before the bytes have arrived. That is the entire
    // point of async loading: the scene can be built and the frame can render
    // while the read is in flight.
    return handle;
}

void ResourceManager::PumpPendingUploads() {
    // MAIN THREAD ONLY. Creating a texture touches the renderer.
    for (PendingUpload& pending : g_pending) {
        Slot* slot = Resolve(pending.handle, /*reportStale=*/false);
        if (slot == nullptr) {
            // Released while the read was in flight. Not an error - it is the
            // normal consequence of unloading a scene mid-load - so the bytes
            // are simply dropped.
            continue;
        }

        std::string error;
        if (!pending.success || !UploadTexture(*slot, pending.bytes, error)) {
            ENGINE_LOG_ERROR(Channels::kResource, "async load of '{}' failed: {}",
                             slot->path, pending.success ? error : pending.error);
            slot->record.state = ResourceState::Failed;
            continue;
        }
        ENGINE_LOG_DEBUG(Channels::kResource, "async loaded '{}' ({}x{})", slot->path,
                         slot->record.width, slot->record.height);
    }
    g_pending.clear();
}

void ResourceManager::Release(Handle<Texture> handle) {
    if (handle.IsNull()) {
        return;
    }

    Slot* slot = Resolve(handle, /*reportStale=*/false);
    if (slot == nullptr) {
        // A Release of something already gone is a normal consequence of
        // shutdown ordering, not an error. Warned at Debug so it is findable
        // when a refcount audit is running and invisible otherwise.
        ENGINE_LOG_DEBUG(Channels::kResource,
                         "Release of a stale or null handle (index {} generation {})",
                         handle.Index(), handle.Generation());
        return;
    }

    if (slot->refCount == 0) {
        ENGINE_LOG_WARN(Channels::kResource, "Release of '{}' whose refcount is already 0",
                        slot->path);
        return;
    }

    --slot->refCount;
    if (slot->refCount == 0) {
        UnloadSlot(handle.Index());
    }
}

Texture* ResourceManager::Get(Handle<Texture> handle) {
    Slot* slot = Resolve(handle, /*reportStale=*/true);
    if (slot == nullptr) {
        return nullptr;
    }
    if (slot->record.state == ResourceState::Failed) {
        // A magenta square, not a hole and not a crash. MissingTexture()
        // acquires the placeholder on first use - see the note there for why
        // it is lazy rather than loaded at Init.
        //
        // `slot` is deliberately re-resolved afterwards rather than reused:
        // acquiring the placeholder can append to g_slots and reallocate it,
        // which would leave this pointer dangling. That is the same
        // iterator-invalidation family as everything else in Week 10, met in a
        // function that looks like it could not possibly have the problem.
        const Handle<Texture> placeholderHandle = MissingTexture();
        Slot* placeholder = Resolve(placeholderHandle, false);
        return (placeholder != nullptr) ? &placeholder->record : nullptr;
    }
    return &slot->record;
}

bool ResourceManager::IsValid(Handle<Texture> handle) {
    return Resolve(handle, false) != nullptr;
}

bool ResourceManager::IsReady(Handle<Texture> handle) {
    const Slot* slot = Resolve(handle, false);
    return slot != nullptr && slot->record.state == ResourceState::Ready;
}

Handle<Texture> ResourceManager::MissingTexture() {
    // LAZY, and the reason is the Milestone 3 check.
    //
    // The placeholder is a REAL reference held by the engine. Acquiring it
    // eagerly in Init would mean TotalRefCount() reads 1 after a scene unload
    // rather than 0 - and that number is read live off the resource panel by
    // whoever is checking the milestone, where 1 and 0 mean very different
    // things.
    //
    // So it is acquired on the FIRST FAILED LOAD instead. In a healthy run the
    // count genuinely reaches zero; when a load has failed, the placeholder
    // sitting in the resource list is itself the diagnostic.
    if (!g_missing.IsNull() || g_loadingMissing) {
        return g_missing;
    }
    g_loadingMissing = true;   // the placeholder's own load can fail and re-enter
    g_missing        = AcquireTexture("textures/missing.bmp");
    g_loadingMissing = false;

    if (g_missing.IsNull() || !IsReady(g_missing)) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "the missing-texture placeholder itself failed to load; failed "
                        "loads will render as nothing at all");
    }
    return g_missing;
}

usize ResourceManager::LoadedCount() {
    usize count = 0;
    for (const Slot& slot : g_slots) {
        if (slot.occupied) {
            ++count;
        }
    }
    return count;
}

u64 ResourceManager::TotalRefCount() {
    u64 total = 0;
    for (const Slot& slot : g_slots) {
        if (slot.occupied) {
            total += slot.refCount;
        }
    }
    return total;
}

usize ResourceManager::BytesResident() {
    usize total = 0;
    for (const Slot& slot : g_slots) {
        if (slot.occupied) {
            total += slot.record.bytes;
        }
    }
    return total;
}

void ResourceManager::Snapshot(std::vector<Entry>& out) {
    out.clear();
    for (u32 i = 0; i < g_slots.size(); ++i) {
        const Slot& slot = g_slots[i];
        if (!slot.occupied) {
            continue;
        }
        Entry entry;
        entry.path     = slot.path;
        entry.refCount = slot.refCount;
        entry.state    = slot.record.state;
        entry.width    = slot.record.width;
        entry.height   = slot.record.height;
        entry.bytes    = slot.record.bytes;
        entry.handle   = MakeHandle<Texture>(i, slot.generation);
        out.push_back(std::move(entry));
    }
}

} // namespace eng
