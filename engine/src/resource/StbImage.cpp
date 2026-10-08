// ============================================================================
//  StbImage.cpp - the one file that compiles stb_image.
//
//  WHY THIS FILE EXISTS AND CONTAINS NO CODE OF ITS OWN
//  stb_image is a "single-header library": the whole decoder lives inside
//  stb_image.h, and including that header normally gives you only the
//  declarations. Defining STB_IMAGE_IMPLEMENTATION before the include is what
//  asks for the actual function bodies - and that must happen in EXACTLY ONE
//  .cpp in the whole program, or the linker finds every function defined twice.
//
//  This is that one .cpp. ResourceManager.cpp includes the header the ordinary
//  way and gets the declarations.
//
//  WHY THE #pragma warning LINES
//  The engine is compiled with /W4, and the `strict` build preset turns every
//  warning into an error. stb_image is somebody else's code, written to be
//  portable across decades of compilers, and it does not compile warning-free
//  at that level. `#pragma warning(push, 0)` turns warnings off for the
//  include and `pop` puts the engine's own settings back, so one third-party
//  file cannot break a build the engine's own code passes.
// ============================================================================

// Only the formats a sprite is plausibly saved as. Each one left out is decoder
// code that is never compiled, so this is a smaller library and a smaller DLL.
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG

// No file-reading functions. The engine hands stb a block of bytes it has
// already read through FileSystem, so stb never needs to touch the disk - which
// is what keeps every path in the engine going through FileSystem::Resolve.
#define STBI_NO_STDIO

#define STB_IMAGE_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
