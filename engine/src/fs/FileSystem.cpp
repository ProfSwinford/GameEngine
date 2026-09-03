// =============================================================================
//  FileSystem.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. FileSystem.h is the specification; read it first.
// =============================================================================

#include <engine/fs/FileSystem.h>

namespace eng {

// Works out where the project is by starting at the program's own location and
// walking up until it finds a folder containing assets/. Where it settled is
// written to the log, because that line is the first thing to check when a file
// will not load on somebody else's machine.
bool FileSystem::Init() {
    return false;
}

// Forgets the project location.
void FileSystem::Shutdown() {
}

// The folder that was found - the one containing assets/.
const std::string& FileSystem::AssetRoot() {
    static const std::string root;
    return root;
}

// Turns a short name like "textures/player.bmp" into a real path on this
// machine. Never fails; whether the file exists is a separate question.
std::string FileSystem::Resolve(std::string_view /*virtualPath*/) {
    return {};
}

// Is there actually a file there?
bool FileSystem::Exists(std::string_view /*virtualPath*/) {
    return false;
}

// Lists the files in one folder, optionally filtered by extension, giving back
// short names that can be handed straight back to ReadTextFile or Scene::Load.
bool FileSystem::ListFiles(std::string_view /*virtualDirectory*/,
                           std::string_view /*extension*/,
                           std::vector<std::string>& /*out*/) {
    return false;
}

// The listing a file BROWSER needs rather than a menu: sub-folders included,
// folders first, each group sorted by name.
bool FileSystem::ListDirectory(std::string_view /*virtualDirectory*/,
                               std::vector<DirEntry>& /*out*/) {
    return false;
}

// Creates a folder, including any missing parent folders.
bool FileSystem::CreateDirectory(std::string_view /*virtualDirectory*/,
                                 std::string& /*outError*/) {
    return false;
}

// Reads a whole text file - a scene, the settings - into a string.
bool FileSystem::ReadTextFile(std::string_view /*virtualPath*/, std::string& /*outText*/,
                              std::string& /*outError*/) {
    return false;
}

// Reads a whole binary file - an image - into a list of bytes.
bool FileSystem::ReadFile(std::string_view /*virtualPath*/,
                          std::vector<unsigned char>& /*outBytes*/,
                          std::string& /*outError*/) {
    return false;
}

// Writes a text file, creating any folders it needs on the way.
bool FileSystem::WriteTextFile(std::string_view /*virtualPath*/, std::string_view /*text*/,
                               std::string& /*outError*/) {
    return false;
}

} // namespace eng
