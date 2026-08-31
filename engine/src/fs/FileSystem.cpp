// ============================================================================
//  FileSystem.cpp - locating and reading the game's files. See FileSystem.h.
//
//  Two standard libraries do most of the work here:
//
//    <filesystem>  the C++17 library for paths, folders and directory
//                  listings. Using it means this file is the same on Windows,
//                  macOS and Linux instead of needing a separate version per
//                  platform.
//
//    <fstream>     the standard file streams, for actually reading and writing.
//
//  SDL appears exactly once, for SDL_GetBasePath, which answers "where is the
//  running program itself?" - a question the standard library cannot answer.
// ============================================================================

#if defined(_MSC_VER)
// Microsoft's compiler warns that std::getenv is "unsafe" and suggests a
// Microsoft-only replacement for it. std::getenv is standard C++ and is used
// correctly below - its result is read immediately and never kept - so the
// warning is switched off for this one file rather than making the code work
// on only one compiler.
#pragma warning(disable : 4996)
#endif

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eng {
namespace {

// A short alias so the code below is not three-quarters namespace.
namespace fs = std::filesystem;

std::string g_root;

// How the project folder is recognised: it is the one containing `assets`.
// That folder is committed and exists in every copy of the project, so there
// is no extra marker file for anyone to forget.
bool LooksLikeRoot(const fs::path& directory) {
    // The std::error_code overloads are used throughout this file. They report
    // problems by setting a code instead of throwing an exception, which suits
    // questions like "does this exist?" where a missing folder is a perfectly
    // normal answer rather than an emergency.
    std::error_code ec;
    return fs::is_directory(directory / "assets", ec);
}

} // namespace

bool FileSystem::Init() {
    // An explicit override, checked FIRST.
    //
    // The upward search below assumes the program is running from inside the
    // project folder, which is true for build/<preset>/bin and false for a
    // build folder somewhere else or a copied executable. Setting the
    // ENGINE_ASSET_ROOT environment variable to the folder CONTAINING assets/
    // skips the guessing entirely.
    if (const char* overridePath = std::getenv("ENGINE_ASSET_ROOT");
        overridePath != nullptr && overridePath[0] != '\0') {
        std::error_code ec;
        const fs::path candidate = fs::absolute(fs::path(overridePath), ec);
        if (LooksLikeRoot(candidate)) {
            g_root = candidate.string();
            ENGINE_LOG_INFO(Channels::kFileSys,
                            "asset folder taken from ENGINE_ASSET_ROOT: '{}'", g_root);
            return true;
        }
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "ENGINE_ASSET_ROOT is set to '{}' but there is no 'assets' folder "
                        "there; searching instead", overridePath);
    }

    // Start from the executable's own folder.
    fs::path start;
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        start = fs::path(base);
    } else {
        std::error_code ec;
        start = fs::current_path(ec);
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "could not find the program's own folder; falling back to the "
                        "current directory, which is not reliable");
    }

    // Walk upward looking for the project folder.
    //
    // From build/debug/bin that happens to be three levels up, but counting
    // levels would bake the build layout into the code - exactly the kind of
    // assumption this class exists to get rid of. The loop limit of 12 is
    // simply a stop so a bad start location cannot walk forever.
    std::error_code ec;
    fs::path current = fs::absolute(start, ec);
    for (int depth = 0; depth < 12; ++depth) {
        if (LooksLikeRoot(current)) {
            g_root = current.string();
            ENGINE_LOG_INFO(Channels::kFileSys, "assets found in '{}'", g_root);
            return true;
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;   // reached the top of the drive
        }
        current = current.parent_path();
    }

    ENGINE_LOG_ERROR(Channels::kFileSys,
                     "could not find an 'assets' folder above '{}'. Nothing will load.",
                     start.string());
    g_root = start.string();
    return false;
}

void FileSystem::Shutdown() {
    g_root.clear();
    ENGINE_LOG_INFO(Channels::kFileSys, "file system shut down");
}

const std::string& FileSystem::AssetRoot() {
    return g_root;
}

std::string FileSystem::Resolve(std::string_view virtualPath) {
    // The normal case: everything lives under assets/.
    fs::path path = fs::path(g_root) / "assets" / fs::path(std::string(virtualPath));

    // Three exceptions, which live beside assets/ rather than inside it:
    //
    //   config/       settings edited by hand; not something the game ships
    //   logs/         output the program writes
    //   gamescripts/  C++ SOURCE that the build compiles
    //
    // gamescripts/ is the important one. Source code is not an asset and must
    // never be packaged as one - the day somebody writes a packaging step,
    // putting scripts under assets/ would ship the game's own source with it.
    if (virtualPath.starts_with("config/") || virtualPath.starts_with("logs/") ||
        virtualPath.starts_with("gamescripts/") || virtualPath == "gamescripts") {
        path = fs::path(g_root) / fs::path(std::string(virtualPath));
    }

    // lexically_normal tidies up things like "a/./b" and "a/../b".
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
        ENGINE_LOG_WARN(Channels::kFileSys, "'{}' is not a folder (looked in '{}')",
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
        // VIRTUAL paths are returned, not real ones. A caller handed a real
        // path would have to know not to pass it back to ReadFile, which would
        // resolve it a second time and produce a doubled-up path.
        out.push_back(prefix + name);
    }

    // The order a folder listing comes back in is up to the operating system.
    // Sorting makes a menu built from this stable between runs.
    std::sort(out.begin(), out.end());
    return true;
}

