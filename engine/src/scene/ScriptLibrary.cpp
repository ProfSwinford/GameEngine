// =============================================================================
//  ScriptLibrary.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. ScriptLibrary.h is the specification; read it
//  first.
//
//  A reload has an order that is not optional: destroy every live script
//  object, empty the registry, unload the library, load the new one, rebind by
//  name. Doing the first two after the third calls destructors that no longer
//  exist in the process.
// =============================================================================

#include <engine/scene/ScriptLibrary.h>

namespace eng {

// Where the compiled scripts live. The one definition of the name, used by the
// engine that loads the file and the editor that writes it.
std::string ScriptLibrary::DefaultVirtualPath() {
    return {};
}

// Loads the compiled script library. Loading it runs the file-scope objects
// inside, and those are what ENGINE_REGISTER_SCRIPT creates - so by the time
// this returns, every script inside has already added itself to the registry.
bool ScriptLibrary::Load(std::string_view /*virtualPath*/, std::string& /*outError*/) {
    return false;
}

// Unloads the library, after destroying everything that came out of it.
void ScriptLibrary::Unload() {
}

// Is a script library currently loaded?
bool ScriptLibrary::IsLoaded() {
    return false;
}

// Which file is loaded, for the log and the editor's status line.
const std::string& ScriptLibrary::LoadedPath() {
    static const std::string none;
    return none;
}

// How many scripts came out of it.
std::size_t ScriptLibrary::ScriptCount() {
    return 0;
}

} // namespace eng
