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
//  writes a small build script, runs it, and loads the result.
//
//  ==========================================================================
//  ONE LIBRARY PER SCRIPT WHILE DEVELOPING, ONE FOR A SHIPPED GAME
//
//  Each .cpp under assets/ becomes its own library in .build/modules/, and the
//  engine loads the whole folder - see ScriptLibrary.h. An export links the
//  same objects into a single .build/userContent.dll instead, which is what
//  the game ships with; see BuildBundle.
//
//  TWO THINGS THAT BUYS, and the second matters more:
//
//    Editing one script compiles one file. Measured on the machine this was
//    written on: a one-script change went from about forty seconds to under
//    six, of which three are the editor starting up.
//
//    A BROKEN SCRIPT STOPS BEING EVERYONE'S PROBLEM. One library meant one
//    link, one link needs every object, and so a typo in one file produced no
//    library at all - every script in the project stopped running over it.
//
//  Three things got it from forty seconds to six, and they are worth knowing
//  separately because only the first was asked for:
//
//    PER-SCRIPT LIBRARIES, so only what changed is compiled.
//    A PRECOMPILED HEADER of <engine/Engine.h>, which was nine of those forty
//      seconds per file - every script parses the whole engine otherwise.
//    ADOPTING vcvars64.bat's ENVIRONMENT ONCE rather than running it per
//      build, which was another six seconds of every single build.
//
//  ==========================================================================
//  WHEN IT HAPPENS
//
//  When the editor window regains focus. That is the natural moment: you alt-
//  tab away to your text editor, write a script, save it, and come back - and
//  the thing you came back to has already noticed and rebuilt.
//
//  Nothing happens if nothing changed. NeedsRebuild and the build itself ask
//  the same question through the same function - see PlanBuild's `dryRun` -
//  because two copies of "is this out of date" drift apart, and the symptom is
//  a script that is never rebuilt.
//
//  AND NOTHING HAPPENS AFTER A FAILURE UNTIL A SCRIPT CHANGES. A build that
//  failed leaves its inputs untouched, so "is this out of date?" goes on
//  answering yes with no new answer possible - and the editor used to try
//  again on every alt-tab, each attempt scrolling the compiler's explanation
//  of the first one further out of reach. The inputs are fingerprinted on
//  failure and the automatic trigger stays quiet until one of them moves.
//
//  This is NeedsRebuild's behaviour, not the build's: BuildAndReload and
//  BeginAsync still build whenever they are asked. That is deliberate, so
//  Rebuild in the Assets panel always works and is the way out if this is
//  ever wrong about something.
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

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

class ScriptBuild {
public:
    // What a build attempt produced.
    struct Result {
        bool ok = true;          // false only when a build was attempted and failed
        bool rebuilt = false;    // false when nothing needed doing
        std::size_t compiled = 0;// how many files were actually compiled, for the
                                 // summary - NOT how many scripts exist. Reporting
                                 // the total was the whole bug: "built 11 file(s)"
                                 // after recompiling one is how somebody concludes
                                 // the incremental build does not work.
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
    //
    // FALSE AFTER A FAILED BUILD until one of the files that build read
    // changes, so a broken script is reported once instead of on every
    // alt-tab. It is the automatic trigger that this governs - BeginAsync and
    // BuildAndReload do not consult it, so an explicit Rebuild still runs.
    static bool NeedsRebuild();

    // Compiles the scripts and loads the result.
    //
    // The library is UNLOADED first, because Windows will not let a file be
    // overwritten while it is loaded - and because every script object has to
    // be destroyed before the code that defines it disappears. Reloading
    // afterwards rebinds every ScriptComponent by name, so a scene being
    // edited carries on working with nothing reattached.
    static Result BuildAndReload();

    // ---- one library, for shipping ---------------------------------------
    //
    // Links every script into a single userContent.dll, which is what an
    // exported game gets. A shipped game has no reason to carry one library
    // per script - it will never recompile one of them - and loading eleven
    // libraries means eleven import tables and eleven lots of relocation for
    // no benefit at all.
    //
    // IT IS THE SAME OBJECTS, LINKED DIFFERENTLY. The editor's per-script
    // build and this one compile into the same .build/obj, so exporting right
    // after a build is a link and not a rebuild.
    //
    // It does NOT unload or reload anything: the editor carries on running the
    // per-script libraries it already has. Nothing in the editor ever loads
    // this file - see ScriptLibrary::Init for how the two are told apart.
    static Result BuildBundle();

    // ========================================================================
    //  BUILDING WITHOUT LOCKING THE EDITOR UP.
    //
    //  BuildAndReload blocks until the compiler finishes, which is seconds of
    //  frozen window. These three run it on a worker instead:
    //
    //      if (ScriptBuild::BeginAsync()) { ... }        // starts one
    //      if (ScriptBuild::IsBuilding())  { ... }       // still going
    //      Result r;
    //      if (ScriptBuild::TakeResult(r)) { ... }       // finished; collect
    //
    //  ------------------------------------------------------------------
    //  ONLY ONE BUILD RUNS AT A TIME, and that guard is the whole point.
    //
    //  NeedsRebuild() answers from the FILES, and the files do not change
    //  until a build finishes. So anything that asks "does this need
    //  rebuilding?" on a timer gets "yes" every single time while a build is
    //  already running - and starting a thread for each of those answers is
    //  how one edit turns into dozens of compiler processes fighting over the
    //  same output file.
    //
    //  Blocking while a build runs also used to hide this: no frames were
    //  drawn during a build, so nothing could ask again. Making it
    //  asynchronous removed that accidental protection, which is why the
    //  guard has to be explicit now.
    //
    //  BeginAsync returns false when one is already in flight, or when there
    //  was nothing to do - in which case the result is ready immediately.
    //  ------------------------------------------------------------------
    //
    //  TakeResult MUST be called from the main thread. It performs the load
    //  and the rebinding, which touch the live scene.
    static bool BeginAsync();
    static bool IsBuilding();
    static bool TakeResult(Result& out);

    // Waits for any in-flight build and collects it. Called on the way down,
    // so the editor never exits with a compiler still writing into .build/.
    static void WaitForBuild();

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

    // The three phases of a build. See the long note above BeginBuild in the
    // .cpp for which of them may run off the main thread - only Compile may.
    //
    // Public because the asynchronous path drives them from two threads, and
    // because spelling the phases out is the clearest statement of which part
    // of a build is allowed to touch the live scene.
    static bool   BeginBuild(Result& out);
    static Result Compile();
    static Result FinishBuild(Result compiled);

private:
    // After a successful build: compares the classes each file registers
    // against the classes it registered last time, and reports - or repairs -
    // anything that moved. See the note on ClassManifestFile in the .cpp.
    static void VerifyRegistrations(const Sources& sources);
};

} // namespace editor
