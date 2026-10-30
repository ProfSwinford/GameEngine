// ============================================================================
//  main.cpp - where the editor starts.
//
//  Deliberately dull: build the application object, start it, run it, stop it.
//  Everything interesting is in EditorApp or in one of the panels.
//
//  There are three programs in this project:
//    editor   - this one, the development environment
//    player   - a game running on its own, with no editor at all
//    hub      - the launcher: a list of projects and a list of engines
//
//  The editor opens a project it was NAMED and nothing else. Run with no
//  project it starts the launcher and exits rather than guessing - see the
//  long note further down, which is the one decision in this file worth
//  reading before changing anything.
// ============================================================================

#include "EditorApp.h"
#include "GameExport.h"
#include "ScriptBuild.h"
#include "ProjectMenu.h"
#include "Relaunch.h"

#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/project/EngineBuild.h>
#include <engine/project/Project.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN drops the half of windows.h nothing here uses, and
// NOMINMAX stops it defining min and max as MACROS, which would then break
// every std::min and std::max in anything included after it. Both are
// standard practice and the reason is worth knowing once.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// ---------------------------------------------------------------------------
//  The editor is built as a WINDOWS program rather than a console one, so no
//  black terminal window opens beside it - the Console panel shows the same
//  log, better. See CMakeLists.txt.
//
//  The cost is that `editor --build-scripts`, which is meant to be run FROM a
//  terminal, would print into nowhere: a /SUBSYSTEM:WINDOWS program is not
//  connected to the console that launched it, even though one is right there.
//
//  AttachConsole(ATTACH_PARENT_PROCESS) borrows it. It fails harmlessly when
//  the program was started by double-clicking, because then there is no parent
//  console to borrow - which is exactly the case where the window should stay
//  hidden. So this is safe to call unconditionally and needs no flag.
//
//  CONOUT$ is Windows' name for "the console I am attached to, for writing".
//  Reopening stdout and stderr onto it is what makes printf and the log's
//  terminal output land in the right place.
// ---------------------------------------------------------------------------
void UseParentConsoleIfThereIsOne() {
#if defined(_WIN32)
    // ALREADY GOING SOMEWHERE? Leave it alone.
    //
    // `editor --build-scripts > build.txt` hands this program a real file
    // handle for stdout, and an automated build almost certainly does the
    // same. Pointing stdout at the console below would throw that redirection
    // away and write to the screen instead, so the file the caller asked for
    // would come back empty. A GUI program that was NOT redirected has no
    // stdout handle at all, which is how the two cases are told apart.
    const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
    if (existing != nullptr && existing != INVALID_HANDLE_VALUE &&
        GetFileType(existing) != FILE_TYPE_UNKNOWN) {
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return;
    }
    FILE* reopened = nullptr;
    freopen_s(&reopened, "CONOUT$", "w", stdout);
    freopen_s(&reopened, "CONOUT$", "w", stderr);
#endif
}

// ---------------------------------------------------------------------------
//  WHAT THE EXIT CODE MEANS.
//
//  A launcher has to tell "the project you remembered has been moved" apart
//  from "the engine itself is broken", because the first is a stale entry in
//  a list and the second is worth a bug report. One non-zero code cannot say
//  which, and a launcher that guesses shows the wrong message.
//
//  0 AND 1 KEEP THEIR OLD MEANINGS exactly, so nothing that already reads
//  them changes behaviour - tools/fresh-clone-check.sh included. 2 and 3
//  describe situations that could not arise before --project existed.
// ---------------------------------------------------------------------------
constexpr int kExitOk        = 0;
constexpr int kExitUsage     = 1;   // bad arguments, or a build that failed
constexpr int kExitNoProject = 2;   // the named folder is not a project
constexpr int kExitEngine    = 3;   // the project was fine; the engine would not start

