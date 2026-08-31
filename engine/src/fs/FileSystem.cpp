// WEEK 9 - the file system layer. See FileSystem.h, especially the
// which-thread-am-I-on block.

#include <engine/concurrency/JobSystem.h>
#include <engine/concurrency/ThreadSafeQueue.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace eng {
namespace {

namespace fs = std::filesystem;

std::string g_root;

// One completed read, waiting for the main thread to fire its callback.
struct CompletedRead {
    FileSystem::ReadCallback callback;
    std::vector<u8>          bytes;
    std::string              error;
    bool                     success = false;
};

ThreadSafeQueue<CompletedRead> g_completions;
std::atomic<usize>             g_pending{0};

// The marker that identifies the repository root. `assets` is the natural
// choice: it is the thing being looked for, it is committed, and it exists in
// every clone. A dedicated sentinel file would also work and would be more
// explicit; this is one fewer file to forget to commit.
bool LooksLikeRoot(const fs::path& directory) {
    std::error_code ec;
    return fs::is_directory(directory / "assets", ec);
}

} // namespace

bool FileSystem::Init() {
    // AN EXPLICIT OVERRIDE, CHECKED FIRST.
    //
    // The walk-upward search below assumes the executable lives inside the
    // repository, which is true for `build/<preset>/bin` and false for every
    // other arrangement: a build directory outside the source tree, a CI
    // runner, a packaged game whose assets ship in a different place, or a
    // binary someone copied somewhere to try it.
    //
    // ENGINE_ASSET_ROOT names the directory CONTAINING `assets/`. It is
    // checked before the search rather than as a fallback, because an override
    // that only applies when the guess fails is an override you cannot rely on.
    // Logged loudly, since "why is it loading those assets" is otherwise a very
    // confusing half hour.
    if (const char* overridePath = std::getenv("ENGINE_ASSET_ROOT");
        overridePath != nullptr && overridePath[0] != '\0') {
        std::error_code overrideEc;
        const fs::path candidate = fs::absolute(fs::path(overridePath), overrideEc);
        if (LooksLikeRoot(candidate)) {
            g_root = candidate.string();
            ENGINE_LOG_INFO(Channels::kFileSys,
                            "asset root taken from ENGINE_ASSET_ROOT: '{}'", g_root);
            return true;
        }
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "ENGINE_ASSET_ROOT is set to '{}' but there is no 'assets' "
                        "directory there; falling back to the search",
                        overridePath);
    }

    // Start from the EXECUTABLE'S OWN LOCATION, not the working directory.
    // SDL_GetBasePath is the portable way to ask, and it works before
    // SDL_Init - which matters, because the file system comes up before the
    // window does.
    fs::path start;
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        start = fs::path(base);
    } else {
        // Fallback rather than failure. A missing base path is rare and the
        // working directory is usually right; logging the difference means a
        // machine where it is wrong says so instead of silently not finding
        // assets.
        std::error_code ec;
        start = fs::current_path(ec);
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "SDL_GetBasePath unavailable; falling back to the working "
                        "directory, which is not reliable across launch methods");
    }

    // Walk upward. From build/debug/bin that is three levels, but counting
    // levels would hardcode the build layout - which is precisely the kind of
    // assumption this whole class exists to remove.
    std::error_code ec;
    fs::path current = fs::absolute(start, ec);
    for (int depth = 0; depth < 12; ++depth) {
        if (LooksLikeRoot(current)) {
            g_root = current.string();
            ENGINE_LOG_INFO(Channels::kFileSys, "asset root resolved to '{}'", g_root);
            ENGINE_LOG_INFO(Channels::kFileSys, "(search started at '{}')", start.string());
            return true;
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }

    ENGINE_LOG_ERROR(Channels::kFileSys,
                     "could not find an 'assets' directory above '{}'. Assets will not "
                     "load. Is this a fresh clone with assets/ committed?",
                     start.string());
    g_root = start.string();
    return false;
}

void FileSystem::Shutdown() {
    // Anything still in flight is dropped deliberately, and loudly. Draining
    // instead would mean waiting on worker threads that are being torn down.
    const usize pending = g_pending.load();
    if (pending > 0) {
        ENGINE_LOG_WARN(Channels::kFileSys, "{} async read(s) still in flight at shutdown",
                        pending);
    }
    g_completions.Stop();
    g_root.clear();
    ENGINE_LOG_INFO(Channels::kFileSys, "FileSystem down");
}

const std::string& FileSystem::AssetRoot() {
    return g_root;
}

std::string FileSystem::Resolve(std::string_view virtualPath) {
    fs::path path(g_root);
    path /= "assets";
    path /= fs::path(std::string(virtualPath));

    // config/ lives beside assets/ rather than inside it, because a config
    // file is not an asset - it is not shipped in a package and it is edited
    // by hand. A virtual path starting with "config/" resolves against the
    // repository root instead.
    //
    // gamescripts/ is on that list for a stronger reason: it holds C++ SOURCE that
    // the build compiles. Source is not shipped in an asset package and must
    // not be, so putting it under assets/ would be a lie about what it is - and
    // the day someone writes a packaging step, the mistake would ship the
    // game's source with the game.
    if (virtualPath.starts_with("config/") || virtualPath.starts_with("logs/") ||
        virtualPath.starts_with("gamescripts/") || virtualPath == "gamescripts") {
        path = fs::path(g_root) / fs::path(std::string(virtualPath));
    }
    return path.lexically_normal().string();
}

