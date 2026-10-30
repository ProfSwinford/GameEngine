#pragma once

// ============================================================================
//  ScriptLibrary.h - loading a project's compiled scripts while the program is
//  running.
//
//  ==========================================================================
//  TWO SHAPES, AND THE EDITOR AND A SHIPPED GAME WANT DIFFERENT ONES
//
//    WHILE DEVELOPING: one library PER SCRIPT, in .build/modules/. Editing one
//      script recompiles one file and relinks one small library instead of
//      rebuilding everything, and - the part that matters more - A SCRIPT THAT
//      WILL NOT COMPILE NO LONGER STOPS THE OTHERS. With a single library a
//      typo in one file meant nothing linked, so nothing ran.
//
//    IN A SHIPPED GAME: one userContent.dll holding all of them. A game has no
//      reason to carry eleven libraries, their import tables and eleven times
//      the load-time relocation work, and nothing is ever going to recompile
//      one of them.
//
//  Both are loaded by the same code below; a bundle is just the case where the
//  list has one entry in it.
//
//  ==========================================================================
//  WHY THIS EXISTS
//
//  A project's scripts are not part of the editor. They are compiled - by the
//  editor itself, see the editor's ScriptBuild - into libraries which are then
//  loaded here.
//
//  That is what makes the editor a finished program rather than something that
//  has to be rebuilt every time somebody writes a script. Adding a script
//  touches the project. It never touches the editor.
//
//  ==========================================================================
//  HOW A SCRIPT INSIDE THE LIBRARY FINDS THE ENGINE
//
//  It links against the same engine.dll the editor is already running. There
//  is exactly one engine in the process, so when a script calls
//  InputMap::IsDown it reads the same input the editor is feeding. That is the
//  whole reason the engine is a shared library - see engine/CMakeLists.txt.
//
//  Registration needs no extra work either. ENGINE_REGISTER_SCRIPT creates an
//  object at file scope, and the operating system runs those constructors as
//  part of loading the library - so the scripts announce themselves to
//  ScriptRegistry the moment Load() returns.
//
//  ==========================================================================
//  THE ORDER A RELOAD HAS TO HAPPEN IN, because getting it wrong is a crash
//  rather than a mistake you can see.
//
//  Everything the library created lives in the library's memory: the script
//  objects themselves, and the functions the registry stores for making more
//  of them. The instant the library is unloaded, all of that is gone. So:
//
//    1. every live script object is destroyed (its name is remembered)
//    2. the registry entries for that library are removed, because every one
//       of them is a pointer to a function inside it
//    3. only now is the library unloaded
//    4. the new one is compiled and loaded, and re-registers itself
//    5. every script is rebound BY NAME to the new code
//
//  Unload() does 1 to 3. Load/LoadModules do 4 and 5. Doing them in any other
//  order means calling a function that no longer exists.
//
//  WHICH NAMES BELONG TO WHICH LIBRARY is worked out by watching the registry
//  across the load: whatever appeared is that library's. Nothing has to be
//  exported from the library itself to find out, and it stays correct for a
//  script registered from a header that two libraries both include - the
//  second registration is refused, so the name belongs to whichever got there
//  first and only that one takes it away again.
// ============================================================================

#include <engine/core/Subsystem.h>

#include <string>
#include <string_view>

namespace eng {

class ScriptLibrary : public Subsystem {
public:
    // Loads the library from DefaultVirtualPath. A project with no scripts yet
    // is not a failure; see Load below.
    bool Init(const BootConfig& config) override;
    void Shutdown() override;

    // ---- where the compiled scripts live ---------------------------------
    //
    // THERE ARE TWO PLACES, because a project being developed and a game that
    // has shipped want different things from the same file.
    //
    //   WHILE DEVELOPING: .build/userContent.dll, beside the object files, the
    //     build script and the source list the editor writes next to it. It is
    //     build output, it is in .gitignore with the rest of the build output,
    //     and it is regenerated whenever a script changes.
    //
    //   IN A BUILT GAME: assets/userContent.dll, beside the packed scenes and
    //     textures. A shipped game has no build folder and should not look as
    //     though it does - a hidden directory called .build sitting next to
    //     the executable reads as leftover rubbish, and on Linux and macOS it
    //     is literally hidden from the person who just unzipped the game.
    //
    //     The compiled game code living in the data folder is also what Unity
    //     does, for what it is worth: <Game>_Data/Managed/.
    //
    // Both are VIRTUAL paths, so each resolves the same way on every platform
    // and from any working directory. The extension differs per platform -
    // .dll, .so, .dylib - which is why these are functions and not constants.
    static std::string DefaultVirtualPath();
    static std::string ShippedVirtualPath();

    // The folder the editor writes one library per script into, while a
    // project is being developed. Never present in a shipped game.
    static std::string ModuleDirVirtualPath();

    // Are there any per-script libraries to load? Asked before the bundle,
    // because in a project being developed the modules are what the editor
    // just built and a .build/userContent.dll left behind by an export must
    // not shadow them.
    static bool HasModules();

    // The one of the two above that actually exists, or an empty string when
    // neither does. This is what Init loads, and what the editor shows.
    static std::string FindLibraryPath();

    // Loads the compiled scripts, unloading anything already loaded first, and
    // rebinds every ScriptComponent in the current scene by name.
    //
    // A missing file is NOT an error: a project with no scripts yet is a
    // perfectly ordinary state. It returns false with an explanation only when
    // the file exists and could not be loaded.
    static bool Load(std::string_view virtualPath, std::string& outError);

    // The same, for a FOLDER of one-library-per-script. Every library in it is
    // loaded, and the scene is rebound once at the end rather than after each
    // - rebinding eleven times would have ten of them find scripts that have
    // not been loaded yet.
    //
    // ONE THAT WILL NOT LOAD DOES NOT STOP THE OTHERS. It is reported and the
    // rest carry on, which is the whole point of building them separately: a
    // broken script costs you that script, not the project. outError is set
    // from the first failure and false returned, so a caller can still say
    // something went wrong.
    static bool LoadModules(std::string_view virtualDirectory, std::string& outError);

    // Destroys every live script object, removes their registry entries and
    // unloads every library - in that order. Safe to call when none is loaded.
    static void Unload();

    static bool IsLoaded();

    // How many libraries are loaded: one for a bundle, one per script while
    // developing.
    static std::size_t ModuleCount();

    // Where the currently loaded library came from. Empty when none is loaded.
    static const std::string& LoadedPath();

    // How many scripts the loaded library registered. Shown in the editor,
    // because "nothing happens when I press Play" and "no scripts loaded" are
    // the same fact and only one of them tells you what to do.
    static std::size_t ScriptCount();
};

} // namespace eng
