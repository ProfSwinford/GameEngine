// =============================================================================
//  SdlHandles.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. SdlHandles.h is the specification; read it first.
//
//  This is the only file in the engine that may include SDL's headers for these
//  four types, which is what keeps SDL out of the public interface.
// =============================================================================

#include <engine/platform/SdlHandles.h>

namespace eng {

// Destroys an SDL window. Called automatically when the WindowPtr holding it
// goes out of scope, so there is no matching "close" call to remember.
void SdlWindowDeleter::operator()(SDL_Window* /*window*/) const noexcept {
}

// Destroys an SDL renderer, the same way and for the same reason.
void SdlRendererDeleter::operator()(SDL_Renderer* /*renderer*/) const noexcept {
}

// Frees an SDL surface - the in-memory picture an image file is read into
// before it is handed to the graphics card.
void SdlSurfaceDeleter::operator()(SDL_Surface* /*surface*/) const noexcept {
}

// Destroys an SDL texture - a picture that already lives on the graphics card.
void SdlTextureDeleter::operator()(SDL_Texture* /*texture*/) const noexcept {
}

} // namespace eng