bool FileSystem::ListDirectory(std::string_view virtualDirectory,
                               std::vector<DirEntry>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    if (!fs::is_directory(real, ec)) {
        // Deliberately NOT a warning. A file browser asks about folders that
        // may legitimately not exist yet - gamescripts/ before the first
        // script is written - and logging each of those would fill the Console
        // with non-problems.
        return false;
    }

    std::string prefix(virtualDirectory);
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(real, ec)) {
        DirEntry item;
        item.name = entry.path().filename().string();

        // Skip hidden entries. A leading dot is the convention on every
        // platform, and showing ".git" in an asset browser has never helped.
        if (item.name.empty() || item.name.front() == '.') {
            continue;
        }

        item.isDirectory = entry.is_directory(ec);
        if (!item.isDirectory && !entry.is_regular_file(ec)) {
            continue;   // skip anything that is neither, e.g. a broken shortcut
        }

        item.virtualPath = prefix + item.name;
        if (!item.isDirectory) {
            item.byteSize = static_cast<unsigned long long>(entry.file_size(ec));
            if (ec) {
                item.byteSize = 0;
                ec.clear();
            }
        }
        out.push_back(std::move(item));
    }

    // Folders first, then files, each group alphabetical.
    //
    // The [](...) is a LAMBDA - a small unnamed function written where it is
    // used. std::sort calls it to ask "should a come before b?".
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

    // create_directories returns false when the folder already existed, which
    // is a success as far as this function is concerned. So the error code is
    // what gets checked, not the return value.
    if (ec) {
        outError = "cannot create '" + std::string(virtualDirectory) + "': " + ec.message();
        return false;
    }
    outError.clear();
    return true;
}

bool FileSystem::ReadTextFile(std::string_view virtualPath, std::string& outText,
                              std::string& outError) {
    const std::string real = Resolve(virtualPath);

    std::ifstream file(real);
    if (!file) {
        outError = "cannot open '" + std::string(virtualPath) + "' (looked in '" + real + "')";
        return false;
    }

    // Reading a whole file into a string: point a std::stringstream at the
    // file's contents and take the string back out. This is the standard
    // one-liner for the job and avoids any manual size/allocate/read dance.
    std::ostringstream contents;
    contents << file.rdbuf();
    outText = contents.str();

    outError.clear();
    return true;
}

bool FileSystem::ReadFile(std::string_view virtualPath, std::vector<unsigned char>& outBytes,
                          std::string& outError) {
    const std::string real = Resolve(virtualPath);

    // std::ios::binary means "do not translate anything" - essential for an
    // image, where a byte that happens to look like a line ending must not be
    // rewritten. std::ios::ate means "start positioned at the end", which is
    // how the size is found below.
    std::ifstream file(real, std::ios::binary | std::ios::ate);
    if (!file) {
        outError = "cannot open '" + std::string(virtualPath) + "' (looked in '" + real + "')";
        return false;
    }

    const std::streamsize size = file.tellg();   // we are at the end, so this is the length
    if (size < 0) {
        outError = "cannot measure '" + real + "'";
        return false;
    }
    file.seekg(0, std::ios::beg);                // back to the start to read

    outBytes.resize(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size)) {
        outError = "'" + real + "' ended sooner than expected";
        outBytes.clear();
        return false;
    }

    outError.clear();
    return true;
}

bool FileSystem::WriteTextFile(std::string_view virtualPath, std::string_view text,
                               std::string& outError) {
    const std::string real = Resolve(virtualPath);

    // Make sure the folder exists before trying to write into it, so a missing
    // folder reports as "cannot create folder" rather than the much less
    // helpful "cannot open file".
    std::error_code ec;
    fs::create_directories(fs::path(real).parent_path(), ec);

    // std::ios::trunc empties any existing file first, so saving twice does
    // not leave the tail of a longer previous version behind.
    std::ofstream file(real, std::ios::trunc);
    if (!file) {
        outError = "cannot open '" + real + "' for writing";
        return false;
    }

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        outError = "writing to '" + real + "' failed";
        return false;
    }

    outError.clear();
    return true;
}

} // namespace eng
