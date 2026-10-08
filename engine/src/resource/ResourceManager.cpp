// ============================================================================
//  ResourceManager.cpp - loading images. See ResourceManager.h.
//
//  The whole file is about one table:
//
//      std::unordered_map<std::string, std::weak_ptr<Texture>> g_cache;
//
//  std::unordered_map is the standard hash table: given a key (here, the
//  file's path) it finds the matching value quickly no matter how many entries
//  there are. It is the right container for "look this up by name".
//
//  The value is a std::WEAK_ptr rather than a shared_ptr, and that choice is
//  the interesting part. A shared_ptr counts as an owner, so a cache full of
//  them would keep every texture ever loaded alive until the program exited -
//  a cache that is also a leak. A weak_ptr watches a shared_ptr without owning
//  it: it can tell you whether the thing is still alive and hand you a real
//  reference if it is, but it does not by itself keep it alive.
//
//  So: the sprites own the textures, and the cache only remembers where they
//  are.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/platform/SdlHandles.h>
#include <engine/render/Renderer.h>
#include <engine/resource/ResourceManager.h>

#include <SDL3/SDL.h>

// Declarations only. The decoder itself is compiled once, in StbImage.cpp.
#define STBI_NO_STDIO
#include <stb_image.h>

#include <unordered_map>
#include <vector>

namespace eng {
namespace {

// path -> the texture loaded from it, if it is still alive somewhere.
std::unordered_map<std::string, std::weak_ptr<Texture>> g_cache;

// The magenta placeholder. This one IS a shared_ptr: the engine deliberately
// keeps it loaded for the whole run, because it is needed exactly when things
// are going wrong and that is a bad moment to be reading a file.
TextureRef g_missing;

bool g_initialised = false;
bool g_loadingMissing = false;   // stops MissingTexture recursing if it fails

// Reads an image file and hands the pixels to the graphics card.
// Returns nullptr and fills in outError if anything goes wrong.
TextureRef CreateTextureFromFile(std::string_view virtualPath, std::string& outError) {
    std::vector<unsigned char> bytes;
    if (!FileSystem::ReadFile(virtualPath, bytes, outError)) {
        return nullptr;
    }

    // stb_image turns those bytes into pixels. It is handed the bytes the
    // engine already read rather than a filename, which is what keeps every
    // path in the engine going through FileSystem::Resolve.
    //
    // The last argument is the number of channels to produce, and asking for 4
    // is the whole reason this is simple: whatever the file actually holds -
    // greyscale, 24-bit colour, a palette - stb converts it to red, green,
    // blue, alpha, one byte each. An image with no transparency of its own
    // comes back fully opaque, so there is only ever one pixel layout to deal
    // with below.
    int width          = 0;
    int height         = 0;
    int channelsInFile = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                              &height, &channelsInFile, 4);
    if (pixels == nullptr) {
        // stbi_failure_reason is stb's equivalent of SDL_GetError: a short
        // sentence about the last thing that went wrong.
        outError = stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                    : "the image could not be decoded";
        return nullptr;
    }

    auto* renderer = static_cast<SDL_Renderer*>(Renderer::NativeRendererHandle());
    if (renderer == nullptr) {
        stbi_image_free(pixels);
        outError = "there is no renderer yet, so textures cannot be created";
        return nullptr;
    }

    // Pixels in ordinary memory have to be copied to where the graphics card
    // can draw them quickly. That is these two calls: make a texture of the
    // right size and format, then fill it in.
    //
    // SDL_PIXELFORMAT_RGBA32 is SDL's name for "one byte each of red, green,
    // blue and alpha, in the order this machine expects" - exactly what stb was
    // asked for above.
    SDL_Texture* native = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC, width, height);
    if (native == nullptr) {
        stbi_image_free(pixels);
        outError = SDL_GetError();
        return nullptr;
    }

    // The pitch is how many bytes one row of the image takes: four per pixel.
    const bool uploaded = SDL_UpdateTexture(native, nullptr, pixels, width * 4);

    // The pixels have been copied into the texture, so stb's buffer is finished
    // with. Freed HERE, before anything can return, because it was allocated by
    // stb rather than owned by a unique_ptr like the rest of the engine's SDL
    // objects - this is the one place in the engine with a raw allocation to
    // keep track of.
    stbi_image_free(pixels);

    if (!uploaded) {
        SDL_DestroyTexture(native);
        outError = SDL_GetError();
        return nullptr;
    }

