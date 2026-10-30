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
#include <engine/project/ScriptAbi.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptLibrary.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace eng {
namespace {

// The handle SDL gives back for a loaded library. Null when nothing is loaded.
// One loaded library, and which script names came out of it.
//
// `owned` is filled by watching the registry across the load. It is what lets
// one library be taken away without disturbing the others - see the header.
struct Module {
    SDL_SharedObject*        handle = nullptr;
    std::string              path;
    std::vector<std::string> owned;
};

std::vector<Module> g_modules;

// What to show: the bundle's path, or the folder the modules came from.
std::string g_loadedPath;

} // namespace

namespace {

// The platform's extension for a dynamically loaded library, and the bundle's
// whole name. Separate because the module folder needs the extension on its
// own to list what is in it.
const char* LibraryExtension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

const char* LibraryFileName() {
#if defined(_WIN32)
    return "userContent.dll";
#elif defined(__APPLE__)
    return "userContent.dylib";
#else
    return "userContent.so";
#endif
}

// Every script name currently in the registry, so that loading a library can
// be bracketed and the difference taken.
std::vector<std::string> RegisteredNames() {
    std::vector<std::string> names;
    ScriptRegistry::ForEachScript([&names](const char* name) { names.emplace_back(name); });
    return names;
}

// Loads one library and records which names it brought with it. Returns false
// with outError set; the caller decides whether that is fatal.
bool LoadOne(std::string_view virtualPath, std::string& outError) {
    const std::string realPath = FileSystem::Resolve(virtualPath);

    // Taken BEFORE the load, because loading is what runs the file-scope
    // constructors ENGINE_REGISTER_SCRIPT creates - by the time
    // SDL_LoadObject returns, the new names are already in.
    const std::vector<std::string> before = RegisteredNames();

    SDL_SharedObject* handle = SDL_LoadObject(realPath.c_str());
    if (handle == nullptr) {
        outError = std::string("could not load '") + realPath + "': " + SDL_GetError();
        return false;
    }

    Module module;
    module.handle = handle;
    module.path.assign(virtualPath);

    for (const std::string& name : RegisteredNames()) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            module.owned.push_back(name);
        }
    }

    // A library that registered NOTHING is worth saying so about. It is either
    // a helper that should not have been built as a script, or a file whose
    // ENGINE_REGISTER_SCRIPT went missing - and either way nothing in it will
    // ever run.
    if (module.owned.empty()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "'{}' loaded but registered no scripts, so nothing in it can "
                        "run - check it has an ENGINE_REGISTER_SCRIPT line",
                        virtualPath);
    }

    g_modules.push_back(std::move(module));
    outError.clear();
    return true;
}

// Lists every script with THE HOOKS IT TURNED OUT TO HAVE.
//
// Because hooks are found by name rather than declared, a misspelled hook name
// is not a compile error - it is a function nobody calls. This is what makes
// that visible: if your script is listed with hooks you did not expect, the
// spelling is where to look. A script with none at all is a warning, because
// it is almost certainly a mistake.
//
// Done ONCE after everything is loaded rather than per library, so the list
// reads as one list.
void LogLoadedScripts() {
    ScriptRegistry::ForEachEntry([](const char* name, const ScriptRegistry::Entry& e) {
        const std::string hooks = DescribeHooks(e.hooks);
        if (e.hooks.start == nullptr && e.hooks.update == nullptr &&
            e.hooks.fixedUpdate == nullptr &&
            e.hooks.destroy == nullptr && !e.hooks.AnyContact()) {
            ENGINE_LOG_WARN(Channels::kScene, "    script '{}' has {}", name, hooks);
        } else {
            ENGINE_LOG_INFO(Channels::kScene, "    script '{}' - {}", name, hooks);
        }
    });
}

} // namespace

std::string ScriptLibrary::DefaultVirtualPath() {
    // Where the EDITOR writes it: beside the object files and the build script
    // it generates, which is what .build/ is for. See the header for why a
    // shipped game uses a different place.
    return std::string(".build/") + LibraryFileName();
}

std::string ScriptLibrary::ShippedVirtualPath() {
    // Where a BUILT GAME keeps it. No folder prefix, so FileSystem::Resolve
    // puts it under assets/ with the rest of the game's content - see the
    // header.
    return LibraryFileName();
}

std::string ScriptLibrary::ModuleDirVirtualPath() { return ".build/modules"; }

bool ScriptLibrary::HasModules() {
    std::vector<std::string> found;
    if (!FileSystem::ListFiles(ModuleDirVirtualPath(), LibraryExtension(), found)) {
        return false;   // no such folder, which is the shipped-game case
    }
    return !found.empty();
}

