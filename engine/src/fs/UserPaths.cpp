// ============================================================================
//  UserPaths.cpp - see UserPaths.h for why this is separate from FileSystem.
// ============================================================================

#include <engine/fs/UserPaths.h>

#include <engine/core/Log.h>

#include <SDL3/SDL.h>

#include <filesystem>

namespace eng {
namespace {

// SDL_GetPrefPath creates the folder and hands back a string IT owns, so the
// result is copied and the original freed.
//
// The two names become %APPDATA%/Engine2D/Engine2D on Windows. Both are the
// same on purpose: SDL's first argument is the organisation and the second
// the application, and there is one of each here.
std::string AskTheOperatingSystem() {
    char* path = SDL_GetPrefPath("Engine2D", "Engine2D");
    if (path == nullptr) {
        // Unusual, not impossible: a read-only or sandboxed home directory.
        // Reported once, and callers cope - a launcher with no list is less
        // bad than one that will not start.
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "there is nowhere to keep your settings ({}), so things like the "
                        "recent-projects list will not be remembered between runs",
                        SDL_GetError());
        return {};
    }

    std::string copied = path;
    SDL_free(path);
    return copied;
}

} // namespace

const std::string& UserPaths::Dir() {
    // READ ONCE. A function-local static, so a process that never asks pays
    // nothing and there is no initialisation-order question about a global
    // that touches the file system.
    static const std::string dir = AskTheOperatingSystem();
    return dir;
}

std::string UserPaths::File(std::string_view name) {
    const std::string& dir = Dir();
    if (dir.empty() || name.empty()) {
        return {};
    }

    // JOINED, NOT RESOLVED. filename() throws away any directory part, so a
    // name containing "../" cannot reach outside the settings folder. This is
    // one of the few places in the engine that writes outside a project, and
    // it does not also get to write anywhere.
    const std::filesystem::path leaf = std::filesystem::path(std::string(name)).filename();
    if (leaf.empty()) {
        return {};
    }
    return (std::filesystem::path(dir) / leaf).generic_string();
}

} // namespace eng
