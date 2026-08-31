// =============================================================================
//  WEEK 2 - the IDE's entry point. Deliberately boring: construct, Init, Run,
//  Shutdown. Everything interesting is in EditorApp or in a panel.
//
//  Two executables exist from Week 2 onward:
//    editor   - this. The IDE.
//    sandbox  - the standalone game runtime. No IDE, no ImGui.
//
//  The sandbox is kept working every week. It is what the Week 10 gate is
//  built in, and it is what proves the engine can ship without its tools.
// =============================================================================

#include "EditorApp.h"

#include <cstdio>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    editor::EditorApp app;

    if (!app.Init()) {
        std::fprintf(stderr,
                     "editor: initialisation failed. The boot log above names the "
                     "subsystem that refused to start.\n");
        app.Shutdown();
        return 1;
    }

    app.Run();
    app.Shutdown();
    return 0;
}
