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
#include <engine/resource/ImportSettings.h>
#include <engine/resource/ResourceManager.h>

#include <SDL3/SDL.h>

// Declarations only. The decoder itself is compiled once, in StbImage.cpp.
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace eng {
namespace {

// path -> the texture loaded from it, if it is still alive somewhere.
std::unordered_map<std::string, std::weak_ptr<Texture>> g_cache;

// The write time each cached image had when it was read, keyed the same way.
//
// A SEPARATE TABLE RATHER THAN A FIELD ON Texture, because it is nobody
// else's business: render/Texture.h describes what the graphics card holds, and
// when a file was last saved is a question only this file asks. See
// ReloadChangedTextures.
std::unordered_map<std::string, long long> g_loadedAt;

// The magenta placeholder. This one IS a shared_ptr: the engine deliberately
// keeps it loaded for the whole run, because it is needed exactly when things
// are going wrong and that is a bad moment to be reading a file.
TextureRef g_missing;

bool g_initialised = false;
bool g_loadingMissing = false;   // stops MissingTexture recursing if it fails

// ---------------------------------------------------------------------------
//  ONE SHEET'S PIXELS, kept just long enough to cut several frames out of it.
//
//  THIS IS A REQUIREMENT AND NOT AN OPTIMISATION, which is worth stating
//  because it looks like one. Cutting a frame out of a spritesheet needs the
//  sheet's pixels, and there is no partial decode: getting them means reading
//  the whole file and decoding the whole image. A 12x8 sheet is 96 frames, so
//  without this, expanding that sheet in the Assets panel - or loading one
//  animation - would read and decode the same PNG ninety-six times, on the UI
//  thread, inside one call.
//
//  ONE SLOT, not a map, and that is deliberate too. The access pattern this
//  exists for is a burst against ONE sheet: slice it, load its frames, move
//  on. A single slot serves that exactly, cannot grow without bound, and
//  needs no eviction policy to argue about. Two sheets alternating would
//  thrash it and still be correct.
//
//  It holds a std::vector rather than stb's own pointer so that every refusal
//  below is a plain return. The raw allocation in CreateTextureFromFile is
//  the only one in this file for a reason.
// ---------------------------------------------------------------------------
struct DecodedSheet {
    std::string                path;
    int                        width  = 0;
    int                        height = 0;
    std::vector<unsigned char> rgba;   // tightly packed, four bytes per pixel
};

DecodedSheet g_decoded;

// Reads an image file and hands the pixels to the graphics card.
// Returns nullptr and fills in outError if anything goes wrong.
// ---------------------------------------------------------------------------
//  Decodes a file and puts the result INTO an existing Texture.
//
//  Split out of CreateTextureFromFile so that an image which has changed on
//  disk can be read again WITHOUT making a new Texture object - because the
//  object is what everything holds. A sprite in the scene, the Assets panel's
//  thumbnail and the Inspector's preview all share one shared_ptr, and
//  replacing its contents is what makes an edit appear in all three at once.
//  Handing back a new object would leave every existing holder on the old one.
//
//  THE OLD IMAGE IS DESTROYED ONLY ONCE THE NEW ONE EXISTS. A file saved half
//  way through, or replaced with something undecodable, leaves the picture that
//  was already on screen alone and says why. The alternative - free first, then
//  fail - turns a transient bad read into a hole.
// ---------------------------------------------------------------------------
bool UploadFileInto(Texture& texture, std::string_view virtualPath,
                    TextureFilter filter, std::string& outError);

TextureRef CreateTextureFromFile(std::string_view virtualPath, TextureFilter filter,
                                 std::string& outError) {
    TextureRef texture = std::make_shared<Texture>();
    if (!UploadFileInto(*texture, virtualPath, filter, outError)) {
        return nullptr;
    }
    texture->path = std::string(virtualPath);
    return texture;
}

bool UploadFileInto(Texture& texture, std::string_view virtualPath,
                    TextureFilter filter, std::string& outError) {
    std::vector<unsigned char> bytes;
    if (!FileSystem::ReadFile(virtualPath, bytes, outError)) {
        return false;
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
        return false;
    }

    auto* renderer = static_cast<SDL_Renderer*>(Renderer::NativeRendererHandle());
    if (renderer == nullptr) {
        stbi_image_free(pixels);
        outError = "there is no renderer yet, so textures cannot be created";
        return false;
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
        return false;
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
        return false;
    }

    // HOW THIS IMAGE ASKED TO BE SCALED, which used to be NEAREST for
    // everything. Nearest is still what it gets when nothing says otherwise:
    // the test images are checkerboards, and smoothing turns a crisp 32x32
    // checker into grey mush as soon as it is enlarged. It is also what
    // pixel-art games want, which is why it stayed the default when the choice
    // became an asset's to make. See engine/resource/ImportSettings.h.
    //
    // PASSED IN rather than looked up here, because a sliced frame's settings
    // belong to its SHEET and this function has only ever seen a real file.
    SDL_SetTextureScaleMode(native, filter == TextureFilter::Linear
                                        ? SDL_SCALEMODE_LINEAR
                                        : SDL_SCALEMODE_NEAREST);

    // NOW the old one goes, with the new one already built. See the note above.
    if (texture.native != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture.native));
    }
    texture.width         = width;
    texture.height        = height;
    texture.native        = native;
    texture.isPlaceholder = false;
    return true;
}

