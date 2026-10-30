// ============================================================================
//  EngineBuild.cpp - reading engine-version.json. See EngineBuild.h for why
//  it is a file rather than a compiled-in constant.
// ============================================================================

#include <engine/project/EngineBuild.h>

#include <engine/core/Json.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace eng {
namespace {

// Where this executable is. The same SDL call FileSystem uses to begin its
// upward search, for the same reason: it is the only portable answer, and
// argv[0] is not one.
fs::path ExecutableDirectory() {
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        return fs::path(base);
    }
    return {};
}

} // namespace

EngineBuildInfo EngineBuild::ReadFrom(std::string_view realDirectory) {
    EngineBuildInfo info;
    info.id = "unknown";

    if (realDirectory.empty()) {
        return info;
    }

    const fs::path  file = fs::path(realDirectory) / kFileName;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        // No file. Not an error - a hand-built engine, or one from before
        // this existed. See the header.
        return info;
    }

    std::ifstream      in(file, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();

    std::string parseError;
    const Json  document = ParseJson(all.str(), parseError);
    if (!parseError.empty() || !document.is_object()) {
        // Deliberately silent rather than warning. This is called by a
        // launcher once per installed engine, and a folder with a damaged
        // file should show as "unknown" in a list rather than produce a
        // warning nobody asked for. The editor's own build reports through
        // the stamp instead, where a mismatch has a visible consequence.
        return info;
    }

    info.version       = ReadString(document, "version", "", kFileName);
    info.configuration = ReadString(document, "configuration", "", kFileName);
    info.scriptAbi     = ReadInt(document, "scriptAbi", 0, kFileName);
    info.known         = !info.version.empty();

    if (info.known) {
        info.id = info.configuration.empty() ? info.version
                                             : info.version + " " + info.configuration;
    }
    return info;
}

const EngineBuildInfo& EngineBuild::Running() {
    // READ ONCE. A function-local static so there is no start-up cost for a
    // process that never asks, and no order-of-initialisation question about
    // a global that touches the filesystem.
    static const EngineBuildInfo info =
        ReadFrom(ExecutableDirectory().generic_string());
    return info;
}

} // namespace eng
