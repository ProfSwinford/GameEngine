// ============================================================================
//  Settings.cpp - reading JSON, and the settings the engine starts up with.
//  See Settings.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Files.h>
#include <engine/modules/Settings.h>

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


// ============================================================================
//  Config.cpp - reading config/engine.json. See Config.h.
//
//  Every value is read with one of the safe helpers from Json.h, each of which
//  takes the default to use when the key is missing. That is why this file has
//  almost no error handling in it: the "what if this key is not there" case is
//  answered once, in the helper, instead of once per setting.
// ============================================================================


namespace eng {
namespace {

// Returns the named section of the document, or an empty object when the file
// does not have that section.
//
// Written as a function rather than as `document["window"]` because square
// brackets on a Json INSERT an empty entry when the key is missing, which
// would quietly add sections to the document just by reading it.
const Json& Section(const Json& document, const char* name) {
    static const Json kEmpty = Json::object();
    if (!document.is_object()) {
        return kEmpty;
    }
    const auto it = document.find(name);
    return (it != document.end() && it->is_object()) ? *it : kEmpty;
}

} // namespace

bool LoadBootConfig(std::string_view virtualPath, BootConfig& outConfig,
                    Json& outDocument, std::string& outError) {
    outDocument = Json::object();

    std::string text;
    std::string readError;
    if (!FileSystem::ReadTextFile(virtualPath, text, readError)) {
        // Not a failure. Every setting has a sensible default, so the engine
        // starts normally; it just says which file it could not find, because
        // "why is my window the wrong size" is otherwise a puzzle.
        outError = "no settings file at '" + std::string(virtualPath) +
                   "'; using built-in defaults";
        ENGINE_LOG_WARN(Channels::kConfig, "{}", outError);
        return true;
    }

    std::string parseError;
    Json document = ParseJson(text, parseError);
    if (!parseError.empty()) {
        outError = std::string(virtualPath) + ": " + parseError;
        ENGINE_LOG_ERROR(Channels::kConfig, "{}", outError);
        // Returning false here is the one case that matters: the file is
        // present and somebody clearly meant something by it, so quietly
        // ignoring the whole thing would be wrong.
        return false;
    }

    // --- window -----------------------------------------------------------
    const Json& window = Section(document, "window");
    outConfig.windowWidth  = ReadInt(window, "width", outConfig.windowWidth, "window");
    outConfig.windowHeight = ReadInt(window, "height", outConfig.windowHeight, "window");
    outConfig.windowTitle  = ReadString(window, "title", outConfig.windowTitle, "window");

    // --- logging ----------------------------------------------------------
    const Json& logging = Section(document, "logging");
    outConfig.logFile = ReadString(logging, "file", outConfig.logFile, "logging");

    const std::string thresholdText =
        ReadString(logging, "threshold", ToString(outConfig.logThreshold), "logging");
    if (!ParseLogLevel(thresholdText, outConfig.logThreshold)) {
        ENGINE_LOG_WARN(Channels::kConfig,
                        "logging.threshold is '{}', which is not one of Info, Warning or "
                        "Error; using {}",
                        thresholdText, ToString(outConfig.logThreshold));
    }

    // --- tunables ---------------------------------------------------------
    const Json& tunables = Section(document, "tunables");
    outConfig.logBufferCapacity =
        ReadInt(tunables, "logBufferCapacity", outConfig.logBufferCapacity, "tunables");
    outConfig.gizmoCircleSegments =
        ReadInt(tunables, "gizmoCircleSegments", outConfig.gizmoCircleSegments, "tunables");
    outConfig.fixedTimestepSeconds = 
        ReadFloat(tunables, "fixedTimestepSeconds", outConfig.fixedTimestepSeconds, "tunables");
    outConfig.maxStepsPerFrame =
        ReadInt(tunables, "maxStepsPerFrame", outConfig.maxStepsPerFrame, "tunables");

    // --- startup ----------------------------------------------------------
    outConfig.startupScene =
        ReadString(Section(document, "startup"), "scene", outConfig.startupScene, "startup");

    // The whole document is handed back so that InputMap can read its own
    // "input" section from it. Parsing the file once and sharing the result
    // beats every subsystem opening it again.
    outDocument = std::move(document);

    outError.clear();
    ENGINE_LOG_INFO(Channels::kConfig, "settings loaded from '{}'", virtualPath);
    return true;
}

} // namespace eng