std::string ScriptLibrary::FindLibraryPath() {
    // THE BUILD FOLDER IS CHECKED FIRST, and the order is deliberate.
    //
    // In a shipped game .build/ does not exist, so this falls straight through
    // to the packed one and costs nothing. In a project being developed the
    // editor has just rebuilt .build/, and checking it first means a stale
    // assets/userContent.dll - committed by accident, or left behind by
    // someone who exported into their own project once - can never shadow the
    // library the editor just compiled. Loading yesterday's scripts while the
    // Console reports a successful build is a bad afternoon.
    if (FileSystem::Exists(DefaultVirtualPath())) {
        return DefaultVirtualPath();
    }
    if (FileSystem::Exists(ShippedVirtualPath())) {
        return ShippedVirtualPath();
    }
    return {};
}

bool ScriptLibrary::Init(const BootConfig&) {
    // ======================================================================
    //  WERE THESE SCRIPTS BUILT FOR THIS ENGINE?
    //
    //  Asked before anything is loaded, because the answer decides whether
    //  loading is safe at all. A library built against a different C runtime
    //  loads perfectly and then corrupts the heap - see ScriptAbi.h for what
    //  that looks like from the outside, which is almost nothing.
    //
    //  ----------------------------------------------------------------------
    //  IT RETURNS TRUE ON A MISMATCH, WHICH LOOKS WRONG AND IS LOAD-BEARING.
    //
    //  Returning false fails this subsystem, which fails Engine::Init, which
    //  stops the program. For the player that would be defensible. For THE
    //  EDITOR IT WOULD BE A TRAP: the editor is the only thing that can
    //  rebuild the scripts, and it rebuilds them shortly after start-up - so
    //  an editor that refused to start because the scripts are stale could
    //  never start in order to fix them. The only way out would be deleting
    //  .build/ by hand.
    //
    //  So the scripts are simply not loaded. Every ScriptComponent shows as
    //  NOT FOUND, which is a state both programs already handle and which the
    //  Inspector already explains, and the error above says why in one line.
    //  The editor then rebuilds - ScriptBuild compares the same stamp and has
    //  always treated a change to it as "recompile everything".
    //  ----------------------------------------------------------------------
    //
    //  CHECKED HERE AND NOT INSIDE LoadModules, and the reason is a window
    //  that only opens when a build goes wrong.
    //
    //  The editor calls LoadModules again at the end of every build. On a
    //  SUCCESSFUL build that is harmless: ScriptBuild::Compile writes the new
    //  stamp before FinishBuild loads anything, so a check there would see
    //  the new stamp and agree.
    //
    //  THE STAMP IS WRITTEN ONLY ON SUCCESS, THOUGH - deliberately, so that a
    //  failed build cannot leave behind a stamp claiming the library matches
    //  this configuration. So after a PARTIAL failure - the configuration
    //  changed, every script was recompiled, and one of them has a syntax
    //  error - the modules on disk are new and correct while the stamp beside
    //  them is still the old one. The editor loads them anyway, which is the
    //  entire point of one library per script. A check inside LoadModules
    //  would refuse those correctly-built modules and blame the engine.
    //
    //  Start-up is the only moment when a library on disk might have been
    //  built by something other than this program, which is exactly when the
    //  question is worth asking.
    // ======================================================================
    if (const ScriptAbiCheck abi = ScriptAbi::CheckRecorded();
        abi.verdict == ScriptAbiCheck::Verdict::Mismatch) {
        ENGINE_LOG_ERROR(Channels::kScene, "{}", abi.explanation);
        return true;
    } else if (abi.verdict == ScriptAbiCheck::Verdict::Unverifiable &&
               !abi.explanation.empty()) {
        // Only when something WAS recorded and could not be understood. A
        // shipped game records nothing and says nothing; see ScriptAbi.h.
        ENGINE_LOG_WARN(Channels::kScene, "{}", abi.explanation);
    }

    // ======================================================================
    //  THE MODULE FOLDER WINS WHEN IT HAS ANYTHING IN IT.
    //
    //  A shipped game has no .build/, so this costs one failed directory
    //  listing there and falls straight through to the bundle.
    //
    //  In a project being developed it is the other way round: the modules are
    //  what the editor last built, and a .build/userContent.dll is a leftover
    //  from the last EXPORT - which is a bundle of whatever the scripts were
    //  at that moment. Letting that shadow the modules would run yesterday's
    //  code while the Console reported a successful build, which is the same
    //  bad afternoon FindLibraryPath already guards against from the other
    //  direction.
    // ======================================================================
    if (HasModules()) {
        std::string error;
        return LoadModules(ModuleDirVirtualPath(), error);
    }

    // A project with no scripts is fine and returns true. Only a library that
    // exists and will not load is a failure.
    const std::string path = FindLibraryPath();
    if (path.empty()) {
        ENGINE_LOG_INFO(Channels::kScene,
                        "no compiled scripts found (looked for '{}' and '{}') - write "
                        "one in the Assets panel and the editor will build it",
                        DefaultVirtualPath(), ShippedVirtualPath());
        return true;
    }

    std::string error;
    return Load(path, error);
}

