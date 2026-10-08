#pragma once

// ============================================================================
//  ResourceManager.h - loads image files and hands out shared textures.
//
//  WHAT IT DOES
//  Ask it for "textures/player.bmp" and you get a TextureRef back. Ask for the
//  same path again and you get the SAME texture, not a second copy of it - the
//  file is read from disk once no matter how many entities use it.
//
//  HOW OWNERSHIP WORKS
//  A TextureRef is a std::shared_ptr (see render/Texture.h). Every sprite that
//  uses a texture holds one, and the texture stays loaded for exactly as long
//  as at least one of them exists. Delete the last entity using a picture and
//  the picture unloads by itself.
//
//  Nothing has to be released by hand, and there is no way to end up holding a
//  reference to a texture that has already been thrown away. That whole class
//  of bug simply does not exist here, which is why shared_ptr is worth using
//  even though "manually count the users" would also work.
//
//  THE CACHE
//  Internally there is a table from path to texture, so that asking twice
//  gives the same answer. It stores WEAK references - see the .cpp - so being
//  in the cache does not by itself keep a texture loaded.
//
//  IMAGE FORMATS: .png, .bmp and .jpg
//  .png is the one to use. It is what every image editor saves by default, it
//  compresses properly, and it is the only one of the three that stores
//  TRANSPARENCY - which is what you want for a sprite that is not a rectangle.
//
//  SDL itself can only read .bmp, so the decoding is done by stb_image: one
//  public-domain header file, no library to build and no extra .dll beside a
//  finished game. See src/resource/StbImage.cpp.
//
//  Whatever the file holds - 24-bit colour, a palette, greyscale - it arrives
//  here as red, green, blue and alpha, one byte each. A format with no
//  transparency of its own simply comes out fully opaque.
// ============================================================================

#include <engine/core/Subsystem.h>
#include <engine/render/Texture.h>

#include <string>
#include <string_view>

namespace eng {

class ResourceManager : public Subsystem {
public:
    // Nothing in the settings affects the texture cache, so the config is
    // accepted and ignored.
    bool Init(const BootConfig& config) override;

    // Reports anything still loaded, then clears the cache. Textures still
    // being used by something at this point are named in the log, because that
    // almost always means a scene was not unloaded.
    void Shutdown() override;

    // Loads an image, or returns the already-loaded one.
    //
    // On failure this returns the magenta placeholder rather than an empty
    // reference, so a mistyped filename shows up as an obviously wrong square
    // on screen instead of an invisible gap that is easy to miss.
    static TextureRef LoadTexture(std::string_view virtualPath);

    // The magenta "this did not load" square, from assets/textures/missing.bmp.
    static TextureRef MissingTexture();

    // How many different images are loaded right now. Shown in the editor.
    static std::size_t LoadedCount();

    // Removes cache entries whose texture has already been let go of. Called
    // once per frame; purely housekeeping, and it frees nothing that was still
    // in use.
    static void PruneCache();
};

} // namespace eng