    // NEAREST rather than smooth scaling. The test images are checkerboards,
    // and smoothing turns a crisp 32x32 checker into grey mush as soon as it
    // is enlarged. It is also what pixel-art games want.
    SDL_SetTextureScaleMode(native, SDL_SCALEMODE_NEAREST);

    // std::make_shared builds the object and its reference count together in
    // one allocation. It is the preferred way to create a shared_ptr.
    TextureRef texture = std::make_shared<Texture>();
    texture->path   = std::string(virtualPath);
    texture->width  = width;
    texture->height = height;
    texture->native = native;
    return texture;
}

} // namespace

// The destructor declared in render/Texture.h. It lives here because this is
// where SDL is available, and it is what makes "the last owner lets go" turn
// into "the graphics card memory is freed".
Texture::~Texture() {
    if (native != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(native));
        native = nullptr;
    }
}

bool ResourceManager::Init(const BootConfig&) {
    g_initialised = true;
    ENGINE_LOG_INFO(Channels::kResource, "resource manager ready");
    return true;
}

void ResourceManager::Shutdown() {
    // Let go of the placeholder first, so it does not appear in the report
    // below as something that failed to unload.
    g_missing.reset();

    PruneCache();

    // Anything left in the cache is a texture something is still holding on
    // to. That is almost always a scene that was never unloaded, so name the
    // files rather than just the fact.
    for (const auto& [path, weak] : g_cache) {
        if (const TextureRef alive = weak.lock()) {
            // use_count() is how many shared_ptrs currently own this texture.
            // The -1 discounts the temporary `alive` created on the line above.
            ENGINE_LOG_WARN(Channels::kResource,
                            "'{}' is still in use by {} thing(s) at shutdown", path,
                            alive.use_count() - 1);
        }
    }

    g_cache.clear();
    g_initialised = false;
    ENGINE_LOG_INFO(Channels::kResource, "resource manager shut down");
}

TextureRef ResourceManager::LoadTexture(std::string_view virtualPath) {
    const std::string key(virtualPath);

    // Already loaded, and still alive somewhere?
    //
    // weak_ptr::lock() is the one operation that matters here: it returns a
    // real shared_ptr if the object is still alive, or an empty one if the
    // last owner has already let go. Checking it is how the cache tells the
    // difference between "loaded" and "was loaded once".
    if (g_cache.contains(key)) {
        if (TextureRef existing = g_cache.at(key).lock()) {
            return existing;
        }
        g_cache.erase(key);   // it was unloaded; forget the stale entry
    }

    std::string error;
    TextureRef  texture = CreateTextureFromFile(virtualPath, error);
    if (!texture) {
        ENGINE_LOG_ERROR(Channels::kResource, "could not load '{}': {}", virtualPath,
                         error);
        // Returning the magenta square rather than nothing, so the mistake is
        // visible on screen. Silence would just leave a gap where a sprite
        // should be, which is far easier to overlook.
        return MissingTexture();
    }

    ENGINE_LOG_INFO(Channels::kResource, "loaded '{}' ({}x{})", virtualPath,
                    texture->width, texture->height);

    g_cache[key] = texture;   // stores a weak reference; see the file header
    return texture;
}

TextureRef ResourceManager::MissingTexture() {
    if (g_missing || g_loadingMissing) {
        return g_missing;
    }

    // The guard matters: if the placeholder image itself is missing,
    // LoadTexture would call this function again to report the failure, and
    // round it would go forever.
    g_loadingMissing = true;

    std::string error;
    g_missing = CreateTextureFromFile("textures/missing.bmp", error);
    if (g_missing) {
        g_missing->isPlaceholder = true;
    } else {
        ENGINE_LOG_WARN(Channels::kResource,
                        "the 'missing texture' placeholder could not be loaded itself "
                        "({}); failed sprites will draw as nothing at all", error);
    }

    g_loadingMissing = false;
    return g_missing;
}

std::size_t ResourceManager::LoadedCount() {
    std::size_t count = 0;
    for (const auto& [path, weak] : g_cache) {
        // expired() is the cheap "is it gone?" question - it does not build a
        // shared_ptr the way lock() does.
        if (!weak.expired()) {
            ++count;
        }
    }
    return count;
}

void ResourceManager::PruneCache() {
    // std::erase_if works on maps as well as vectors, and removing entries
    // this way is safe - doing it with a loop and erase() while iterating is
    // where erasing from a container usually goes wrong.
    std::erase_if(g_cache, [](const auto& entry) {
        return entry.second.expired();
    });
}

} // namespace eng
