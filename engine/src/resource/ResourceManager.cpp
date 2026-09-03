// =============================================================================
//  ResourceManager.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. ResourceManager.h is the specification; read it
//  before filling one in.
// =============================================================================

#include <engine/resource/ResourceManager.h>

namespace eng {

// Hands the picture back to the graphics card. A texture is a shared_ptr, so
// this runs when the last sprite using the image lets go of it - there is no
// "unload" call for anybody to remember.
Texture::~Texture() {
}

// Prepares the loader and builds the magenta "missing image" texture.
bool ResourceManager::Init() {
    return false;
}

// Reports anything still loaded, then empties the cache. Naming what is left is
// how a texture nobody released gets noticed.
void ResourceManager::Shutdown() {
}

// Loads a .bmp and hands back a shared texture. Asking twice for the same file
// gives back the SAME texture, so the file is read once no matter how many
// entities use it.
TextureRef ResourceManager::LoadTexture(std::string_view /*virtualPath*/) {
    return nullptr;
}

// The magenta placeholder, handed back when a file is missing so a wrong
// filename shows up on screen instead of crashing.
TextureRef ResourceManager::MissingTexture() {
    return nullptr;
}

// How many textures are loaded right now.
std::size_t ResourceManager::LoadedCount() {
    return 0;
}

// Drops cache entries whose texture has already been let go of. Called once per
// frame, because being in the cache must not by itself keep an image loaded.
void ResourceManager::PruneCache() {
}

} // namespace eng