// ---------------------------------------------------------------------------
//  DOES THE ENVIRONMENT NAME A PROJECT THE EDITOR CAN OPEN?
//
//  NOT THE SAME QUESTION AS "IS THE VARIABLE SET", and the difference is a
//  hole big enough to drive the whole feature through.
//
//  FileSystem::Init is deliberately LENIENT about a bad ENGINE_ASSET_ROOT: it
//  warns and falls back to the upward search. That leniency is load-bearing
//  for the player and the tests, and kind to anybody with an old variable
//  exported in their shell.
//
//  For the editor it is the one door left open. A variable pointing at a
//  folder that is no longer a project would count as "somebody named one",
//  skip the hand-off, fall through to the search, and open whichever project
//  happens to sit above the executable - a stale reference loading itself on a
//  cold start, which is precisely what this change exists to stop. Measured
//  before it was closed:
//
//      ENGINE_ASSET_ROOT=C:/gone editor
//        Warning: ... no 'assets' folder there; searching instead
//        Info:    assets found in 'C:\dev\GameEngine'
//
//  So the editor checks the folder HERE and hands off when it does not hold a
//  project, which leaves Init's leniency untouched for everybody else.
// ---------------------------------------------------------------------------
bool TheEnvironmentNamesAProject() {
    const std::string named = eng::FileSystem::RootFromEnvironment();
    if (named.empty()) {
        return false;
    }

    // ---- ABSOLUTE FIRST, WHICH IS NOT A FORMALITY ------------------------
    //
    // These are the same two steps FileSystem::Init takes before accepting a
    // root, and they have to stay the same two: if this is stricter than Init
    // the editor opens the launcher for a value the player and
    // `--build-scripts` open happily, which looks like the editor being
    // broken.
    //
    // fs::absolute is what does the reconciling. A value may be relative -
    // ENGINE_ASSET_ROOT=. is reasonable and works - and on Windows it may
    // have trailing spaces that the file system itself ignores. Measured:
    // 'C:/dev/GameEngine ' with a trailing space IS accepted by Init, and was
    // refused here until this call was added.
    std::error_code             ec;
    const std::filesystem::path candidate =
        std::filesystem::absolute(std::filesystem::path(named), ec);
    if (!ec && eng::Project::Looks(candidate.generic_string())) {
        return true;
    }

    // SAID, not silently ignored. Somebody who exported that variable
    // believes it is doing something, and a launcher appearing instead of
    // their project is otherwise inexplicable.
    std::fprintf(stderr,
                 "%s names '%s', which is not a project - opening the launcher rather "
                 "than searching for one.\n",
                 eng::FileSystem::kRootVariable, named.c_str());
    return false;
}

// ---------------------------------------------------------------------------
//  HANDING OVER TO THE LAUNCHER.
//
//  Reached when nobody named a project. See the note at the call site for why
//  that is a hand-off rather than a search.
//
//  EXIT ZERO ON SUCCESS, because nothing went wrong: the editor was asked to
//  open a project, was given none, and did the useful thing instead. A
//  non-zero code here would make every `editor` with no arguments look like a
//  failure to whatever started it.
// ---------------------------------------------------------------------------
int OpenTheLauncherInstead(int frameLimit) {
    if (!editor::HubIsInstalled()) {
        // NOTHING TO HAND OFF TO. Falling back to the upward search here
        // would quietly reinstate the behaviour this exists to remove, in the
        // one case where nobody would notice - so it says what would fix it
        // instead.
        //
        // IT NAMES THE PATH IT LOOKED AT rather than asserting that no
        // launcher exists, because the check can be wrong: see the note on
        // HubIsInstalled, which cannot see its own folder when that folder's
        // name is not ASCII. Printing the path turns "this is impossible, the
        // file is right there" into something somebody can act on, and the
        // path prints correctly even in the case where the check did not.
        std::fprintf(stderr,
                     "no project was named, and no launcher was found to pick one in.\n"
                     "  looked for:           %s\n"
                     "  name a project with:  editor --project <folder>\n",
                     editor::LauncherPath().c_str());
        return kExitNoProject;
    }

    // WAITED FOR WHEN --frames WAS GIVEN, and let go of otherwise.
    //
    // `editor --frames N` promises a command that ends by itself, and a
    // caller writing `editor --frames 2 && cmake --build ...` is entitled to
    // assume nothing of ours is still running when it returns. Letting go here
    // breaks that: the editor exits in about a quarter of a second and leaves
    // the launcher running, so the build that follows races it - and since
    // hub.exe IS a build output, the way that surfaces is
    //
    //    LINK : fatal error LNK1168: cannot open bin\hub.exe for writing
    //
    // which names nothing to do with the cause. Measured at 267ms with the
    // launcher still alive, before the wait was added.
    //
    // Interactively the opposite is right: let go, so the terminal comes back
    // and the launcher is nobody's child.
    std::string error;
    if (!editor::RelaunchHub(error, editor::HubRequest{.becauseNoProject = true,
                                                       .frameLimit      = frameLimit,
                                                       .waitForIt = frameLimit > 0})) {
        std::fprintf(stderr, "could not open the launcher: %s\n", error.c_str());
        return kExitEngine;
    }

    // SAID OUT LOUD, for whoever ran this from a terminal. A program that
    // exits zero having printed nothing looks like it did nothing, and the
    // window that does appear is not the one that was asked for.
    std::printf("no project was named, so the launcher was opened to pick one.\n");
    return kExitOk;
}