// ---------------------------------------------------------------------------
//  Decodes a sheet, or hands back the one already decoded. Null on failure.
// ---------------------------------------------------------------------------
const DecodedSheet* DecodeSheet(std::string_view sheetPath, std::string& outError) {
    if (g_decoded.path == sheetPath && !g_decoded.rgba.empty()) {
        return &g_decoded;
    }

    std::vector<unsigned char> bytes;
    if (!FileSystem::ReadFile(sheetPath, bytes, outError)) {
        return nullptr;
    }

    // WHEN THIS SHEET WAS LAST WRITTEN, recorded even though the sheet itself
    // gets no Texture of its own. A sheet used only through its frames never
    // reaches the texture cache - the frames are cut from these raw pixels -
    // so without this there would be nothing for ReloadChangedTextures to
    // compare, and repainting a spritesheet in a paint program would be the
    // one edit the editor could not notice.
    g_loadedAt[std::string(sheetPath)] = FileSystem::ModifiedAt(sheetPath);

    int width          = 0;
    int height         = 0;
    int channelsInFile = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                              &height, &channelsInFile, 4);
    if (pixels == nullptr) {
        outError = stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                    : "the image could not be decoded";
        return nullptr;
    }

    // COPIED OUT AND FREED IMMEDIATELY, so that nothing below this line has to
    // remember a raw allocation. The copy is the whole sheet once; the thing it
    // buys is not having a leak on each of the refusal paths in CopyFrameOut.
    const std::size_t total = static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u;
    g_decoded.path   = std::string(sheetPath);
    g_decoded.width  = width;
    g_decoded.height = height;
    g_decoded.rgba.assign(pixels, pixels + total);
    stbi_image_free(pixels);

    return &g_decoded;
}

