// ============================================================================
//  main.cpp - the game, running on its own with no editor attached.
//
//  This is the program a finished game would ship as. It links the engine and
//  cannot reach SDL, because the engine keeps SDL to itself.
//
//  The whole loop is four calls:
//
//      while (engine.BeginFrame()) {   // read input, work out how many steps
//          engine.Simulate();          // run those steps
//          engine.RenderFrame();       // draw
//          engine.PresentFrame();      // show it
//      }
//
//  Engine::Run() is exactly that loop, and the default mode below just calls
//  it. The --game mode writes the loop out by hand so that the sample game can
//  stop when the round ends.
// ============================================================================

#include "CollectorGame.h"

#include <engine/Engine.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void PrintUsage() {
    std::printf(
        "sandbox - the game, running without the editor\n"
        "\n"
        "  (no options)            open the scene named in config/engine.json\n"
        "  --game                  play the sample game, Collector\n"
        "  --autoplay              the same game, played automatically\n"
        "  --scene <path>          open a different scene, e.g. scenes/collector.json\n"
        "  --config <path>         use a different settings file\n"
        "  --frames <N>            run N frames then exit (useful for testing)\n"
        "  --help                  this text\n");
}

// Just open a scene and run it. This is the mode that shows the orbiting
// hierarchy from assets/scenes/orbit_test.json.
int RunScene(const eng::Engine::Options& options, int frameLimit) {
    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    if (frameLimit <= 0) {
        // The ordinary case: run until the window is closed.
        eng::Engine::Get().Run();
    } else {
        // A capped run, so an automated test can start the program and be sure
        // it will finish.
        for (int frame = 0; frame < frameLimit && eng::Engine::Get().BeginFrame();
             ++frame) {
            eng::Engine::Get().Simulate();
            eng::Engine::Get().RenderFrame();
            eng::Engine::Get().PresentFrame();
        }
    }

    eng::Engine::Get().Shutdown();
    return 0;
}

// Play the sample game.
int RunGame(eng::Engine::Options options, bool autoplay, int frameLimit) {
    // The game lives in its own scene file. Named here rather than in the
    // engine, because which scene a game starts in is a decision belonging to
    // the game.
    if (options.sceneOverride.empty()) {
        options.sceneOverride = "scenes/collector.json";
    }

    if (!eng::Engine::Get().Init(options)) {
        return 1;
    }

    game::CollectorGame collector;
    if (!collector.Init()) {
        eng::Engine::Get().Shutdown();
        return 1;
    }
    collector.SetAutopilot(autoplay);

    // The loop is written out here rather than calling Engine::Run(), because
    // this mode needs an extra reason to stop: the round finishing.
    int frames = 0;
    while (eng::Engine::Get().BeginFrame()) {
        eng::Engine::Get().Simulate();
        eng::Engine::Get().RenderFrame();
        eng::Engine::Get().PresentFrame();
        ++frames;

        if (frameLimit > 0 && frames >= frameLimit) {
            break;
        }
        if (autoplay && collector.IsFinished()) {
            // Give the player a moment to read "YOU WIN" before the window
            // closes, rather than vanishing the instant the last pickup goes.
            static int lingerFrames = 0;
            if (++lingerFrames > 120) {
                break;
            }
        }
    }

    ENGINE_LOG_INFO(eng::Channels::kGame, "finished with {} collected after {} frames",
                    collector.Collected(), frames);

    collector.Shutdown();
    eng::Engine::Get().Shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    eng::Engine::Options options;

    bool game       = false;
    bool autoplay   = false;
    int  frameLimit = 0;

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
        if (arg == "--game") {
            game = true;
        } else if (arg == "--autoplay") {
            game     = true;
            autoplay = true;
        } else if (arg == "--scene") {
            if (!next(options.sceneOverride)) { return 1; }
        } else if (arg == "--config") {
            if (!next(options.configPath)) { return 1; }
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

    return game ? RunGame(options, autoplay, frameLimit)
                : RunScene(options, frameLimit);
}
