#pragma once

// ============================================================================
//  ScriptBuild.h - the editor compiles the project's scripts itself.
//
//  ==========================================================================
//  WHY THIS EXISTS
//
//  Adding a script must not require rebuilding the editor. Somebody using the
//  released development environment has an editor, a project folder, and a
//  compiler - they do not have this source tree, and they should not need it.
//
//  So the editor does the build. It finds a C++ compiler on the machine,
//  writes a small build script, runs it, and loads the result. Every .cpp in
//  the project's assets/ folder - at any depth - is compiled together into ONE
//  library, .build/userContent.dll, which the engine loads at run time -
//  see engine/include/engine/scene/ScriptLibrary.h.
//
//  ==========================================================================
//  WHEN IT HAPPENS
//
//  When the editor window regains focus. That is the natural moment: you alt-
//  tab away to your text editor, write a script, save it, and come back - and
//  the thing you came back to has already noticed and rebuilt.
//
//  Nothing happens if nothing changed. The check is a timestamp comparison
//  against the built library plus a list of which files went into it, so
//  adding, editing OR deleting a script all count as a change.
//
//  ==========================================================================
//  WHAT IT NEEDS FROM THE MACHINE
//
//  A C++ compiler. This is compiled C++, so there is no way around that - but
//  it is the only requirement, and the editor says so plainly in the Console
//  when it cannot find one rather than failing silently.
//
//    Windows   Visual Studio or the standalone Build Tools. Found through
//              vswhere, which every Visual Studio install ships.
//    macOS     clang++, which comes with the Xcode command line tools.
//    Linux     g++ or clang++.
//
//  ==========================================================================
//  TWO THINGS THAT HAVE TO MATCH THE EDITOR'S OWN BUILD
//
//  A compiled script and the running engine pass std::string and std::vector
//  between them, so they must agree about how those are laid out and about
//  which heap they came from. That means the script build uses the same C++
//  standard and the same C runtime setting the engine was built with. Those
//  values are baked into the editor at build time rather than guessed - see
//  the ENGINE_SCRIPT_* definitions in editor/CMakeLists.txt.
// ============================================================================

#include <string>
#include <vector>

namespace editor {

class ScriptBuild {
public:
    // What a build attempt produced.
    struct Result {
        bool ok = true;          // false only when a build was attempted and failed
        bool rebuilt = false;    // false when nothing needed doing
        std::string summary;     // one line, for the status bar
        std::string output;      // what the compiler printed, when it complained
    };

    // Looks for a compiler once and remembers what it found. Called at
    // start-up so the answer is already known the first time it is needed.
    static void Init();

    // A short description of the compiler that was found, for the Console and
    // the status bar. Empty when there is none.
    static const std::string& CompilerDescription();
    static bool               HasCompiler();

    // True when a script under assets/ has been added, changed or removed since
    // the library was last built. Cheap - it only looks at timestamps.
    static bool NeedsRebuild();

    // Compiles the scripts and loads the result.
    //
    // The library is UNLOADED first, because Windows will not let a file be
    // overwritten while it is loaded - and because every script object has to
    // be destroyed before the code that defines it disappears. Reloading
    // afterwards rebinds every ScriptComponent by name, so a scene being
    // edited carries on working with nothing reattached.
    static Result BuildAndReload();

    // What the last build printed. Kept so a panel can show it after the fact.
    static const std::string& LastOutput();

    // Every script source found under assets/, split by how it gets compiled.
    struct Sources {
        // .cpp files, handed to the compiler directly.
        std::vector<std::string> compiled;

        // .h / .hpp files that contain an ENGINE_REGISTER_SCRIPT. A header is
        // not compiled on its own, so these are pulled in through one
        // generated translation unit - otherwise a script written entirely in
        // a header would be found by the Assets panel and never run.
        //
        // Headers WITHOUT a registration are left alone. They are helpers, and
        // including a helper that was written to be included in one particular
        // place can easily fail to compile on its own.
        std::vector<std::string> headers;

        // Every other .h under assets/ - helpers, included by something else.
        //
        // These are never handed to the compiler, but they ARE watched: a
        // helper header is part of what the scripts are built from, so editing
        // one has to trigger a rebuild. Leaving them out would mean changing a
        // shared header and finding that nothing happened.
        std::vector<std::string> helpers;

        bool        Empty() const { return compiled.empty() && headers.empty(); }
        std::size_t Count() const { return compiled.size() + headers.size(); }

        // Everything, in one sorted list - what gets recorded and compared to
        // decide whether a rebuild is needed. Helpers included, for the reason
        // above.
        std::vector<std::string> All() const;
    };

    // Walks assets/ and every folder under it. Public because the Assets panel
    // reports the count, and because it is the honest answer to "which files
    // does the editor think are scripts?".
    static Sources GatherSources();

private:
    // After a successful build: compares the classes each file registers
    // against the classes it registered last time, and reports - or repairs -
    // anything that moved. See the note on ClassManifestFile in the .cpp.
    static void VerifyRegistrations(const Sources& sources);
};

} // namespace editor
