// =============================================================================
//  FileSystem.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. FileSystem.h is the specification; read it first.
// =============================================================================

#include <engine/fs/FileSystem.h>
#include <engine/core/Log.h>

#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>


namespace eng {
namespace {

namespace fs = std::filesystem;

std::string g_root;

bool LooksLikeRoot(const fs::path& directory) {
    std::error_code ec;
    return fs::is_directory(directory / "assets", ec);
}

} // namespace

// Works out where the project is by starting at the program's own location and
// walking up until it finds a folder containing assets/. Where it settled is
// written to the log, because that line is the first thing to check when a file
// will not load on somebody else's machine.
bool FileSystem::Init(const BootConfig& config) {

    if (const char* overridePath = std::getenv("ENGINE_ASSET_ROOT");
        overridePath != nullptr && overridePath[0] != '\0') {
        std::error_code ec;
        const fs::path candidate = fs::absolute(fs::path(overridePath), ec);
        if (LooksLikeRoot(candidate)) {
            g_root = candidate.string();
            ENGINE_LOG_INFO(Channels::kFileSys, "Asset folder taken from ENGINE_ASSET_ROOT: '{}'",
                            g_root);
            return true;
        }
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "ENGINE_ASSET_ROOT is set to '{}' but there is no 'assets' folder  there; "
                        "searching instead",
                        overridePath);
    }

    fs::path start;

    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        start = fs::path(base);
    } else {
        std::error_code ec;
        start = fs::current_path(ec);
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "Could not find the programs's own folder; falling back to the current "
                        "directory, which is not reliable.");
    }

    std::error_code ec;
    fs::path current = fs::absolute(start, ec);

    for (int depth = 0; depth < 12; depth++) {
        if (LooksLikeRoot(current)) {
            g_root = current.string();
            ENGINE_LOG_INFO(Channels::kFileSys, "Asset found in: '{}'", g_root);
            return true;
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }

    ENGINE_LOG_ERROR(Channels::kFileSys,
                     "Could not find an 'assets' folder above '{}'. Nothing will load.",
                     start.string());
    g_root = start.string();
    return false;
}

// Forgets the project location.
void FileSystem::Shutdown() {
    g_root.clear();
    ENGINE_LOG_INFO(Channels::kFileSys, "File system shut down.");
}

// The folder that was found - the one containing assets/.
const std::string& FileSystem::AssetRoot() {
    return g_root;
}

// Turns a short name like "textures/player.bmp" into a real path on this
// machine. Never fails; whether the file exists is a separate question.
std::string FileSystem::Resolve(std::string_view virtualPath) {
    fs::path path = fs::path(g_root) / "assets" / fs::path(std::string(virtualPath));

    if (virtualPath.starts_with("config/") || virtualPath.starts_with("logs/") ||
        virtualPath.starts_with(".build/") || virtualPath == ".build") {
        path = fs::path(g_root) / fs::path(std::string(virtualPath));
    }

    return path.lexically_normal().string();
}

// Is there actually a file there?
bool FileSystem::Exists(std::string_view virtualPath) {
    std::error_code ec;
    return fs::exists(Resolve(virtualPath), ec);
}

// Lists the files in one folder, optionally filtered by extension, giving back
// short names that can be handed straight back to ReadTextFile or Scene::Load.
bool FileSystem::ListFiles(std::string_view virtualDirectory,
                           std::string_view extension,
                           std::vector<std::string>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    if (!fs::is_directory(real, ec)) {
        ENGINE_LOG_WARN(Channels::kFileSys, "'{}' is not a folder (looked in  '{}')", virtualDirectory, real);
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
        out.push_back(prefix+name);
    }

    std::sort(out.begin(), out.end());

    return true;
}

// The listing a file BROWSER needs rather than a menu: sub-folders included,
// folders first, each group sorted by name.
bool FileSystem::ListDirectory(std::string_view virtualDirectory,
                               std::vector<DirEntry>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    if (!fs::is_directory(real, ec)) {
        return false;
    }

    std::string prefix(virtualDirectory);
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }

    for (const auto& entry : fs::directory_iterator(real, ec)) {
        DirEntry item;
        item.name = entry.path().filename().string();

        if (item.name.empty() || item.name.front() == '.') {
            continue;
        }

        item.isDirectory = entry.is_directory(ec);
        if (!item.isDirectory && !entry.is_regular_file(ec)) {
            continue;
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

    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.name < b.name;
    });

    return true;
}

// Creates a folder, including any missing parent folders.
bool FileSystem::CreateDirectory(std::string_view virtualDirectory, std::string& outError) {
    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    fs::create_directories(real, ec);

    if (ec) {
        outError = "cannot create '" + std::string(virtualDirectory) + "': " + ec.message();
        return false;
    }
    outError.clear();
    return true;
}

// Reads a whole text file - a scene, the settings - into a string.
bool FileSystem::ReadTextFile(std::string_view virtualPath, std::string& outText,
                              std::string& outError) {
    const std::string real = Resolve(virtualPath);
    
    std::ifstream file(real);
    if (!file) {
        outError = "cannot open '" + std::string(virtualPath) + "' (looked in '" + real + "')";
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    outText = contents.str();

    outError.clear();
    return true;
}

// Reads a whole binary file - an image - into a list of bytes.
bool FileSystem::ReadFile(std::string_view virtualPath,
                          std::vector<unsigned char>& outBytes,
                          std::string& outError) {
    const std::string real = Resolve(virtualPath);

    std::ifstream file(real, std::ios::binary | std::ios::ate);
    if (!file) {
        outError = "cannot open '" + std::string(virtualPath) + "' (looked in '" + real + "')";
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        outError = "cannot measure size of file: '" + real + "'";
        return false;
    }
    file.seekg(0, std::ios::beg);

    outBytes.resize(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size)){
        outError = "'" + real + "' ended sooner than expected";
        outBytes.clear();
        return false;
    }

    outError.clear();
    return true;
}

// Writes a text file, creating any folders it needs on the way.
bool FileSystem::WriteTextFile(std::string_view virtualPath, std::string_view text,
                               std::string& outError) {
    const std::string real = Resolve(virtualPath);
    
    std::error_code ec;
    fs::create_directories(fs::path(real).parent_path(), ec);

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
