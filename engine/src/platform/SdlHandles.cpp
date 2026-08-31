// WEEK 3 - the deleters. This is the one translation unit where the SDL types
// are complete and where the destroy functions are named. See SdlHandles.h.
//
// SDL's destroy functions all accept null and do nothing, which is why none of
// these needs a guard - and is worth having gone and READ rather than guessed,
// because "it probably handles null" is how half-constructed objects become
// crashes at shutdown.

#include <engine/platform/SdlHandles.h>

#include <SDL3/SDL.h>

namespace eng {

void SdlWindowDeleter::operator()(SDL_Window* window) const noexcept {
    SDL_DestroyWindow(window);
}

void SdlRendererDeleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

void SdlSurfaceDeleter::operator()(SDL_Surface* surface) const noexcept {
    SDL_DestroySurface(surface);
}

void SdlTextureDeleter::operator()(SDL_Texture* texture) const noexcept {
    SDL_DestroyTexture(texture);
}

} // namespace eng
