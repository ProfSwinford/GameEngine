// =============================================================================
//  Config.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Config.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/core/Config.h>
#include <engine/fs/FileSystem.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>

namespace eng {
namespace {

    const Json& Section(const Json& document, const char* name) {
        static const Json kEmpty = Json::object();
        if (!document.is_object()) {
            return kEmpty;
        }
        const auto it = document.find(name);

        return (it != document.end() && it->is_object()) ? *it : kEmpty;
    }
}
// Reads config/engine.json into a BootConfig - window size, log level, fixed
// timestep, starting scene. Also hands back the parsed document, because the
// input bindings are read out of it later. Returns false if the file is
// missing or malformed, which is the one thing that stops the engine starting.

    
    bool LoadBootConfig(std::string_view virtualPath, BootConfig& outConfig,
                    Json& outDocument, std::string& outError) {
    

        std::string text;
        std::string readError;

        if (!FileSystem::ReadTextFile(virtualPath, text, readError)) {
        
        outError =
                "no settings file at '" + std::string(virtualPath) + "'; using built-in defaults";
            ENGINE_LOG_WARN(Channels::kConfig, "{}", outError);
            return true;
        }

        std::string parseError;
        Json document = ParseJson(text, parseError);
        if (!parseError.empty()){
            outError = std::string(virtualPath) + ": " + parseError;
            ENGINE_LOG_ERROR(Channels::kConfig, "{}", outError);
            return false;
        }

        const Json& window = Section(document, "window");
        outConfig.windowWidth = ReadInt(window, "width", outConfig.windowWidth, "window");
        outConfig.windowHeight = ReadInt(window, "height", outConfig.windowHeight, "window");
        outConfig.windowTitle = ReadString(window, "title", outConfig.windowTitle, "window");

        const Json& logging = Section(document, "logging");
        outConfig.logFile = ReadString(logging, "file", outConfig.logFile, "logging");

        const std::string thresholdText =
            ReadString(logging, "threshold", ToString(outConfig.logThreshold), "logging");

        if (!ParseLogLevel(thresholdText, outConfig.logThreshold)) {
            ENGINE_LOG_WARN(
                Channels::kConfig,
                "logging threshold is '{}', which is not one of Info, Warning, or Error; using {}",
                thresholdText, ToString(outConfig.logThreshold));
        }

        const Json& tunables = Section(document, "tunables");
        outConfig.logBufferCapacity =
            ReadInt(tunables, "logBufferCapacity", outConfig.logBufferCapacity, "tunables");
        outConfig.gizmoCircleSegments =
            ReadInt(tunables, "gizmoCircleSegments", outConfig.gizmoCircleSegments, "tunables");
        outConfig.fixedTimestepSeconds =
            ReadFloat(tunables, "fixedTimestepSeconds", outConfig.fixedTimestepSeconds, "tunables");
        outConfig.maxStepsPerFrame =
            ReadInt(tunables, "maxStepsPerFrame", outConfig.maxStepsPerFrame, "tunables");

        outConfig.startupScene =
            ReadString(Section(document, "startup"), "scene", outConfig.startupScene, "startup");

        outDocument = std::move(document);

        outError.clear();
        ENGINE_LOG_INFO(Channels::kConfig, "settings loaded from '{}'", virtualPath);
        return true;
    }

} // namespace eng
