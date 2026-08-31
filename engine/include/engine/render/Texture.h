#pragma once

// ============================================================================
//  Texture.h - a loaded image, and the shared pointer used to refer to one.
//
//  A texture is a picture that has been read off disk and handed to the
//  graphics card, ready to be drawn. Sprites in the scene refer to textures;
//  the asset browser previews them; the same picture is usually used by many
//  entities at once.
//
//  WHY std::shared_ptr AND NOT A RAW Texture*
//  "Used by many entities at once" is the whole problem. A raw pointer gives
//  no answer to "when is it safe to unload this?" - if one entity unloads a
//  texture the others are still pointing at it, and drawing through a pointer
//  to something that has been freed is a crash (or worse, silently wrong
//  pixels).
//
//  std::shared_ptr is the standard library's answer. It counts how many
//  owners a thing currently has, and destroys it automatically when the last
//  one lets go. Copy a TextureRef and the count goes up; let one go out of
//  scope and it goes down. Nobody has to remember to unload anything, and
//  there is no way to end up holding a pointer to a texture that is gone.
//
//  You can see the count live in the Inspector, which is the clearest way to
//  understand what shared ownership means.
// ============================================================================

#include <memory>
#include <string>

namespace eng {

struct Texture {
    // Where it was loaded from, e.g. "textures/player.bmp". Kept so the
    // texture can be saved back into a scene file and shown in the Inspector.
    std::string path;

    int width  = 0;
    int height = 0;

    // The graphics-card object SDL created for this image.
    //
    // Stored as void* so that this header does not have to mention SDL. The
    // drawing code casts it back; nothing else touches it.
    void* native = nullptr;

    // True when this is the stand-in magenta square shown for an image that
    // could not be loaded. The Inspector uses it to say so in red rather than
    // leaving you to wonder why your sprite is magenta.
    bool isPlaceholder = false;

    // Destroys the graphics-card object. Called automatically by shared_ptr
    // when the last owner lets go.
    ~Texture();
};

// The type everything else uses. Read it as "a shared reference to a texture".
using TextureRef = std::shared_ptr<Texture>;

} // namespace eng
