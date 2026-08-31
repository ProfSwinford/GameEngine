// ============================================================================
//  ScriptsAnchor.cpp - a tiny file that exists for two build reasons.
//
//  1. A static library with no source files at all is not a valid library on
//     every toolchain, and deleting your last script should not break the
//     build of the editor and the game.
//
//  2. THE ONE THAT ACTUALLY BITES. A script registers itself using an object
//     at file scope that nothing else refers to by name - see
//     ENGINE_REGISTER_SCRIPT. When the linker pulls pieces out of a static
//     library it normally takes only the ones needed to satisfy a reference
//     somebody made, and nothing refers to a script. Every registrar would be
//     thrown away and every script would silently fail to appear.
//
//     The fix lives in the CMake of the things that LINK this library:
//     WHOLE_ARCHIVE, which tells the linker to keep all of it. This file is
//     where that requirement is written down, so the next person who wonders
//     why their script does not show up finds the answer next to the cause
//     rather than buried in a build file.
// ============================================================================

#include <engine/scene/ScriptComponent.h>

namespace gamescripts {

// Deliberately called by nothing. Its existence is what makes the library
// non-empty; the whole-archive link is what makes the registrars survive.
int ScriptsAnchor() { return static_cast<int>(eng::ScriptRegistry::Count()); }

} // namespace gamescripts
