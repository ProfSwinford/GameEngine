// ============================================================================
//  Json.cpp - the safe JSON readers declared in Json.h.
//
//  Every function here follows the same three-step shape:
//    1. is the key there at all?     -> no: quietly use the fallback
//    2. is it the type we expected?  -> no: warn, naming the key, use the fallback
//    3. otherwise                    -> return the value
//
//  Step 2 is the one that earns its keep. A config file with "width": "big"
//  should not cost you your key bindings, and it should not be silent either -
//  a setting that is being ignored and a setting that is being obeyed look
//  identical from the outside unless something says so.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/core/Log.h>

namespace eng {
namespace {

// Builds the "window.width" part of a warning message.
std::string Describe(std::string_view where, std::string_view key) {
    if (where.empty()) {
        return std::string(key);
    }
    return std::string(where) + "." + std::string(key);
}

// Returns a pointer to the value at `key`, or nullptr if it is not there.
// `find` is used rather than `object[key]` because square brackets on a Json
// INSERT a null entry when the key is missing, which would quietly modify the
// document just by reading it.
const Json* Lookup(const Json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(std::string(key));
    return (it != object.end()) ? &(*it) : nullptr;
}

} // namespace

Json ParseJson(std::string_view text, std::string& outError) {
    // The three extra arguments to parse() are:
    //   nullptr - no callback that inspects the document as it is read
    //   false   - do NOT throw on a syntax error; return a special value instead
    //   true    - allow // comments, which makes a hand-edited config far nicer
    Json document = Json::parse(text, nullptr, /*allow_exceptions=*/false,
                                /*ignore_comments=*/true);

    if (document.is_discarded()) {
        // is_discarded() is how the non-throwing parse reports "this was not
        // valid JSON".
        outError = "the file is not valid JSON (check for a missing comma, quote "
                   "or closing brace)";
        return Json::object();
    }

    outError.clear();
    return document;
}

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

float ReadFloat(const Json& object, std::string_view key, float fallback,
                std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    // is_number() accepts both 3 and 3.5, because a person writing a config
    // file should not have to type "1.0" to mean one.
    if (!value->is_number()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be a number; using {}",
                        Describe(where, key), fallback);
        return fallback;
    }
    return value->get<float>();
}

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

std::string ReadString(const Json& object, std::string_view key,
                       std::string_view fallback, std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return std::string(fallback);
    }
    if (!value->is_string()) {
        ENGINE_LOG_WARN(Channels::kConfig, "'{}' should be text; using '{}'",
                        Describe(where, key), fallback);
        return std::string(fallback);
    }
    return value->get<std::string>();
}

Vec2 ReadVec2(const Json& object, std::string_view key, Vec2 fallback,
              std::string_view where) {
    const Json* value = Lookup(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_array() || value->size() != 2 ||
        !(*value)[0].is_number() || !(*value)[1].is_number()) {
        ENGINE_LOG_WARN(Channels::kConfig,
                        "'{}' should be two numbers like [10, 20]; using [{}, {}]",
                        Describe(where, key), fallback.x, fallback.y);
        return fallback;
    }
    return Vec2{(*value)[0].get<float>(), (*value)[1].get<float>()};
}

bool HasKey(const Json& object, std::string_view key) {
    return Lookup(object, key) != nullptr;
}

void WriteVec2(Json& object, std::string_view key, Vec2 value) {
    // Json::array() builds an empty list; the two pushes fill it in. Written
    // this way rather than with braces because `{x, y}` is ambiguous to the
    // library - it cannot tell a two-element list from a key/value pair.
    Json pair = Json::array();
    pair.push_back(value.x);
    pair.push_back(value.y);
    object[std::string(key)] = std::move(pair);
}

} // namespace eng