void ScriptLibrary::Shutdown() {
    Unload();
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

    // Step 4: loading the library runs the constructors of its file-scope
    // objects, and those are what ENGINE_REGISTER_SCRIPT creates - so by the
    // time this call returns, every script inside has already added itself to
    // ScriptRegistry.
    if (!LoadOne(virtualPath, outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    g_loadedPath.assign(virtualPath);

    ENGINE_LOG_INFO(Channels::kScene, "loaded {} script(s) from '{}'",
                    ScriptRegistry::Count(), virtualPath);
    LogLoadedScripts();

    // Step 5: anything in the scene that was waiting for a script by name can
    // now find it. A ScriptComponent that showed as NOT FOUND a moment ago
    // starts working here, with nothing reattached by hand.
    ScriptSystem::RebindAll();

    outError.clear();
    return true;
}

bool ScriptLibrary::LoadModules(std::string_view virtualDirectory,
                                std::string& outError) {
    Unload();
    outError.clear();

    std::vector<std::string> libraries;
    if (!FileSystem::ListFiles(virtualDirectory, LibraryExtension(), libraries) ||
        libraries.empty()) {
        // Not an error, exactly as a missing bundle is not: a project with no
        // scripts built yet is an ordinary state.
        ENGINE_LOG_INFO(Channels::kScene,
                        "no compiled scripts found in '{}' - write one in the Assets "
                        "panel and the editor will build it", virtualDirectory);
        return true;
    }

    // ======================================================================
    //  ONE THAT WILL NOT LOAD DOES NOT STOP THE OTHERS.
    //
    //  This is the whole reason the editor builds them separately. With a
    //  single library, one script that would not link meant NOTHING ran - the
    //  file was never produced, so the project went dead over a typo. Here the
    //  failure costs exactly the script it belongs to.
    // ======================================================================
    std::size_t failed = 0;
    for (const std::string& library : libraries) {
        std::string error;
        if (LoadOne(library, error)) {
            continue;
        }
        ++failed;
        ENGINE_LOG_ERROR(Channels::kScene, "{}", error);
        if (outError.empty()) {
            outError = error;
        }
    }

    g_loadedPath.assign(virtualDirectory);

    ENGINE_LOG_INFO(Channels::kScene, "loaded {} script(s) from {} librar{} in '{}'",
                    ScriptRegistry::Count(), g_modules.size(),
                    g_modules.size() == 1 ? "y" : "ies", virtualDirectory);
    LogLoadedScripts();

    // ONCE, after everything is in. Rebinding after each library would have
    // the first ten rebinds looking for scripts that are not loaded yet, and
    // every one of those would report a script as NOT FOUND that is about to
    // be perfectly fine.
    ScriptSystem::RebindAll();

    return failed == 0;
}

void ScriptLibrary::Unload() {
    // Step 1: destroy every live script object. They were created by code
    // inside these libraries, so they must not outlive them.
    //
    // Done ONCE for all of them rather than per library. A script object
    // belongs to whichever library made it, and nothing here knows which that
    // was - and it does not need to, because every library is going.
    ScriptSystem::UnbindAll();

    if (g_modules.empty()) {
        // Still worth clearing the registry: the scripts may have been
        // registered by a build that was linked in rather than loaded.
        ScriptRegistry::Clear();
        return;
    }

    // IN REVERSE, so that if one library's static destructors reach for
    // something another one set up, the later arrival goes first. Nothing in
    // this project does that; it costs one word to be right anyway.
    for (std::size_t i = g_modules.size(); i-- > 0;) {
        Module& module = g_modules[i];

        // Step 2: take out exactly this library's entries. Every one of them
        // is a pointer to a function inside it.
        for (const std::string& name : module.owned) {
            ScriptRegistry::Remove(name);
        }

        // Step 3: and only now let go of the library itself.
        SDL_UnloadObject(module.handle);
    }

    const std::size_t count = g_modules.size();
    g_modules.clear();

    // A SAFETY NET, and it should find nothing. Anything left is a name that
    // was registered without a library being loaded - a script linked
    // straight into the program, which the tests do. Clearing it keeps the
    // promise that nothing survives an unload.
    ScriptRegistry::Clear();

    ENGINE_LOG_INFO(Channels::kScene, "unloaded {} script librar{} from '{}'", count,
                    count == 1 ? "y" : "ies", g_loadedPath);
    g_loadedPath.clear();
}

bool ScriptLibrary::IsLoaded() { return !g_modules.empty(); }

std::size_t ScriptLibrary::ModuleCount() { return g_modules.size(); }

const std::string& ScriptLibrary::LoadedPath() { return g_loadedPath; }

std::size_t ScriptLibrary::ScriptCount() { return ScriptRegistry::Count(); }

} // namespace eng
