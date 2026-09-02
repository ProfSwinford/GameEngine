// =============================================================================
//  ResourceManager.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/resource/ResourceManager.h>

namespace eng {

// TODO: hand the texture back to the graphics card. Until then there is
// nothing to release, so this is safely empty - but the moment LoadTexture
// starts creating real textures, this has to start destroying them.
Texture::~Texture() {}

bool ResourceManager::Init()     { return true; }
void ResourceManager::Shutdown() {}

// TODO: read the .bmp, hand it to the renderer, and remember it so that asking
// twice gives back the SAME texture rather than a second copy.
TextureRef ResourceManager::LoadTexture(std::string_view /*virtualPath*/) {
    return nullptr;
}

// TODO: the magenta "this image is missing" texture, so a wrong filename shows
// up on screen instead of crashing.
TextureRef ResourceManager::MissingTexture() { return nullptr; }

std::size_t ResourceManager::LoadedCount() { return 0; }
void        ResourceManager::PruneCache()  {}

} // namespace eng
