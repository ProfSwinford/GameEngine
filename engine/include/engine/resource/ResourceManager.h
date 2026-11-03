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
#include <engine/render/Font.h>
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

    // Loads a .ttf at one particular size, or returns the already-baked one.
    //
    // KEYED ON PATH AND SIZE TOGETHER. A font atlas holds letters drawn at
    // one size; the same file at a different size is a different atlas, and
    // caching on the path alone would hand back 16-pixel letters to whoever
    // asked for 32 and leave them wondering why their text is blurry. See
    // render/Font.h.
    //
    // Never returns null. A file that will not load comes back as a fallback
    // font that draws through the built-in 8x8, so a missing .ttf is ugly
    // rather than invisible.
    static FontRef LoadFont(std::string_view virtualPath, float pixelHeight);

    // The font used by anything that does not name one.
    static FontRef DefaultFont(float pixelHeight);

    // ======================================================================
    //  Re-reads an asset's .oimport and applies it to what is already loaded.
    //
    //  For the EDITOR. Changing the scaling mode of an image has to show up on
    //  the sprites already drawing it, or the setting appears to do nothing
    //  until the next run - which is indistinguishable from broken.
    //
    //  IT TAKES A PATH, NOT A TextureRef, AND THAT IS THE SAFE CHOICE. The
    //  cache only ever holds textures that loaded successfully, so a path
    //  cannot reach the shared magenta placeholder; a TextureRef obtained from
    //  LoadTexture very much can, because that is what a failed load returns.
    //
    //  A FONT IS ONE ASSET AND SEVERAL TEXTURES. The font cache is keyed on
    //  path AND size, so one .ttf may have been baked into atlases at four
    //  sizes, and all four have to be re-filtered. They are found by comparing
    //  each loaded font's own `path` rather than by taking the cache key
    //  apart, because a key is "<path>@<size>" and a path may contain an '@'.
    //
    //  Nothing loaded under that path is not a failure - the settings are
    //  saved either way and the next load will read them.
    // ======================================================================
    static void RefreshImportSettings(std::string_view virtualPath);

    // ======================================================================
    //  Forgets every frame cut out of this sheet, so the next load re-cuts it.
    //
    //  FOR THE SLICER. Re-slicing changes what a frame IS - its rectangle
    //  moves, or the name stops existing - and a frame texture sitting in the
    //  cache would go on being handed out for a name whose pixels have moved.
    //
    //  IT CANNOT REACH WHAT SOMETHING ALREADY OWNS. The texture cache holds
    //  WEAK references, so a SpriteComponent already holding the old frame
    //  keeps drawing it until it asks for it again. That is a real limit and
    //  the Inspector says so rather than pretending otherwise.
    // ======================================================================
    static void InvalidateSubAssets(std::string_view sheetPath);

    // ======================================================================
    //  Reads any loaded image whose FILE has changed since it was loaded.
    //
    //  THE CACHE HAS NO IDEA WHAT TIME IT IS. It is keyed on the path and
    //  holds a weak reference, so while anything is still holding an image,
    //  asking for it again hands back exactly what was loaded the first time -
    //  however many times the file has been saved since. That is right for a
    //  shipped game, where nothing changes under it, and wrong for an editor
    //  somebody is using beside a paint program.
    //
    //  THE PIXELS OF THE EXISTING Texture ARE REPLACED, rather than a new
    //  Texture being handed out. That is the whole point: the object is what
    //  everything holds. A sprite in the open scene, the Assets panel's
    //  thumbnail and the Inspector's preview share one shared_ptr, so refilling
    //  it is what makes one edit appear in all three at once. Handing back a
    //  new object would update whoever asked next and nobody else.
    //
    //  A sheet that changed also has its cut frames dropped, so the next one
    //  asked for is cut from the new pixels. A frame something is ALREADY
    //  holding keeps its own pixels until it is asked for again - the same
    //  limit, for the same reason, as re-slicing. See InvalidateSubAssets.
    //
    //  ONLY SAFE WHERE NOTHING HAS BEEN DRAWN YET. It destroys graphics-card
    //  objects, so a handle already submitted to a frame's draw list would be
    //  freed under the renderer. The editor calls it from the frame loop
    //  before the interface is built; see EditorApp's focus-regain.
    //
    //  Returns how many images were read again, for the log.
    // ======================================================================
    static std::size_t ReloadChangedTextures();

    // Drops every cached .oimport and re-applies them to everything loaded.
    //
    // For the editor's "the window has just regained focus" check, beside the
    // same check for scripts and prefabs. These files are JSON and meant to be
    // opened by hand - that is the whole argument for the format - so one
    // fixed in a text editor has to arrive the same way an edited script does.
    //
    // Re-applying a filter is idempotent and costs one SDL call, so unlike the
    // prefab check this needs no "first sight is not a change" rule.
    static void ReloadImportSettings();

    // How many different images are loaded right now. Shown in the editor.
    static std::size_t LoadedCount();

    // Removes cache entries whose texture has already been let go of. Called
    // once per frame; purely housekeeping, and it frees nothing that was still
    // in use.
    static void PruneCache();
};

} // namespace eng
