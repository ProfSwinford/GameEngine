// ============================================================================
//  Relaunch.cpp - see Relaunch.h for why the timing is the whole point.
// ============================================================================

#include "Relaunch.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace editor {

std::string ExecutableDirectory() {
    // SDL_GetBasePath hands back a string IT owns and must not be freed, so
    // the result is copied. It ends with a separator, which is removed here
    // so that callers can join with / and get one separator rather than two.
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        fs::path path(base);
        if (path.filename().empty() && path.has_parent_path()) {
            path = path.parent_path();
        }
        return path.generic_string();
    }
    return {};
}

namespace {

// The launcher's executable, beside this one.
std::filesystem::path HubPath() {
    const std::string directory = ExecutableDirectory();
    if (directory.empty()) {
        return {};
    }
#if defined(_WIN32)
    return fs::path(directory) / "hub.exe";
#else
    return fs::path(directory) / "hub";
#endif
}

} // namespace

std::string LauncherPath() {
    return HubPath().generic_string();
}

bool HubIsInstalled() {
    const fs::path hub = HubPath();
    if (hub.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::exists(hub, ec);
}

bool RelaunchHub(std::string& outError, const HubRequest& request) {
    const fs::path hub = HubPath();

    std::error_code ec;
    if (hub.empty() || !fs::exists(hub, ec)) {
        outError = "there is no launcher installed beside the editor";
        return false;
    }

    const std::string hubPath = hub.string();

    // BOTH STRINGS LIVE UNTIL AFTER SDL_CreateProcess. The vector holds
    // pointers into them, so a temporary - std::to_string(...).c_str() written
    // inline - would be freed before SDL ever read it.
    const std::string frames = std::to_string(request.frameLimit);

    // BUILT UP rather than written out, because the two options are
    // independent of each other and the array would otherwise need one spelled
    // out per combination.
    std::vector<const char*> args;
    args.push_back(hubPath.c_str());
    if (request.becauseNoProject) {
        args.push_back("--from-editor");
    }
    if (request.frameLimit > 0) {
        args.push_back("--frames");
        args.push_back(frames.c_str());
    }

    // NULL-TERMINATED, which SDL requires and which is easy to leave out
    // because nothing complains until it reads past the end.
    args.push_back(nullptr);

    SDL_Process* child = SDL_CreateProcess(args.data(), false);
    if (child == nullptr) {
        const char* reason = SDL_GetError();
        outError           = (reason != nullptr && reason[0] != '\0')
                                 ? reason
                                 : "the operating system did not say why";
        return false;
    }

    if (request.waitForIt) {
        // THE RETURN VALUE IS NOT DECORATION. SDL_WaitProcess returns true
        // only if the process EXITED, and SDL_process.h is explicit that the
        // exit code "will not be changed if the process is still running" -
        // so ignoring the bool and reading exitCode would report a tidy zero
        // for a wait that never happened, with the launcher still alive. That
        // is precisely the orphan this branch exists to prevent, reported as
        // success.
        int        exitCode = 0;
        const bool exited   = SDL_WaitProcess(child, true, &exitCode);

        // DESTROYED EITHER WAY: SDL_process.h says to call this "whether the
        // process was killed, terminated on its own, or is still running".
        SDL_DestroyProcess(child);

        if (!exited) {
            const char* reason = SDL_GetError();
            outError = std::string("the launcher was started but could not be waited "
                                   "for, so it may still be running: ") +
                       ((reason != nullptr && reason[0] != '\0')
                            ? reason
                            : "the operating system did not say why");
            return false;
        }

        // REPORTED AS A FAILURE OF OURS, because the only reason to wait is
        // that somebody is reading the exit code. A launcher that could not
        // open a window must not look like a successful hand-off.
        if (exitCode != 0) {
            outError = "the launcher exited with code " + std::to_string(exitCode);
            return false;
        }
        return true;
    }

    SDL_DestroyProcess(child);
    return true;
}

bool Relaunch(const std::string& projectRoot, std::string& outError) {
    const std::string directory = ExecutableDirectory();
    if (directory.empty()) {
        outError = "cannot work out where the editor is installed";
        return false;
    }

    // THE SAME EXECUTABLE, by name rather than by argv[0].
    //
    // argv[0] is whatever the caller typed and need not be a usable path at
    // all; SDL_GetBasePath is the only portable answer to "where am I?". The
    // extension is spelled out per platform because there is no portable way
    // to ask.
#if defined(_WIN32)
    const fs::path exe = fs::path(directory) / "editor.exe";
#else
    const fs::path exe = fs::path(directory) / "editor";
#endif

    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        outError = "'" + exe.string() + "' is not there";
        return false;
    }

    const std::string exePath = exe.string();

    // NULL-TERMINATED, which SDL requires and which is easy to leave out
    // because nothing complains until it reads past the end.
    const char* args[] = {exePath.c_str(), "--project", projectRoot.c_str(), nullptr};

    // false: the child keeps this process's stdout and stderr rather than
    // having them piped. That matters when the editor was started from a
    // terminal - the new one should print where the old one did, instead of
    // into a pipe nobody reads.
    SDL_Process* child = SDL_CreateProcess(args, false);
    if (child == nullptr) {
        const char* reason = SDL_GetError();
        outError = (reason != nullptr && reason[0] != '\0')
                       ? reason
                       : "the operating system did not say why";
        return false;
    }

    // RELEASED IMMEDIATELY, NOT WAITED FOR. This drops our handle on the
    // child without killing it - the new editor must outlive the one that
    // started it, and waiting here would leave a dead editor's process alive
    // purely to hold a handle.
    SDL_DestroyProcess(child);
    return true;
}

} // namespace editor
