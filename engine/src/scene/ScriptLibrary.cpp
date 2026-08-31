// ============================================================================
//  ScriptLibrary.cpp - loading and unloading a project's compiled scripts.
//  See ScriptLibrary.h for the order a reload has to happen in.
//
//  SDL is used to do the loading. SDL_LoadObject and SDL_UnloadObject are a
//  thin cross-platform wrapper over LoadLibrary on Windows and dlopen
//  everywhere else, so this file needs no #ifdef per operating system. SDL is
//  already a dependency, which makes it a much better choice here than writing
//  the platform code by hand.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptLibrary.h>

#include <SDL3/SDL.h>

namespace eng {
namespace {

// The handle SDL gives back for a loaded library. Null when nothing is loaded.
SDL_SharedObject* g_handle = nullptr;

std::string g_loadedPath;

} // namespace

std::string ScriptLibrary::DefaultVirtualPath() {
    // The compiled scripts go in a hidden folder inside the project's scripts
    // folder. Hidden - the leading dot - so the Assets panel does not show it:
    // it is build output, not something anybody edits.
    //
    // This is the ONE definition of the name. Both things that produce the
    // file - the editor's own compiler call, and scripts/CMakeLists.txt - are
    // written to match it, so there is never a question of which file is the
    // current one.
#if defined(_WIN32)
    return "scripts/.build/userContent.dll";
#else
    return "scripts/.build/userContent.so";
#endif
}

bool ScriptLibrary::Load(std::string_view virtualPath, std::string& outError) {
    // Step 1 to 3 of the order in the header: get rid of the old one first.
    Unload();

    if (!FileSystem::Exists(virtualPath)) {
        // Not an error. A project with no scripts written yet, or one that has
        // never been built, is a perfectly ordinary state to be in.
        outError.clear();
        ENGINE_LOG_INFO(Channels::kScene,
                        "no compiled scripts found at '{}' - write one in the Assets "
                        "panel and the editor will build it", virtualPath);
        return true;
    }

    const std::string realPath = FileSystem::Resolve(virtualPath);

    // Step 4: loading the library runs the constructors of its file-scope
    // objects, and those are what ENGINE_REGISTER_SCRIPT creates - so by the
    // time this call returns, every script inside has already added itself to
    // ScriptRegistry.
    g_handle = SDL_LoadObject(realPath.c_str());
    if (g_handle == nullptr) {
        outError = std::string("could not load '") + realPath + "': " + SDL_GetError();
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    g_loadedPath.assign(virtualPath);

    ENGINE_LOG_INFO(Channels::kScene, "loaded {} script(s) from '{}'",
                    ScriptRegistry::Count(), virtualPath);
    ScriptRegistry::ForEachScript([](const char* name) {
        ENGINE_LOG_INFO(Channels::kScene, "    script '{}'", name);
    });

    // Step 5: anything in the scene that was waiting for a script by name can
    // now find it. A ScriptComponent that showed as NOT FOUND a moment ago
    // starts working here, with nothing reattached by hand.
    ScriptSystem::RebindAll();

    outError.clear();
    return true;
}

void ScriptLibrary::Unload() {
    if (g_handle == nullptr) {
        // Still worth clearing the registry: the scripts may have been
        // registered by a build that was linked in rather than loaded.
        ScriptSystem::UnbindAll();
        ScriptRegistry::Clear();
        return;
    }

    // Step 1: destroy every live script object. They were created by code
    // inside the library, so they must not outlive it.
    ScriptSystem::UnbindAll();

    // Step 2: empty the registry. Every entry in it is a pointer to a function
    // inside the library that is about to disappear.
    ScriptRegistry::Clear();

    // Step 3: and only now let go of the library itself.
    SDL_UnloadObject(g_handle);
    g_handle = nullptr;

    ENGINE_LOG_INFO(Channels::kScene, "unloaded the scripts from '{}'", g_loadedPath);
    g_loadedPath.clear();
}

bool ScriptLibrary::IsLoaded() { return g_handle != nullptr; }

const std::string& ScriptLibrary::LoadedPath() { return g_loadedPath; }

std::size_t ScriptLibrary::ScriptCount() { return ScriptRegistry::Count(); }

} // namespace eng
