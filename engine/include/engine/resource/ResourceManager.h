#pragma once

// =============================================================================
//  WEEK 9 - the resource manager. Ch. 7.2.
//
//  Loads assets, counts references, releases them, and hands out handles.
//
//  ---------------------------------------------------------------------------
//  REFERENCE COUNTING, and the one rule that makes it survivable:
//
//    Acquire increments. Release decrements. At zero, the asset unloads.
//
//    Every Acquire has exactly one matching Release and you can point at both.
//    Cannot point at the Release -> leak. Can point at two -> premature free.
//
//  The M3 verification is that unloading a scene returns EVERY refcount to
//  zero, watched live on the resource panel. Loading the same path twice
//  returns the SAME handle with a refcount of 2, never two copies - the lookup
//  is keyed on the StringId of the virtual path, which is where Week 8 pays
//  off. The provided scene has 18 entities sharing checker_red.bmp precisely
//  so that a refcount that is not 18 tells you the lookup is not finding the
//  existing entry.
//
//  RAII WRAPPER, considered and rejected for now: a TextureRef that acquired
//  in its constructor and released in its destructor would automate the rule
//  the same way Week 3's unique_ptr automates SDL teardown, and it is the
//  right answer eventually. It is not here because SpriteComponent's lifetime
//  is already managed by Entity, so the wrapper would add a second owner to
//  the thing that already has exactly one - and because the explicit
//  Acquire/Release pair is what makes the refcount visible in the panel while
//  learning to read it. This is written down as a Phase 2 item.
//
//  ---------------------------------------------------------------------------
//  ASSET FORMATS: BMP ONLY, via SDL's built-in surface loader. No SDL_image,
//  no PNG, no extra dependency. Week 9 is the week least able to survive a
//  build failure over an image codec.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/resource/Handle.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

// A texture's load state. ASYNC LOADING NEEDS A THIRD STATE: a handle can be
// valid while the bytes are still in flight, which is distinct from both
// "ready" and "stale". Callers ask IsReady() and draw nothing (or the
// placeholder) until it is.
enum class ResourceState : u8 {
    Empty,
    Loading,   // handle is valid, pixels have not arrived yet
    Ready,
    Failed,    // load failed; Get() returns the magenta placeholder
};

const char* ToString(ResourceState state);

struct Texture {
    // Deliberately NOT opaque. Callers hold handles and go through Get(), but
    // once they have the Texture they need its size to lay a sprite out, and
    // the editor's resource browser needs `native` to hand to ImGui::Image -
    // which is the one place a raw platform pointer legitimately crosses the
    // boundary, and it crosses as void*.
    i32           width  = 0;
    i32           height = 0;
    usize         bytes  = 0;      // approximate GPU/CPU footprint
    ResourceState state  = ResourceState::Empty;
    void*         native = nullptr;
};

class ResourceManager {
public:
    static bool Init();

    // ASSERTS THAT EVERY REFCOUNT IS ZERO. Better to fail loudly at shutdown
    // than to leak silently; this single assert found more bugs in Week 9 than
    // any test written that week. Anything still resident is logged with its
    // path and count before the assert fires, so the diagnostic names the
    // culprit rather than just the fact.
    static void Shutdown();

    // Loads if not already loaded; increments the refcount either way;
    // returns a handle. Synchronous.
    static Handle<Texture> AcquireTexture(std::string_view virtualPath);

    // Returns a valid handle IMMEDIATELY, before the bytes have arrived - that
    // is the entire point. The read runs on a worker thread through the Week 5
    // queue; the SURFACE-TO-TEXTURE step is deferred to the main thread by
    // FileSystem::PumpCompletions, because creating a texture touches the
    // renderer and the renderer is not thread-safe.
    static Handle<Texture> AcquireTextureAsync(std::string_view virtualPath);

    // Decrements; unloads at zero. Null and stale handles are ignored with a
    // warning rather than treated as an error - a Release of something already
    // gone is a normal consequence of shutdown ordering.
    static void Release(Handle<Texture> handle);

    // Resolves a handle to the underlying asset.
    //
    // *** MILESTONE 3 REQUIREMENT: *** a STALE handle - one whose generation
    // no longer matches its slot - is DETECTED AND REPORTED with its index and
    // generation, and null is returned. It is never dereferenced, and this
    // does not assert-and-die in release: a missing texture should be a
    // magenta square, not a crash.
    static Texture* Get(Handle<Texture> handle);

    static bool IsValid(Handle<Texture> handle);
    static bool IsReady(Handle<Texture> handle);

    // The magenta placeholder from assets/textures/missing.bmp. Every failed
    // load resolves to it, so a typo in a scene file is loudly visible on
    // screen instead of being an invisible hole.
    static Handle<Texture> MissingTexture();

    // --- instrumentation, for the resource browser and the memory HUD ------
    struct Entry {
        std::string     path;
        u32             refCount = 0;
        ResourceState   state    = ResourceState::Empty;
        i32             width    = 0;
        i32             height   = 0;
        usize           bytes    = 0;
        Handle<Texture> handle{};
    };

    static usize LoadedCount();
    static u64   TotalRefCount();     // on screen: this is how M3 is verified
    static usize BytesResident();
    static void  Snapshot(std::vector<Entry>& out);

    // Called once per frame from the main thread. Turns surfaces that worker
    // threads decoded into textures. Doing this anywhere else is a data race
    // on the renderer.
    static void PumpPendingUploads();

private:
    friend class FileSystemAsyncBridge;
};

} // namespace eng