bool FileSystem::Exists(std::string_view virtualPath) {
    std::error_code ec;
    return fs::exists(Resolve(virtualPath), ec);
}

bool FileSystem::ListFiles(std::string_view virtualDirectory, std::string_view extension,
                           std::vector<std::string>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    if (!fs::is_directory(real, ec)) {
        ENGINE_LOG_WARN(Channels::kFileSys, "'{}' is not a directory (resolved to '{}')",
                        virtualDirectory, real);
        return false;
    }

    std::string prefix(virtualDirectory);
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(real, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!extension.empty() && !name.ends_with(extension)) {
            continue;
        }
        // Returned as a VIRTUAL path, not a real one. A caller that got a real
        // path back would have to know it must not feed it to ReadFile, which
        // resolves again - and that mistake produces a doubled-up path that is
        // deeply confusing to read.
        out.push_back(prefix + name);
    }

    // Sorted, so a menu built from this does not shuffle between runs.
    // directory_iterator's order is whatever the filesystem hands back.
    std::sort(out.begin(), out.end());
    return true;
}

bool FileSystem::ListDirectory(std::string_view virtualDirectory,
                               std::vector<DirEntry>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    if (!fs::is_directory(real, ec)) {
        // NOT a warning. A browser asks about directories that may legitimately
        // not exist yet - `gamescripts/` before the first script is created - and
        // logging every one of those would fill the log with non-problems.
        return false;
    }

    std::string prefix(virtualDirectory);
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(real, ec)) {
        DirEntry item;
        item.name = entry.path().filename().string();

        // Hidden and editor-internal entries are skipped. A leading dot is the
        // convention on every platform this builds for, and showing `.git` in
        // an asset browser has never once been useful.
        if (item.name.empty() || item.name.front() == '.') {
            continue;
        }

        item.isDirectory = entry.is_directory(ec);
        if (!item.isDirectory && !entry.is_regular_file(ec)) {
            continue;   // sockets, devices, broken symlinks
        }

        item.virtualPath = prefix + item.name;
        if (!item.isDirectory) {
            item.byteSize = static_cast<u64>(entry.file_size(ec));
            if (ec) {
                item.byteSize = 0;
                ec.clear();
            }
        }
        out.push_back(std::move(item));
    }

    // Directories first, then files, each group by name - what every file
    // manager does, and what makes a folder findable without reading the icons.
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.name < b.name;
    });
    return true;
}

bool FileSystem::CreateDirectory(std::string_view virtualDirectory, std::string& outError) {
    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    fs::create_directories(real, ec);

    // create_directories reports FALSE with no error when the directory already
    // existed, which is a success for this function's purpose - so the error
    // code is what is checked, not the return value.
    if (ec) {
        outError = "cannot create '" + std::string(virtualDirectory) + "' (resolved to '" +
                   real + "'): " + ec.message();
        return false;
    }
    outError.clear();
    return true;
}

bool FileSystem::ReadFile(std::string_view virtualPath, std::vector<u8>& outBytes,
                          std::string& outError) {
    const std::string real = Resolve(virtualPath);

    std::ifstream file(real, std::ios::binary | std::ios::ate);
    if (!file) {
        outError = "cannot open '" + std::string(virtualPath) + "' (resolved to '" + real +
                   "')";
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        outError = "cannot determine the size of '" + real + "'";
        return false;
    }
    file.seekg(0, std::ios::beg);

    outBytes.resize(static_cast<usize>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size)) {
        outError = "short read on '" + real + "'";
        outBytes.clear();
        return false;
    }

    outError.clear();
    return true;
}

bool FileSystem::WriteFile(std::string_view virtualPath, const void* data, usize bytes,
                           std::string& outError) {
    const std::string real = Resolve(virtualPath);

    std::error_code ec;
    fs::create_directories(fs::path(real).parent_path(), ec);

    std::ofstream file(real, std::ios::binary | std::ios::trunc);
    if (!file) {
        outError = "cannot open '" + real + "' for writing";
        return false;
    }
    if (bytes > 0) {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    }
    if (!file) {
        outError = "write failed on '" + real + "'";
        return false;
    }
    outError.clear();
    return true;
}

void FileSystem::ReadFileAsync(std::string_view virtualPath, ReadCallback onComplete) {
    // The path is copied into the job. Capturing the string_view would dangle
    // the moment the caller's string went out of scope, and it would dangle on
    // ANOTHER THREAD, which is the least debuggable version of that bug.
    std::string path(virtualPath);
    g_pending.fetch_add(1, std::memory_order_relaxed);

    JobSystem::Enqueue([path = std::move(path), callback = std::move(onComplete)]() mutable {
        // *** WORKER THREAD from here to the Push below. ***
        // Nothing in this lambda touches the renderer, the resource tables, or
        // anything else the main thread owns. It reads bytes and posts them.
        CompletedRead result;
        result.callback = std::move(callback);
        result.success  = ReadFile(path, result.bytes, result.error);
        g_completions.Push(std::move(result));
    });
}

void FileSystem::PumpCompletions() {
    // *** MAIN THREAD. *** Every callback fires from here.
    while (auto completed = g_completions.TryPop()) {
        if (completed->callback) {
            completed->callback(completed->success, std::move(completed->bytes),
                                completed->error);
        }
        g_pending.fetch_sub(1, std::memory_order_relaxed);
    }
}

usize FileSystem::PendingReadCount() {
    return g_pending.load(std::memory_order_relaxed);
}

} // namespace eng
