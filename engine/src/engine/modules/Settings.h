#pragma once

// ============================================================================
//  Settings.h - reading JSON, and the settings the engine starts up with.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Math.h>

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

// ============================================================================
//  Json.h - reading and writing the .json files the engine uses.
//
//  Two kinds of file are stored as JSON:
//    config/engine.json   settings: window size, key bindings, log level
//    assets/scenes/*.json scenes: every entity and every component in a level
//
//  WHY JSON
//  It is plain text, so a scene can be opened in any editor, read, diffed and
//  fixed by hand. It nests, so a component's fields sit naturally inside an
//  entity. And it needs no schema file or code generation step.
//
//  WHY nlohmann/json
//  It is the most widely used JSON library for C++ and it is header-only -
//  there is nothing to install, CMake downloads it. Its whole API is one type
//  that behaves like the containers you already know:
//
//      Json document = ParseJson(text, error);
//      int width = ReadInt(document["window"], "width", 1280);
//
//      Json entity;
//      entity["name"] = "Player";
//      entity["position"] = { 10.0f, 20.0f };
//
//  The helper functions below exist because the library's own accessors throw
//  an exception when a key is missing or holds the wrong type. That is a
//  reasonable default for a program reading its own output, and the wrong one
//  for a file a person edits by hand. Each helper takes the value to use when
//  the key is absent, warns (naming the key) when it is present but the wrong
//  type, and never throws.
// ============================================================================




namespace eng {

// The one type the whole JSON library revolves around. A Json can hold a
// number, a string, a true/false, a list, or a set of named fields - and you
// find out which with is_number(), is_string(), is_array(), is_object().
using Json = nlohmann::json;

// Parses text into a Json. On a syntax error this returns an empty object and
// fills outError with the parser's message, including where in the file the
// problem is. It never throws.
Json ParseJson(std::string_view text, std::string& outError);

// ---- safe readers -----------------------------------------------------------
//
// Each takes the object to look in, the key to look for, and the value to use
// if that key is not there. `where` is only used to make a warning message say
// which part of the file was at fault.

int         ReadInt(const Json& object, std::string_view key, int fallback,
                    std::string_view where = "");
float       ReadFloat(const Json& object, std::string_view key, float fallback,
                      std::string_view where = "");
bool        ReadBool(const Json& object, std::string_view key, bool fallback,
                     std::string_view where = "");
std::string ReadString(const Json& object, std::string_view key,
                       std::string_view fallback, std::string_view where = "");

// A position or a size, written in the file as a two-element list: [12.5, -4].
Vec2 ReadVec2(const Json& object, std::string_view key, Vec2 fallback,
              std::string_view where = "");

// True when `object` is a set of named fields and contains `key`. Use this
// before reading when "absent" and "present but zero" mean different things.
bool HasKey(const Json& object, std::string_view key);

// ---- writing ---------------------------------------------------------------

// Stores a Vec2 as [x, y], which is the shape ReadVec2 expects.
void WriteVec2(Json& object, std::string_view key, Vec2 value);

} // namespace eng


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

    // "startup" - the scene loaded when the game starts.
    //
    // Even the first level is data. A scene name compiled into the engine
    // would be a piece of game content living in the engine, which is exactly
    // the thing the engine/game split exists to prevent.
    std::string startupScene = "scenes/orbit_test.json";
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
