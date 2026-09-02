// =============================================================================
//  ScriptLibrary.cpp - A SHELL. The declarations are real; the bodies are
//  yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. ScriptLibrary.h explains WHAT
//  each function is for and WHY it exists - read it first.
//
//  THIS ONE HAS AN ORDER THAT IS NOT OPTIONAL. Reloading has to be:
//
//    1. destroy every live script object   (their code is inside the library)
//    2. empty the registry                 (every entry points into it too)
//    3. unload the library
//    4. compile and load the new one
//    5. rebind every ScriptComponent by name
//
//  Getting 1 and 2 after 3 is a crash, not untidiness - the destructors being
//  called would no longer exist in the process.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptLibrary.h>

namespace eng {
namespace {

std::string g_loadedPath;

} // namespace

// Given, because it is the ONE definition of where the compiled scripts live
// and the editor is written to match it. Changing it here changes both.
std::string ScriptLibrary::DefaultVirtualPath() {
#if defined(_WIN32)
    return ".build/userContent.dll";
#else
    return ".build/userContent.so";
#endif
}

// TODO: load the compiled script library, which runs the file-scope
// constructors inside it - and those are what ENGINE_REGISTER_SCRIPT creates.
// By the time the load returns, every script inside has registered itself.
//
// SDL_LoadObject / SDL_UnloadObject are the cross-platform way to do this.
bool ScriptLibrary::Load(std::string_view virtualPath, std::string& outError) {
    outError.clear();
    g_loadedPath.assign(virtualPath);
    ENGINE_LOG_INFO(Channels::kScene,
                    "ScriptLibrary::Load is a shell - '{}' was not actually loaded, so "
                    "no scripts will run yet",
                    virtualPath);
    return true;
}

void ScriptLibrary::Unload() { g_loadedPath.clear(); }

bool ScriptLibrary::IsLoaded() { return false; }

const std::string& ScriptLibrary::LoadedPath() { return g_loadedPath; }

std::size_t ScriptLibrary::ScriptCount() { return ScriptRegistry::Count(); }

} // namespace eng
