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

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

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
    // write messages, and the ability to turn "scripts/" into a real path.
    // There is no window, no renderer and no scene.
    eng::Log::Init("logs/engine.log", eng::LogLevel::Info);
    eng::FileSystem::Init();

    editor::ScriptBuild::Init();
    const editor::ScriptBuild::Result result = editor::ScriptBuild::BuildAndReload();

    eng::Log::Shutdown();
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
