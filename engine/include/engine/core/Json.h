#pragma once

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

#include <engine/math/Vec2.h>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

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
