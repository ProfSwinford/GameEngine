// =============================================================================
//  Config.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Config.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/core/Config.h>

namespace eng {

// Reads config/engine.json into a BootConfig - window size, log level, fixed
// timestep, starting scene. Also hands back the parsed document, because the
// input bindings are read out of it later. Returns false if the file is
// missing or malformed, which is the one thing that stops the engine starting.
bool LoadBootConfig(std::string_view /*virtualPath*/, BootConfig& /*outConfig*/,
                    Json& /*outDocument*/, std::string& /*outError*/) {
    return false;
}

} // namespace eng
