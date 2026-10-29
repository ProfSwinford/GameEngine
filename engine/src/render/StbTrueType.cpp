// ============================================================================
//  StbTrueType.cpp - the one place stb_truetype's CODE is compiled.
//
//  Exactly the same arrangement as StbImage.cpp, and for the same reason:
//  stb headers hold both the declarations and the implementation, and the
//  implementation only appears when STB_TRUETYPE_IMPLEMENTATION is defined
//  before the include. Doing that in one .cpp and nowhere else is what stops
//  the linker finding the same functions defined in every file that uses it.
//
//  Nothing else belongs in here. See Font.cpp for the engine's side.
// ============================================================================

#define STB_TRUETYPE_IMPLEMENTATION

// Deliberately NOT STBTT_STATIC. That would make every function local to this
// file, and Font.cpp - which includes the header for its declarations only -
// would then fail to link against them.
//
// stb is handed a block of bytes the engine has already loaded through
// FileSystem, so it never opens a file itself. That is the same rule
// StbImage.cpp follows, and what keeps every path in the engine going through
// FileSystem::Resolve.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <stb_truetype.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
