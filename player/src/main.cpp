// ============================================================================
//  main.cpp - the player: a game running with no editor attached.
//
//  THIS IS THE PROGRAM A FINISHED GAME SHIPS AS. Not an example of one - this
//  is it. Build it, put it beside engine.dll, SDL3.dll, assets/ and
//  config/engine.json, and that folder is a game somebody can run.
//
//  ==========================================================================
//  NOTICE WHAT IS NOT IN THIS FILE: the game.
//
//  There is no player, no score, no collectible and no name of any scene. This
//  program opens whichever scene the configuration names and runs the frame
//  loop. Everything that makes the Collector game the Collector game lives in
//  assets/ - the scene file places the entities, and the scripts in
//  assets/scripts/ give them their behaviour.
//
//  That is deliberate and it is the whole point. Game logic written HERE would
//  be compiled into this executable, which the editor is not - so it could
//  never run when somebody pressed Play in the editor. Written as a script, it
//  runs in both, because both programs load the same userContent.dll at run
//  time. See scene/ScriptLibrary.h.
//
//  The rule that follows, and it is worth telling students once:
//
//      If you want to press Play in the editor and see it work,
//      it has to be a script.
//
//  ==========================================================================
//  THE WHOLE LOOP IS FOUR CALLS.
//
//      while (engine.BeginFrame()) {   // read input, work out how many steps
//          engine.Simulate();          // run those steps
//          engine.RenderFrame();       // draw
//          engine.PresentFrame();      // show it
//      }
//
//  Engine::Run() is exactly that loop and nothing else, so a player with no
//  options at all could call it instead. It is written out here for two
//  reasons: it is the clearest place in the project to see the shape of a
//  frame, and this program needs to slip two of its own decisions in - when to
//  quit, and the frame cap used for testing.
//
//  The editor's EditorApp::Run is the same four calls with the interface drawn
//  between RenderFrame and PresentFrame. Comparing the two is the fastest way
//  to understand what an editor actually is.
// ============================================================================

#include <engine/Engine.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void PrintUsage() {
    std::printf(
        "player - a game, running without the editor\n"
        "\n"
        "  (no options)            open the scene named in config/engine.json\n"
        "  --scene <path>          open a different scene, e.g. "
        "scenes/collector.oscene\n"
        "  --scene-index <N>       open scene N from the build list instead\n"
        "  --config <path>         use a different settings file\n"
        "  --frames <N>            run N frames then exit (useful for testing)\n"
        "  --help                  this text\n");
}

} // namespace

int main(int argc, char** argv) {
    eng::Engine::Options options;
    int                  frameLimit = 0;

    // -1 means "nobody asked". Scene 0 is a perfectly good answer, so a
    // sentinel is needed rather than treating zero as unset.
    int sceneIndex = -1;

    // A small hand-written argument loop. It starts at 1 because argv[0] is
    // the program's own name, not an option.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        // A helper for the options that take a value after them, so the
        // "is there actually another argument?" check is written once.
        const auto next = [&](std::string& out) {
            if (i + 1 < argc) {
                out = argv[++i];
                return true;
            }
            std::printf("%s needs a value after it\n", arg.c_str());
            return false;
        };

        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
        if (arg == "--scene") {
            if (!next(options.sceneOverride)) { return 1; }
        } else if (arg == "--config") {
            if (!next(options.configPath)) { return 1; }
        } else if (arg == "--scene-index") {
            std::string value;
            if (!next(value)) { return 1; }
            sceneIndex = std::atoi(value.c_str());
        } else if (arg == "--frames") {
            std::string value;
            if (!next(value)) { return 1; }
            frameLimit = std::atoi(value.c_str());
        } else {
            std::printf("unknown option '%s'\n\n", arg.c_str());
            PrintUsage();
            return 1;
        }
    }

    eng::Engine& engine = eng::Engine::Get();
    if (!engine.Init(options)) {
        return 1;
    }

    // --scene-index, applied AFTER Init.
    //
    // It has to be here rather than inside Init, because the build list is
    // only read during Init - there is nothing to number until then. And here
    // is a SAFE place to change scene: the frame loop has not started, so
    // nothing is part way through walking a list of entities that is about to
    // be destroyed. That is the same reason a script should not call
    // LoadSceneByIndex from a script update; see Engine::LoadSceneByIndex.
    if (sceneIndex >= 0) {
        std::string sceneError;
        if (!engine.LoadSceneByIndex(static_cast<std::size_t>(sceneIndex), sceneError)) {
            std::printf("could not open scene %d: %s\n", sceneIndex, sceneError.c_str());
            engine.Shutdown();
            return 1;
        }
    }

    int frames = 0;
    while (engine.BeginFrame()) {
        engine.Simulate();
        engine.RenderFrame();
        engine.PresentFrame();

        // QUITTING BELONGS TO THE PROGRAM, NOT TO A SCRIPT.
        //
        // A script calling RequestQuit would also run inside the editor, where
        // it would close the editor out from under somebody who was only
        // play-testing - losing their unsaved scene to a keypress that, in the
        // game, politely closes the window. So the game's rules never ask to
        // quit; the program that can safely honour the request reads the action
        // itself.
        //
        // An ACTION, not a key. Search this file for a key code and there is
        // nothing to find, which is why rebinding it in config/engine.json
        // needs no rebuild.
        if (eng::InputMap::IsPressed("Quit")) {
            engine.RequestQuit();
        }

        // A capped run, so an automated test can start the program and be sure
        // it will finish. Zero means "until the window is closed", which is
        // what a player actually wants.
        if (frameLimit > 0 && ++frames >= frameLimit) {
            break;
        }
    }

    engine.Shutdown();
    return 0;
}