// ---------------------------------------------------------------------------
//  Cuts one frame's rectangle out of a decoded sheet.
//
//  ==========================================================================
//  THE ONLY PLACE IN THIS FEATURE THAT CAN CORRUPT MEMORY, so every check is
//  here and every one of them is explained.
//
//  The rectangle comes from a .oimport, which is a text file a person can
//  edit. ImportSettings already refuses a frame with no area or a negative
//  corner when it READS one, and this checks again anyway: that validation is
//  one file away from here, and the cost of being wrong is reading somebody
//  else's memory.
//
//  THE RANGE TEST IS A SUBTRACTION, NOT AN ADDITION. Written the obvious way -
//  `frame.x + frame.w <= sheet.width` - it is signed overflow for a large
//  hand-edited w, which is undefined behaviour, and a compiler is entitled to
//  assume it cannot happen and delete the check. Subtracting cannot overflow
//  here because w and h have already been established as positive and the
//  sheet's own dimensions come from the decoder.
// ---------------------------------------------------------------------------
bool CopyFrameOut(const DecodedSheet& sheet, const SheetFrame& frame,
                  std::vector<unsigned char>& out, std::string& outError) {
    if (sheet.width <= 0 || sheet.height <= 0) {
        outError = "the sheet has no size";
        return false;
    }
    if (frame.w <= 0 || frame.h <= 0 || frame.x < 0 || frame.y < 0) {
        outError = "the frame has no area, or a negative corner";
        return false;
    }
    if (frame.w > sheet.width || frame.h > sheet.height ||
        frame.x > sheet.width - frame.w || frame.y > sheet.height - frame.h) {
        outError = "the frame falls outside the image";
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(frame.w) * 4u;
    out.assign(rowBytes * static_cast<std::size_t>(frame.h), 0);

    for (int row = 0; row < frame.h; ++row) {
        const std::size_t srcRow = static_cast<std::size_t>(frame.y + row);
        const std::size_t src    = (srcRow * static_cast<std::size_t>(sheet.width) +
                                 static_cast<std::size_t>(frame.x)) * 4u;
        const std::size_t dst = static_cast<std::size_t>(row) * rowBytes;
        std::memcpy(out.data() + dst, sheet.rgba.data() + src, rowBytes);
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Builds the texture for one sub-asset: "textures/hero.png#walk_1".
// ---------------------------------------------------------------------------
TextureRef CreateSubAssetTexture(std::string_view path, const TextureImport& settings,
                                 std::string& outError) {
    const std::string sheetPath = SheetPathOf(path);
    const std::string name      = SubAssetNameOf(path);

    // A FRAME OR AN ANIMATION, decided by the sheet. THE RULE USED TO BE
    // WRITTEN OUT HERE - frame, else the animation's first surviving frame -
    // and it now lives on SheetSlice, because eng::ImageExists has to answer
    // the same question for the Inspector's red and two copies that drifted
    // would mark a picture broken while it drew perfectly well.
    const SheetFrame* frame = settings.sheet.FindImage(name);
    if (frame == nullptr) {
        outError = settings.sheet.IsSliced()
                       ? "'" + sheetPath + "' has no part called '" + name + "'"
                       : "'" + sheetPath + "' has not been sliced into parts";
        return nullptr;
    }

    const DecodedSheet* sheet = DecodeSheet(sheetPath, outError);
    if (sheet == nullptr) {
        return nullptr;
    }

    std::vector<unsigned char> rgba;
    if (!CopyFrameOut(*sheet, *frame, rgba, outError)) {
        return nullptr;
    }

    TextureRef texture = Renderer::CreateTextureFromPixels(
        rgba.data(), frame->w, frame->h, settings.filter == TextureFilter::Linear,
        outError);
    if (!texture) {
        return nullptr;
    }

    // THE WHOLE SUB-ASSET PATH, so that the Inspector and the shutdown report
    // name the frame rather than the sheet it came out of.
    texture->path = std::string(path);
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
    g_loadedAt.clear();

    // The decoded sheet too - it is up to a few megabytes of pixels kept for
    // slicing, and nothing is going to ask for another frame after this.
    g_decoded = DecodedSheet{};

    // The .oimport cache goes with them. One project's "textures/hero.png" is
    // not another's, so a cache keyed on the virtual path must not outlive the
    // project it was filled from - see ImportLibrary::Clear.
    ImportLibrary::Clear();

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

    // THE SETTINGS BELONG TO THE FILE, and for a sliced frame the file is its
    // SHEET. Looked up once here and passed down, so that nothing below asks
    // ImportLibrary about a '#' path on the ordinary route - which would cache
    // an entry holding the defaults under a name no sidecar can ever have, and
    // report every frame of a Linear sheet as Nearest.
    //
    // EXACTLY ONE PLACE BELOW BREAKS THAT, DELIBERATELY: the fallback for a
    // real file whose own name contains a '#'. There the '#' path IS a file on
    // disk, so "a.png#b.oimport" is a sidecar it can legitimately have, and
    // its own settings are the right ones. See the note on the fallback.
    const std::string   sheetPath = SheetPathOf(virtualPath);
    const TextureImport settings  = ImportLibrary::ForTexture(sheetPath);

    // Which settings the texture was ACTUALLY made with, which is what the log
    // line at the end has to report. The two differ only on that one fallback,
    // and reporting the sheet's filter for a file that is not part of the
    // sheet is the kind of wrong answer this log exists to prevent.
    TextureFilter usedFilter = settings.filter;

    std::string error;
    TextureRef  texture;
    if (IsSubAsset(virtualPath)) {
        texture = CreateSubAssetTexture(virtualPath, settings, error);

        // ==================================================================
        //  THE ONE AMBIGUITY THE SPLIT RULE CANNOT SETTLE BY ITSELF.
        //
        //  '#' is a legal filename character, so a file genuinely named
        //  "a.png#b" can exist - the editor can even produce one. The rule in
        //  ImportSettings.h reads that as frame "b" of the sheet "a.png",
        //  which is right far more often than not, and this is the way back
        //  for when it is wrong.
        //
        //  The disk is only touched when the sub-asset reading has ALREADY
        //  FAILED, so the ordinary path pays nothing for it and a real file is
        //  never shadowed by a frame that does not exist.
        // ==================================================================
        if (!texture && FileSystem::Exists(virtualPath)) {
            // ITS OWN SETTINGS, NOT THE SHEET'S. This path is a real file, so
            // it can have a sidecar of its own, and the sheet it was mistaken
            // for has nothing to do with it. Recorded in usedFilter so the log
            // below says what was actually applied.
            usedFilter = ImportLibrary::ForTexture(virtualPath).filter;
            texture    = CreateTextureFromFile(virtualPath, usedFilter, error);
        }
    } else {
        texture = CreateTextureFromFile(virtualPath, settings.filter, error);
    }

    if (!texture) {
        ENGINE_LOG_ERROR(Channels::kResource, "could not load '{}': {}", virtualPath,
                         error);
        // Returning the magenta square rather than nothing, so the mistake is
        // visible on screen. Silence would just leave a gap where a sprite
        // should be, which is far easier to overlook.
        return MissingTexture();
    }

    // THE SCALING MODE IS IN THE LINE, because "why is my sprite blurry?" is a
    // question this log is the fastest answer to - and the setting lives in a
    // file beside the image that somebody may not know exists.
    ENGINE_LOG_INFO(Channels::kResource, "loaded '{}' ({}x{}, scaled {})", virtualPath,
                    texture->width, texture->height, TextureFilterName(usedFilter));

    g_cache[key] = texture;   // stores a weak reference; see the file header

    // WHEN THE FILE SAID IT WAS LAST WRITTEN, so that a later visit can tell
    // whether it has been edited since. Recorded for a real file only: a
    // frame's pixels come from its sheet, which has its own entry.
    if (!IsSubAsset(virtualPath)) {
        g_loadedAt[key] = FileSystem::ModifiedAt(virtualPath);
    }
    return texture;
}

// ---------------------------------------------------------------------------
//  Fonts
// ---------------------------------------------------------------------------
//
//  The same weak-reference cache as textures, with one difference: the key is
//  the path AND the size. See the note in ResourceManager.h.
// ---------------------------------------------------------------------------

FontRef BakeFont(std::string_view virtualPath, float pixelHeight, bool smooth,
                 std::string& outError);

namespace {
std::unordered_map<std::string, std::weak_ptr<Font>> g_fontCache;

std::string FontKey(std::string_view path, float pixelHeight) {
    // Rounded to a whole pixel, because that is the only resolution an atlas
    // has: asking for 15.9 and asking for 16 must not bake two atlases that
    // are indistinguishable on screen.
    return std::string(path) + "@" + std::to_string(static_cast<int>(pixelHeight + 0.5f));
}
} // namespace

FontRef ResourceManager::LoadFont(std::string_view virtualPath, float pixelHeight) {
    if (pixelHeight < 1.0f) {
        pixelHeight = 1.0f;
    }
    const std::string key = FontKey(virtualPath, pixelHeight);

    if (g_fontCache.contains(key)) {
        if (FontRef existing = g_fontCache.at(key).lock()) {
            return existing;
        }
        g_fontCache.erase(key);
    }

    // SMOOTH UNLESS THE FONT ASKS NOT TO BE, which is the opposite default to
    // a texture's and deliberately so - a letter is a curve approximated into
    // coverage values, and drawing those unfiltered throws away the very
    // information that makes it look like a curve. A font drawn AS pixel art
    // is the exception, and it says so in its own .oimport.
    //
    // RESOLVED HERE rather than inside BakeFont, so that engine/src/render
    // does not have to know that engine/src/resource exists.
    const FontImport settings = ImportLibrary::ForFont(virtualPath);

    std::string error;
    FontRef     font =
        BakeFont(virtualPath, pixelHeight, settings.filter == TextureFilter::Linear,
                 error);

    if (font->isFallback && !virtualPath.empty()) {
        // AT ERROR, because the project's log threshold discards warnings and
        // "my text is the wrong shape" is otherwise unexplainable.
        //
        // AN EMPTY PATH IS NOT A FAILURE and says nothing. "No font assigned"
        // is the documented default - it means the built-in one - so every
        // label in a scene would otherwise report an error for working
        // exactly as intended, which is how a log becomes something people
        // stop reading.
        ENGINE_LOG_ERROR(Channels::kResource,
                         "could not use the font '{}': {} - falling back to the "
                         "built-in one", virtualPath, error);
    } else if (!font->isFallback) {
        ENGINE_LOG_INFO(Channels::kResource, "baked font '{}' at {}px ({}x{} atlas)",
                        virtualPath, static_cast<int>(pixelHeight),
                        font->atlas ? font->atlas->width : 0,
                        font->atlas ? font->atlas->height : 0);
    }

    g_fontCache[key] = font;
    return font;
}

FontRef ResourceManager::DefaultFont(float pixelHeight) {
    // An empty path never resolves to a file, so this is a fallback font -
    // which draws through the built-in 8x8. That is the point: a UI object
    // with no font assigned still shows its text, and assigning a .ttf is an
    // improvement rather than a requirement.
    return LoadFont("", pixelHeight);
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
    g_missing = CreateTextureFromFile(
        "textures/missing.bmp",
        ImportLibrary::ForTexture("textures/missing.bmp").filter, error);
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

void ResourceManager::RefreshImportSettings(std::string_view virtualPath) {
    const std::string key(virtualPath);

    // ---- the image itself --------------------------------------------------
    //
    // THROUGH THE CACHE, which is what keeps this away from the shared
    // placeholder: the insert in LoadTexture is only reached once a file has
    // actually loaded, so no failed path has an entry here. The lock() is not
    // a formality either - entries are pruned lazily, so a key whose texture
    // has already been let go of is an ordinary thing to find.
    const TextureImport settings = ImportLibrary::ForTexture(SheetPathOf(virtualPath));
    const bool          smooth   = (settings.filter == TextureFilter::Linear);

    // THE IMAGE AND EVERY FRAME CUT OUT OF IT. A frame's texture is cached
    // under "<sheet>#<name>", so a prefix match finds all of them - and
    // without it, choosing a filter for a sliced sheet would change the sheet
    // and leave every frame in the scene as it was, which is indistinguishable
    // from the setting not working.
    //
    // A PREFIX IS SAFE HERE in a way the font cache's '@' is not: a frame name
    // cannot contain the separator (IsLegalSubAssetName refuses it), so
    // "<key>#" cannot match a different sheet's frame.
    const std::string framePrefix = key + kSubAssetSeparator;
    for (const auto& [cachedKey, weak] : g_cache) {
        if (cachedKey != key && !cachedKey.starts_with(framePrefix)) {
            continue;
        }
        if (const TextureRef texture = weak.lock()) {
            Renderer::SetTextureFilter(texture, smooth);
        }
    }

    // ---- every atlas baked from this font ----------------------------------
    //
    // BY COMPARING EACH FONT'S OWN PATH, never by taking a cache key apart.
    // The keys here are "<path>@<size>" and a folder may perfectly well have
    // an '@' in its name, so splitting on it would quietly match the wrong
    // font - or none.
    const FontImport fontSettings = ImportLibrary::ForFont(SheetPathOf(virtualPath));
    for (const auto& [cacheKey, weak] : g_fontCache) {
        const FontRef font = weak.lock();
        if (!font || font->path != key) {
            continue;
        }
        // A fallback font has no atlas of its own - it draws through the
        // built-in 8x8 - so there is nothing to filter. Guarded for the same
        // reason Renderer::DrawTextFont guards it.
        Renderer::SetTextureFilter(font->atlas,
                                   fontSettings.filter == TextureFilter::Linear);
    }
}

void ResourceManager::InvalidateSubAssets(std::string_view sheetPath) {
    const std::string prefix = std::string(sheetPath) + kSubAssetSeparator;

    // A frame name cannot contain the separator - IsLegalSubAssetName refuses
    // it - so this prefix cannot reach another sheet's frames.
    std::erase_if(g_cache, [&prefix](const auto& entry) {
        return entry.first.starts_with(prefix);
    });

    // And the decoded pixels, because the slicer has just changed what they
    // are going to be cut into. One slot, so this is the whole cache.
    if (g_decoded.path == sheetPath) {
        g_decoded = DecodedSheet{};
    }
}

std::size_t ResourceManager::ReloadChangedTextures() {
    // Collected first, then acted on: the reload replaces a Texture's contents
    // and drops sub-asset entries from g_cache, and walking a container while
    // something inside it erases from that container is a habit worth not
    // forming even where it happens to be safe.
    struct Candidate {
        std::string path;
        TextureRef  texture;
        long long   loadedAt = 0;
    };
    std::vector<Candidate> candidates;

    // ======================================================================
    //  WALKED OVER WHAT HAS BEEN READ, not over what is cached.
    //
    //  g_loadedAt holds every real file this manager has opened, which is not
    //  the same set as the textures it is holding: a spritesheet used only
    //  through its frames never gets a Texture of its own, because each frame
    //  is cut from the raw decoded pixels. Walking the texture cache would
    //  therefore miss exactly the file this whole feature is about.
    //
    //  A frame's own key is never in here - see where it is recorded - so
    //  there is no sub-asset test to make.
    // ======================================================================
    for (const auto& [path, loadedAt] : g_loadedAt) {
        TextureRef live;
        if (const auto found = g_cache.find(path); found != g_cache.end()) {
            live = found->second.lock();
        }
        // A placeholder is not this file's picture, so refilling it would
        // replace the magenta square shared by everything that failed.
        if (live && live->isPlaceholder) {
            continue;
        }
        candidates.push_back(Candidate{path, std::move(live), loadedAt});
    }

    std::size_t reloaded = 0;
    for (const Candidate& candidate : candidates) {
        const long long now = FileSystem::ModifiedAt(candidate.path);
        if (now == 0 || now == candidate.loadedAt) {
            continue;
        }

        // ITS OWN SETTINGS AGAIN, because a sidecar may have been edited in the
        // same visit to the paint program.
        const TextureImport settings = ImportLibrary::ForTexture(candidate.path);

        // NO TEXTURE OF ITS OWN - a sheet only ever sliced. Nothing to refill,
        // but its frames and its decoded pixels are now wrong, which the two
        // lines at the bottom of this loop deal with.
        if (!candidate.texture) {
            g_loadedAt[candidate.path] = now;
            ++reloaded;
            ENGINE_LOG_INFO(Channels::kResource,
                            "'{}' changed on disk; its frames will be cut again",
                            candidate.path);
            InvalidateSubAssets(candidate.path);
            continue;
        }

        std::string error;
        if (!UploadFileInto(*candidate.texture, candidate.path, settings.filter,
                            error)) {
            // THE OLD PICTURE IS STILL THERE - UploadFileInto only destroys it
            // once the new one exists - so this is a report rather than a
            // failure. Saving a large PNG is not atomic, and catching one half
            // written must not blank the sprite.
            ENGINE_LOG_WARN(Channels::kResource,
                            "'{}' changed on disk but could not be read again: {}",
                            candidate.path, error);
            continue;
        }

        g_loadedAt[candidate.path] = now;
        ++reloaded;

        ENGINE_LOG_INFO(Channels::kResource,
                        "re-read '{}' ({}x{}, scaled {}) - it changed on disk",
                        candidate.path, candidate.texture->width,
                        candidate.texture->height,
                        TextureFilterName(settings.filter));

        // Anything cut OUT of it is now cut from the wrong pixels. This also
        // drops the decoded copy, so the next frame asked for re-reads the file.
        InvalidateSubAssets(candidate.path);
    }

    return reloaded;
}

void ResourceManager::ReloadImportSettings() {
    ImportLibrary::Clear();

    // COLLECTED FIRST, then acted on. RefreshImportSettings reads both caches
    // itself, and a loop that walks a container while something inside it
    // re-enters that container is a habit worth not forming even where it
    // happens to be safe.
    std::vector<std::string> paths;
    for (const auto& [path, weak] : g_cache) {
        if (!weak.expired()) {
            paths.push_back(path);
        }
    }

    // One font asset is SEVERAL cache entries - one per size it was baked at -
    // and RefreshImportSettings already does all of them for a given path, so
    // the same path is only worth collecting once.
    for (const auto& [cacheKey, weak] : g_fontCache) {
        const FontRef font = weak.lock();
        if (!font || font->path.empty()) {
            continue;
        }
        bool already = false;
        for (const std::string& seen : paths) {
            if (seen == font->path) {
                already = true;
                break;
            }
        }
        if (!already) {
            paths.push_back(font->path);
        }
    }

    for (const std::string& path : paths) {
        RefreshImportSettings(path);
    }
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
