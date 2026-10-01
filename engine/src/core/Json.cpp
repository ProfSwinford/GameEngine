// =============================================================================
//  Json.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Json.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/core/Json.h>
#include <engine/core/Log.h>

namespace eng {
namespace {
    
std::string Describe(std::string_view where, std::string_view key) {
    if (where.empty()) {
            return std::string(key);
    }
    return std::string(where) + "." + std::string(key);
}

const Json* Lookup(const Json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    const auto it = object.find(std::string(key));
    return (it != object.end()) ? &(*it) : nullptr;

}
} // namespace

// Turns text into a Json document. On failure it returns an empty object and
// puts the reason - including the line number - into outError, rather than
// throwing.
Json ParseJson(std::string_view text, std::string& outError) {
    Json document =
        Json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);

    if (document.is_discarded()) {
        outError = "the file is not valid JSON. Please check formatting and try again.";
        return Json::object();
    }

    outError.clear();
    return document;
}

// Reads a whole number from a field. Missing or wrong-typed fields give back
// the fallback and log a warning naming the file that asked, so a typo in a
// scene file is reported instead of silently becoming zero.
int ReadInt(const Json& object, std::string_view key, int fallback,
            std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_number_integer()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be a whole number; using {}",
                        Describe(where, key), fallback);
        return fallback;
    }
    return value->get<int>();

}

// Reads a decimal number from a field, falling back the same way ReadInt does.
float ReadFloat(const Json& object, std::string_view key, float fallback,
                std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_number()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be a number; using {}",
                        Describe(where, key), fallback);
        return fallback;
    }
    return value->get<float>();
}

// Reads a true/false field, falling back the same way ReadInt does.
bool ReadBool(const Json& object, std::string_view key, bool fallback,
              std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_boolean()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be true or false; using {}",
                        Describe(where, key), fallback);
        return fallback;
    }
    return value->get<bool>();
}

// Reads a text field, falling back the same way ReadInt does.
std::string ReadString(const Json& object, std::string_view key,
                       std::string_view fallback, std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return std::string(fallback);
    }
    if (!value->is_string()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be text; using {}",
                        Describe(where, key), fallback);
        return std::string(fallback);
    }
    return value->get<std::string>();
}

// Reads a two-number array as a Vec2 - the shape scene files use for every
// position, scale and size.
Vec2 ReadVec2(const Json& object, std::string_view key, Vec2 fallback,
              std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_array() || value ->size() != 2 || 
        !(*value)[0].is_number() || !(*value)[1].is_number()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be two numbers like [10, 20]; using [{}, {}]",
                        Describe(where, key), fallback.x, fallback.y);
        return fallback;
    }
    return Vec2{(*value)[0].get<float>(), (*value)[1].get<float>()};
}

// Is this field present at all? Used where "absent" and "set to zero" mean
// different things.
bool HasKey(const Json& object, std::string_view key) {
    return Lookup(object, key) != nullptr;
}

// Writes a Vec2 back out as a two-number array, in the same shape ReadVec2
// expects - which is what makes load, edit, save, load give back what you had.
void WriteVec2(Json& object, std::string_view key, Vec2 value) {
    Json pair = Json::array();
    pair.push_back(value.x);
    pair.push_back(value.y);
    object[std::string(key)] = std::move(pair);
}

} // namespace eng
