// ============================================================================
//  SdlHandles.cpp - the four clean-up functions declared in SdlHandles.h.
//
//  This is the ONLY file in the engine's platform layer that includes the full
//  SDL header, which is what keeps SDL out of every other file. Each function
//  below simply calls the matching SDL_Destroy* function.
//
//  None of them checks for null first. That is not an oversight: SDL's destroy
//  functions are documented to accept a null pointer and do nothing, so a
//  window that failed to open still cleans up correctly.
// ============================================================================

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
