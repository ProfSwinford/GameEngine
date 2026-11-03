// ============================================================================
//  Config.cpp - reading config/engine.json. See Config.h.
//
//  Every value is read with one of the safe helpers from Json.h, each of which
//  takes the default to use when the key is missing. That is why this file has
//  almost no error handling in it: the "what if this key is not there" case is
//  answered once, in the helper, instead of once per setting.
// ============================================================================

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

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

    // --- physics ----------------------------------------------------------
    const Json& physics = Section(document, "physics");
    outConfig.gravity = ReadVec2(physics, "gravity", outConfig.gravity, "physics");
    outConfig.solverIterations =
        ReadInt(physics, "solverIterations", outConfig.solverIterations, "physics");

    // ======================================================================
    //  A MISSING "physics" SECTION IS SAID OUT LOUD, because this is the one
    //  default in the file that is wrong for every project that uses it.
    //
    //  The built-in gravity is Unity's -9.81, which is metres per second
    //  squared, and this engine works in pixels - so a project that does not
    //  override it accelerates everything at a hundredth of the intended
    //  rate. Fall time goes as 1/sqrt(g): everything falls and jumps about
    //  ten times too slowly, and it reads as the whole game running in slow
    //  motion rather than as a settings file missing four lines.
    //
    //  THE SAME SHAPE AS THE "input" WARNING in InputMap::LoadBindings, and
    //  for the same reason: silence about an absent section is only kind when
    //  the default is one somebody would have chosen.
    //
    //  Projects created before the template wrote this section are the ones
    //  that need it, which is why it names the JSON to paste rather than
    //  pointing at the documentation.
    // ======================================================================
    if (!document.is_object() || document.find("physics") == document.end()) {
        ENGINE_LOG_WARN(
            Channels::kConfig,
            "the settings file has no \"physics\" section, so gravity is the "
            "built-in {} px/s^2 - Unity's metres-per-second number used as pixels, "
            "which is a barely visible drift rather than gravity. Things will fall "
            "about ten times too slowly. A 2D game in pixels wants roughly -981: "
            "add  \"physics\": {{ \"gravity\": [0.0, -981.0] }}",
            outConfig.gravity.y);
    }

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
