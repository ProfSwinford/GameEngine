// =============================================================================
//  Json.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Json.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/core/Json.h>

namespace eng {

// Turns text into a Json document. On failure it returns an empty object and
// puts the reason - including the line number - into outError, rather than
// throwing.
Json ParseJson(std::string_view /*text*/, std::string& /*outError*/) {
    return Json::object();
}

// Reads a whole number from a field. Missing or wrong-typed fields give back
// the fallback and log a warning naming the file that asked, so a typo in a
// scene file is reported instead of silently becoming zero.
int ReadInt(const Json& /*object*/, std::string_view /*key*/, int fallback,
            std::string_view /*where*/) {
    return fallback;
}

// Reads a decimal number from a field, falling back the same way ReadInt does.
float ReadFloat(const Json& /*object*/, std::string_view /*key*/, float fallback,
                std::string_view /*where*/) {
    return fallback;
}

// Reads a true/false field, falling back the same way ReadInt does.
bool ReadBool(const Json& /*object*/, std::string_view /*key*/, bool fallback,
              std::string_view /*where*/) {
    return fallback;
}

// Reads a text field, falling back the same way ReadInt does.
std::string ReadString(const Json& /*object*/, std::string_view /*key*/,
                       std::string_view fallback, std::string_view /*where*/) {
    return std::string(fallback);
}

// Reads a two-number array as a Vec2 - the shape scene files use for every
// position, scale and size.
Vec2 ReadVec2(const Json& /*object*/, std::string_view /*key*/, Vec2 fallback,
              std::string_view /*where*/) {
    return fallback;
}

// Is this field present at all? Used where "absent" and "set to zero" mean
// different things.
bool HasKey(const Json& /*object*/, std::string_view /*key*/) {
    return false;
}

// Writes a Vec2 back out as a two-number array, in the same shape ReadVec2
// expects - which is what makes load, edit, save, load give back what you had.
void WriteVec2(Json& /*object*/, std::string_view /*key*/, Vec2 /*value*/) {
}

} // namespace eng
