// The anchor that keeps `gamescripts` linkable when the directory holds no scripts.
//
// Two jobs, both small:
//
//  1. A static library with zero sources is not a library on every toolchain,
//     and deleting your last script should not break the build of the editor
//     and the sandbox.
//
//  2. THE ONE THAT ACTUALLY BITES. Script registration happens in a file-scope
//     object's constructor (see ENGINE_REGISTER_SCRIPT). A linker pulling
//     objects out of a static library takes only the ones that resolve a
//     symbol somebody referenced - and nothing references a script, which is
//     the entire point of registration by name. The registrars would be
//     dropped and every script would silently fail to register.
//
//     The fix is in the CMake of the things that LINK this library:
//     WHOLE_ARCHIVE. This file is where that requirement is written down, so
//     the next person to see "my script does not appear in the list" finds the
//     answer next to the cause rather than in a build file.

#include <engine/scene/ScriptComponent.h>

namespace gamescripts {

// Referenced by nothing on purpose. Its presence is what makes the library
// non-empty; the whole-archive link is what makes the registrars survive.
int ScriptsAnchor() { return static_cast<int>(eng::ScriptRegistry::Count()); }

} // namespace gamescripts
