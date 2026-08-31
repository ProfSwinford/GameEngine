#pragma once

// ============================================================================
//  ScriptLibrary.h - loading a project's compiled scripts while the program is
//  running.
//
//  ==========================================================================
//  WHY THIS EXISTS
//
//  A project's scripts are not part of the editor. They are compiled - by the
//  editor itself, see the editor's ScriptBuild - into a single library called
//  userContent.dll (or .so, or .dylib), which is then loaded here.
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
//    2. the registry is emptied, because its entries point into the library
//    3. only now is the library unloaded
//    4. the new one is compiled and loaded, and re-registers itself
//    5. every script is rebound BY NAME to the new code
//
//  Unload() does 1 to 3. Load() does 4 and 5. Doing them in any other order
//  means calling a function that no longer exists.
// ============================================================================

#include <string>
#include <string_view>

namespace eng {

class ScriptLibrary {
public:
    // The file name the editor builds and this loads, for the platform being
    // run on - "scripts/.build/userContent.dll" on Windows, ".so" on Linux,
    // ".dylib" on macOS. A virtual path, so it resolves the same way anywhere.
    static std::string DefaultVirtualPath();

    // Loads the compiled scripts, unloading anything already loaded first, and
    // rebinds every ScriptComponent in the current scene by name.
    //
    // A missing file is NOT an error: a project with no scripts yet is a
    // perfectly ordinary state. It returns false with an explanation only when
    // the file exists and could not be loaded.
    static bool Load(std::string_view virtualPath, std::string& outError);

    // Destroys every live script object, empties the registry and unloads the
    // library - in that order. Safe to call when nothing is loaded.
    static void Unload();

    static bool IsLoaded();

    // Where the currently loaded library came from. Empty when none is loaded.
    static const std::string& LoadedPath();

    // How many scripts the loaded library registered. Shown in the editor,
    // because "nothing happens when I press Play" and "no scripts loaded" are
    // the same fact and only one of them tells you what to do.
    static std::size_t ScriptCount();
};

} // namespace eng
