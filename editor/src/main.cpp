// ============================================================================
//  main.cpp - where the editor starts.
//
//  Deliberately dull: build the application object, start it, run it, stop it.
//  Everything interesting is in EditorApp or in one of the panels.
//
//  There are two programs in this project:
//    editor   - this one, the development environment
//    sandbox  - the game running on its own, with no editor at all
// ============================================================================

#include "EditorApp.h"
#include "ScriptBuild.h"

#include <engine/modules/Settings.h>
#include <engine/modules/Diagnostics.h>
#include <engine/modules/Files.h>

#include <cstdio>
#include <cstring>

namespace {

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

    log.Init(config);
    fileSystem.Init(config);

    editor::ScriptBuild::Init();
    const editor::ScriptBuild::Result result = editor::ScriptBuild::BuildAndReload();

    fileSystem.Shutdown();
    log.Shutdown();
    return result.ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    // The editor takes at most one option, so there is no argument loop to
    // write - anything other than the two below is a mistake worth reporting.
    if (argc > 1) {
        if (std::strcmp(argv[1], "--build-scripts") == 0) {
            return BuildScriptsAndExit();
        }
        const bool askedForHelp = std::strcmp(argv[1], "--help") == 0;
        std::printf("editor - the development environment\n\n"
                    "  (no options)      open the editor\n"
                    "  --build-scripts   compile the project's scripts and exit\n");
        // Zero when they asked for help, non-zero when they mistyped something.
        return askedForHelp ? 0 : 1;
    }

    editor::EditorApp app;

    if (!app.Init()) {
        std::fprintf(stderr,
                     "the editor could not start. The messages above name the part "
                     "that failed.\n");
        app.Shutdown();
        return 1;   // a non-zero exit code means "something went wrong"
    }

    app.Run();
    app.Shutdown();
    return 0;
}
