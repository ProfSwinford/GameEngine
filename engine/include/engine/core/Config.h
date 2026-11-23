#pragma once

// ============================================================================
//  Config.h - the settings read from config/engine.json at start-up.
//
//  WHY SETTINGS LIVE IN A FILE INSTEAD OF IN THE CODE
//  Every value below started life as a number typed into the source. That is
//  fine until somebody wants to change one, at which point the answer is
//  "rebuild the whole engine" - which is ninety seconds, and you will do it
//  thirty times in an afternoon while you find the value you actually wanted.
//  Reading them from a file makes trying a value instant and lets somebody who
//  does not write C++ change the window size.
//
//  WHAT HAPPENS WHEN THE FILE IS WRONG
//  Config files are edited by people, so they are wrong regularly. The rules
//  are the same everywhere in the engine:
//
//    FILE MISSING     warn, and start with the defaults below. Refusing to
//                     start because a settings file is absent would make a
//                     fresh copy of the project unrunnable.
//    BAD JSON         report the problem, then start with the defaults.
//                     Reported, never silent: a setting being ignored and a
//                     setting being obeyed look identical from the outside.
//    WRONG TYPE       warn, naming the key, use the default for that ONE key
//                     and keep everything else. "width": "big" should not cost
//                     you your key bindings.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/math/Vec2.h>
#include <engine/core/Log.h>
#include <engine/core/WorldUnit.h>

#include <string>
#include <string_view>

namespace eng {

// Every setting the engine reads at start-up, with the value it uses when the
// file does not mention it.
struct BootConfig {
    // "window"
    int         windowWidth  = 1280;
    int         windowHeight = 720;
    std::string windowTitle  = "Engine2D";

    // "logging"
    LogLevel    logThreshold = LogLevel::Info;
    std::string logFile      = "logs/engine.log";

    // "tunables"
    int         logBufferCapacity    = 4096;    // messages kept for the Console
    int         gizmoCircleSegments  = 24;      // how round a drawn circle looks
    float       fixedTimestepSeconds = 1.0f / 60.0f;
    int         maxStepsPerFrame     = 5;

    // "window" -> "fullscreen". Borderless, keeping the desktop's own mode;
    // see platform/Window.h for why that rather than a display mode change.
    bool        fullscreen = false;

    // ======================================================================
    //  "window" -> "resizable". May the player drag the window to any size?
    //
    //  TRUE BY DEFAULT, which is what the engine has always done - so no
    //  existing project changes by this key arriving.
    //
    //  WHY TURNING IT OFF IS A REAL REQUEST. Letterboxing keeps a pinned
    //  shape correct at any window size, so resizing is no longer BROKEN -
    //  but a game can still want to forbid it. A fixed window is part of a
    //  look: a 480x270 pixel game presented at exactly 3x has a right answer
    //  and a hundred wrong ones, and leaving the player to find the right one
    //  by dragging is not a style decision anybody wants to delegate.
    //
    //  ONLY THE GAME OBEYS IT. The editor hosts the same engine and must not
    //  lock its own window because a project said so - see
    //  Engine::Options::gameOwnsTheWindow.
    // ======================================================================
    bool        windowResizable = true;

    // ======================================================================
    //  "presentation" -> "mode": "free", "16:9" or "1920x1080".
    //
    //  WHAT SHAPE THE FINISHED GAME IS. Free - the default, and what the
    //  engine did before this existed - lets the game fill the window, which
    //  means a wider monitor sees MORE OF THE LEVEL. Pinning a shape or a
    //  size fixes the framing for every player and puts black in whatever
    //  space is left over.
    //
    //  KEPT AS TEXT rather than parsed into this struct, because the one
    //  parser lives in render/Presentation.h and the editor's own
    //  custom-aspect box calls the same one. Two copies of "what does 16:9
    //  mean" is exactly the drift worth avoiding.
    // ======================================================================
    std::string presentation = "free";

    // "units" - how many pixels one world unit is.
    //
    // The size of an ordinary thing: one tile, one character's width, one
    // grid square. Authored sizes are written as fractions of it, so they
    // survive a change of art resolution. See core/WorldUnit.h for why that
    // is worth a setting of its own, and physics/Collider.h for what reads
    // it.
    float       worldUnitPixels = WorldUnit::kDefaultPixels;

    // "physics"
    //
    // UNITY'"'"'S NUMBER IS -9.81, and that is metres per second squared. A 2D
    // game'"'"'s units are usually PIXELS, so -9.81 is a barely visible drift -
    // about 30 pixels in two and a half seconds. At roughly 100 pixels to the
    // metre, -981 is the same gravity as Earth'"'"'s.
    //
    // The engine'"'"'s own default stays at Unity'"'"'s value so that the number means
    // what a physics textbook says it means; the config file is where a
    // project says what its units are.
    Vec2        gravity = Vec2{0.0f, -9.81f};
    int         solverIterations = 4;

    // "startup" - the scene loaded when the game starts.
    //
    // Even the first level is data. A scene name compiled into the engine
    // would be a piece of game content living in the engine, which is exactly
    // the thing the engine/game split exists to prevent.
    std::string startupScene = "scenes/orbit_test.oscene";
};

// Reads config/engine.json.
//
// `outDocument` receives the whole parsed file, because other subsystems need
// their own sections of it: the input bindings, for instance, are read by
// InputMap rather than here.
//
// Returns false only when the file exists but could not be parsed at all -
// the one case where starting up with defaults would silently ignore what
// somebody actually wrote. A missing file is not a failure.
bool LoadBootConfig(std::string_view virtualPath, BootConfig& outConfig,
                    Json& outDocument, std::string& outError);
} // namespace eng