// --build-game <folder>: export a standalone game and exit, without opening a
// window.
//
// The same work File -> Build Game... does, for the same reason
// --build-scripts exists: so that an automated build, or a script that
// packages a release, can do it with nobody at the keyboard. The menu item is
// the only way most people will ever want it; this is the way a build server
// does.
int BuildGameAndExit(const char* destination) {
    const eng::BootConfig config;
    eng::Log              log;
    eng::FileSystem       fileSystem;

    // THE FILE SYSTEM FIRST, which is the opposite of what reads naturally -
    // the log is the thing everything else writes to, so it looks like it
    // should come up first. But Log::Init resolves its own file path through
    // FileSystem, and with no project root found yet it would fall back to a
    // path relative to wherever this was launched from. Engine::Init brings
    // them up in this same order, for this same reason.
    //
    // AND THE RESULT IS CHECKED, because --project reaches here too: a build
    // server can export any project rather than only the one the editor
    // happens to be installed inside, and a folder that is not a project has
    // to be reported rather than exported from.
    if (!fileSystem.Init(config)) {
        std::fprintf(stderr, "no project to export - see the message above\n");
        return kExitNoProject;
    }
    log.Init(config);

    editor::ScriptBuild::Init();
    const editor::GameExport::Result result = editor::GameExport::ExportTo(destination);

    fileSystem.Shutdown();
    log.Shutdown();

    return result.ok ? 0 : 1;
}
// --build-scripts: compile the project's scripts and exit, without opening a
// window.
//
// The editor does this on its own when it starts and whenever it regains
// focus, so this exists for the times there is nobody to press anything - a
// freshly cloned project, or an automated build that wants the library to be
// there before it runs the game.
int BuildScriptsAndExit() {
    // Only the two pieces this actually needs, started by hand: somewhere to
    // write messages, and the ability to turn a virtual path into a real one.
    // There is no window, no renderer and no scene.
    //
    // Both are subsystems, so both are objects with Init and Shutdown - the
    // same two the engine's ordered start-up calls. The settings are the
    // built-in defaults (logs/engine.log, at Info) because there is no engine
    // here to have read config/engine.json.
    const eng::BootConfig config;
    eng::Log              log;
    eng::FileSystem       fileSystem;

    // THE FILE SYSTEM FIRST, which is the opposite of what reads naturally -
    // the log is the thing everything else writes to, so it looks like it
    // should come up first. But Log::Init resolves its own file path through
    // FileSystem, and with no project root found yet it would fall back to a
    // path relative to wherever this was launched from. Engine::Init brings
    // them up in this same order, for this same reason.
    if (!fileSystem.Init(config)) {
        std::fprintf(stderr, "no project to build - see the message above\n");
        return kExitNoProject;
    }
    log.Init(config);

    editor::ScriptBuild::Init();

    // SYNCHRONOUS, deliberately. This mode exists to compile and exit, so
    // there is no window to keep responsive and nothing to do while waiting -
    // and an automated build needs the exit code to mean the build finished.
    const editor::ScriptBuild::Result result = editor::ScriptBuild::BuildAndReload();

    fileSystem.Shutdown();
    log.Shutdown();
    return result.ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    // Done FIRST, and for every mode, so that anything printed below - the
    // usage text, a build's compiler errors, the log's terminal output - is
    // visible when the editor was launched from a terminal, and silently goes
    // nowhere when it was not.
    UseParentConsoleIfThereIsOne();

    // ----------------------------------------------------------------------
    //  A REAL ARGUMENT LOOP, now that --project can accompany any of the
    //  other modes rather than replacing them.
    //
    //  It used to be a chain of comparisons against argv[1] only, which was
    //  honest when exactly one option was possible. `--project <folder>
    //  --build-scripts` has to work - it is how a launcher or a build server
    //  asks for one particular project - so position cannot carry meaning any
    //  more.
    //
    //  --build-scripts and --build-game still REPLACE opening the editor, so
    //  they are remembered and acted on after the loop, once --project has
    //  been seen wherever in the line it was written.
    // ----------------------------------------------------------------------
    int         frameLimit = 0;
    std::string projectRoot;
    const char* exportTo    = nullptr;
    bool        buildScripts = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        // A bare folder as the first argument opens it, so that dragging a
        // project folder onto the editor does the obvious thing.
        //
        // AN EMPTY NAME IS A MISTAKE, NOT AN ABSENCE - see the note on
        // --project just below, which is the same point and the likelier way
        // to arrive at it.
        if (i == 1 && arg[0] != '-') {
            if (arg[0] == '\0') {
                std::printf("the folder to open is an empty name\n");
                return kExitUsage;
            }
            projectRoot = arg;
            continue;
        }

        if (std::strcmp(arg, "--project") == 0) {
            if (i + 1 >= argc) {
                std::printf("--project needs a folder after it\n");
                return kExitUsage;
            }
            projectRoot = argv[++i];

            // AN EMPTY VALUE IS A USAGE ERROR, not "no project".
            //
            // `editor --project "$ROOT"` with ROOT unset arrives here as an
            // empty string, and treating that as "nobody named a project"
            // would open the launcher and exit zero - hiding a scripting bug
            // from the script that has it. The whole claim of this program is
            // that it opens what it was NAMED; being named nothing is a
            // different thing from not being named.
            if (projectRoot.empty()) {
                std::printf("--project was given an empty folder name\n");
                return kExitUsage;
            }
            continue;
        }
        if (std::strcmp(arg, "--build-scripts") == 0) {
            buildScripts = true;
            continue;
        }
        if (std::strcmp(arg, "--build-game") == 0) {
            if (i + 1 >= argc) {
                std::printf("--build-game needs a folder to export into\n");
                return kExitUsage;
            }
            exportTo = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--frames") == 0) {
            if (i + 1 >= argc) {
                std::printf("--frames needs a number after it\n");
                return kExitUsage;
            }
            frameLimit = std::atoi(argv[++i]);
            continue;
        }
        if (std::strcmp(arg, "--version") == 0) {
            // What a launcher probes to find out which engine an executable
            // is. One line, nothing else, exit zero.
            std::printf("%s\n", eng::EngineBuild::Running().id.c_str());
            return kExitOk;
        }

        const bool askedForHelp = std::strcmp(arg, "--help") == 0;
        if (!askedForHelp) {
            std::printf("unrecognised option '%s'\n\n", arg);
        }
        std::printf(
            "editor - the development environment\n\n"
            "  (no options)             open the launcher, to pick a project\n"
            "  <folder>                 open that project\n"
            "  --project <folder>       open that project; combines with the options below\n"
            "  --build-scripts          compile the project's scripts and exit\n"
            "  --build-game <folder>    export a standalone game and exit\n"
            "  --frames <N>             run N frames, then exit\n"
            "  --version                print which engine build this is and exit\n"
            "\n"
            "A project is a folder containing 'project.oproj' or an 'assets' folder.\n"
            "The editor opens one it was NAMED and nothing else - by --project, by a\n"
            "folder argument, or by the ENGINE_ASSET_ROOT environment variable. Given\n"
            "none of those it opens the launcher rather than guessing, which is what\n"
            "stops a cold start loading whichever project happens to sit above it.\n"
            "\n"
            "--build-scripts and --build-game are the exception and DO look upwards for\n"
            "a project, because an automated build has nobody to pick one.\n");
        return askedForHelp ? kExitOk : kExitUsage;
    }

    // Named once, here, so every mode below agrees about which project it is
    // working on - including the two headless ones, which bring the file
    // system up themselves.
    eng::FileSystem::UseRoot(projectRoot);

    if (buildScripts) {
        return BuildScriptsAndExit();
    }
    if (exportTo != nullptr) {
        return BuildGameAndExit(exportTo);
    }

    // ======================================================================
    //  NO PROJECT NAMED? OPEN THE LAUNCHER. DO NOT GUESS.
    //
    //  This used to fall through to FileSystem::Init's upward search, which
    //  walks up from the executable's own folder and opens the first project
    //  it finds. That is pleasant exactly once - the first time somebody runs
    //  the editor out of a build tree that happens to sit inside a project -
    //  and misleading every time after.
    //
    //  Once a launcher can point the editor at ANY folder on the machine,
    //  "the nearest project above the executable" stops being a default and
    //  becomes a stale reference that loads itself. A cold start would
    //  compile that project's scripts, write its .editor/session.json and
    //  show its scenes, none of which anybody asked for. It also quietly
    //  undoes the point of having a launcher, because the way to open a
    //  project becomes "be installed next to it".
    //
    //  So: an explicit project, or the launcher. One rule, and three ways to
    //  be explicit - --project, a bare folder argument (which is what
    //  dragging a project folder onto the editor does), and ENGINE_ASSET_ROOT.
    //
    //  THE TWO HEADLESS MODES ARE EXEMPT and still search, which is why they
    //  are checked above this rather than below it: they exist for when there
    //  is nobody to pick anything. `editor --build-scripts` inside a freshly
    //  cloned project is how the scripts get built before anything is opened,
    //  and tools/fresh-clone-check.sh does exactly that.
    //
    //  --frames IS NOT EXEMPT; it is handed to the launcher instead, so a run
    //  that was meant to end by itself still does. See HubRequest.
    // ======================================================================
    if (projectRoot.empty() && !TheEnvironmentNamesAProject()) {
        return OpenTheLauncherInstead(frameLimit);
    }

    editor::EditorApp app;

    if (!app.Init(projectRoot)) {
        std::fprintf(stderr,
                     "the editor could not start. The messages above name the part "
                     "that failed.\n");
        app.Shutdown();

        // TOLD APART for a launcher's benefit: a folder that is not a project
        // is the launcher's problem to report and probably its list that is
        // stale, while anything else is the engine's.
        return eng::FileSystem::HasRoot() ? kExitEngine : kExitNoProject;
    }

    app.Run(frameLimit);
    app.Shutdown();

    // ----------------------------------------------------------------------
    //  OPENING ANOTHER PROJECT HAPPENS HERE, AFTER Shutdown, AND THAT IS THE
    //  WHOLE REASON IT IS IN main().
    //
    //  The editor has let go of engine.dll and of every script library in the
    //  old project's .build/ folder, and it has written .editor/session.json
    //  and .editor/imgui.ini. Starting the new editor any earlier means two
    //  processes racing over those files, and on Windows a DLL that cannot be
    //  replaced because the old editor still holds it.
    //
    //  See ProjectMenu.h for why switching projects restarts at all.
    // ----------------------------------------------------------------------
    if (editor::ProjectMenu::WhatNext() == editor::ProjectMenu::Exit::OpenHub) {
        std::string error;
        if (!editor::RelaunchHub(error)) {
            std::fprintf(stderr, "could not open the launcher: %s\n", error.c_str());
            return kExitEngine;
        }
    } else if (editor::ProjectMenu::WhatNext() ==
               editor::ProjectMenu::Exit::OpenProject) {
        const std::string& next = editor::ProjectMenu::ProjectToOpen();

        std::string error;
        if (!editor::Relaunch(next, error)) {
            // SAID OUT LOUD. The old editor has already closed by this point,
            // so a silent failure here is indistinguishable from the editor
            // crashing on the way out.
            std::fprintf(stderr, "could not reopen the editor on '%s': %s\n",
                         next.c_str(), error.c_str());
            return kExitEngine;
        }
    }

    return kExitOk;
}
