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

#include <cstdio>

int main(int argc, char** argv) {
    // The editor takes no command-line arguments. Naming them and then casting
    // them to void is the standard way to say "yes, I know these exist, and I
    // am deliberately not using them" without the compiler warning about it.
    (void)argc;
    (void)argv;

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
