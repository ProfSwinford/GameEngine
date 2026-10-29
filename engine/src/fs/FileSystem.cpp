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
#include <engine/fs/AssetArchive.h>
#include <engine/fs/FileSystem.h>

#include <cctype>

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

// Could this virtual path live inside an archive?
//
// Two things cannot, and neither is an arbitrary choice:
//
//    logs/        is written, not read. An archive is read-only.
//    .build/      holds the compiled script library, and SDL_LoadObject needs
//                 a real path on disk to hand to the operating system. There
//                 is no "load a DLL from memory" worth the trouble here.
//
// .editor/ is excluded for the same reason as logs/ - it is written.
//
// config/ CAN be archived, and in an exported game it is - packed into
// scenes.bin, because "startup.scene" decides which level the game opens with
// and a settings file sitting loose beside the executable is an invitation to
// change that. The settings travel with the scenes they choose between.
bool CouldBeArchived(std::string_view virtualPath) {
    return !virtualPath.starts_with("logs/") && !virtualPath.starts_with(".build/") &&
           virtualPath != ".build" && !virtualPath.starts_with(".editor/") &&
           virtualPath != ".editor";
}

// Joins a folder's virtual path to a name inside it.
//
// Written out rather than using operator/ because THE ASSETS ROOT IS THE
// EMPTY STRING: "" + "/" + name would produce "/name", which is an absolute
// path and means something entirely different.
std::string JoinVirtual(std::string_view directory, std::string_view leaf) {
    if (directory.empty()) {
        return std::string(leaf);
    }
    return std::string(directory) + "/" + std::string(leaf);
}

// Is `child` the same as `parent`, or somewhere underneath it?
//
// Compared one path COMPONENT at a time rather than as text, because text
// would say that "C:/work/assets2" is inside "C:/work/assets". Both paths
// should already have been through weakly_canonical.
bool IsInside(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
        if (c == child.end() || *c != *p) {
            return false;
        }
    }
    return true;
}

} // namespace

bool FileSystem::Init(const BootConfig&) {
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
            AssetArchive::MountAll(Resolve(""));   // see the note below
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

            // Any .bin archives sitting in assets/ are mounted now, before
            // anything asks for a file. An exported game finds its archives
            // here; a project being developed finds none, which is not an
            // error - it just means every read goes to disk as it always did.
            if (const int mounted = AssetArchive::MountAll(Resolve("")); mounted > 0) {
                ENGINE_LOG_INFO(Channels::kFileSys,
                                "{} asset archive(s) mounted; packed assets will be "
                                "read in preference to loose files",
                                mounted);
            }
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
    // Let go of the archive index too, so a second Init in the same process -
    // which is what the editor does when it builds scripts headlessly - does
    // not inherit the last run's mounts.
    AssetArchive::UnmountAll();
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
    //   .build/       the library the editor compiles your scripts into
    //
    // .build/ is kept out of assets/ deliberately. Your scripts live IN
    // assets/, wherever you like, alongside the scenes and images they belong
    // with - but the compiled result of them is not something you wrote and
    // not something to browse, so it does not sit in the same tree.
    //   .editor/      the editor's own remembered state - which scene was
    //                 open, where the Scene view was looking. Generated,
    //                 per-person, and nothing to do with the game, which is
    //                 why it is neither in config/ (authored, and packed into
    //                 builds) nor in .build/ (output of the script compiler).
    if (virtualPath.starts_with("config/") || virtualPath.starts_with("logs/") ||
        virtualPath.starts_with(".build/") || virtualPath == ".build" ||
        virtualPath.starts_with(".editor/") || virtualPath == ".editor") {
        path = fs::path(g_root) / fs::path(std::string(virtualPath));
    }

    // lexically_normal tidies up things like "a/./b" and "a/../b".
    return path.lexically_normal().string();
}

// Does this path end with this extension - ".oscene", ".png" - IGNORING
// CAPITALS?
//
// Ignoring them is the whole point. Nobody controls the case of a file name
// reliably: Windows treats "Boss.OSCENE" and "boss.oscene" as the same file
// while Linux does not, and an image exported from a camera frequently arrives
// as .PNG. A rule that matched only lower case would make the same file
// visible to one part of the editor and invisible to another.
//
// std::tolower is handed an `unsigned char` rather than the char itself because
// it is only defined for values that fit in one, and char is SIGNED on this
// compiler - so an accented character arrives negative and the behaviour is
// undefined. The cast is not decoration.
bool FileSystem::HasExtension(std::string_view virtualPath, std::string_view extension) {
    if (extension.size() > virtualPath.size()) {
        return false;
    }
    const std::size_t offset = virtualPath.size() - extension.size();
    for (std::size_t i = 0; i < extension.size(); ++i) {
        const int a = std::tolower(static_cast<unsigned char>(virtualPath[offset + i]));
        const int b = std::tolower(static_cast<unsigned char>(extension[i]));
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool FileSystem::Exists(std::string_view virtualPath) {
    // ARCHIVES FIRST, THEN DISK, and that order is the whole design. An
    // exported game has archives and no loose assets; the editor has loose
    // assets and no archives; a developer testing an export in place could
    // have both, and then the archive is what the player will actually get.
    if (AssetArchive::AnyMounted() && CouldBeArchived(virtualPath) &&
        AssetArchive::Contains(virtualPath)) {
        return true;
    }
    std::error_code ec;
    return fs::exists(Resolve(virtualPath), ec);
}

bool FileSystem::ListFiles(std::string_view virtualDirectory, std::string_view extension,
                           std::vector<std::string>& out) {
    out.clear();

    const std::string real = Resolve(virtualDirectory);

    std::error_code ec;
    const bool      haveFolder = fs::is_directory(real, ec);

    // A MISSING FOLDER IS NOT YET A FAILURE. An exported game has no loose
    // scenes/ folder at all - it has scenes.bin - so the folder genuinely is
    // not there and the archive below is the right answer. Only the case where
    // neither exists is worth complaining about.
    if (haveFolder) {
        std::string prefix(virtualDirectory);
        if (!prefix.empty() && prefix.back() != '/') {
            prefix.push_back('/');
        }

        for (const fs::directory_entry& entry : fs::directory_iterator(real, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (!extension.empty() && !HasExtension(name, extension)) {
                continue;
            }
            // VIRTUAL paths are returned, not real ones. A caller handed a real
            // path would have to know not to pass it back to ReadFile, which
            // would resolve it a second time and produce a doubled-up path.
            out.push_back(prefix + name);
        }
    }

    // AND WHATEVER AN ARCHIVE HOLDS for the same folder. An exported game has
    // no loose scenes/ folder, so without this a level-select menu built from
    // ListFiles would come back empty in the shipped game and work perfectly
    // in the editor - the worst shape a bug can have.
    if (AssetArchive::AnyMounted()) {
        std::vector<std::string> archived;
        AssetArchive::ListFolder(virtualDirectory, archived);
        for (std::string& path : archived) {
            if (!extension.empty() && !HasExtension(path, extension)) {
                continue;
            }
            if (std::find(out.begin(), out.end(), path) == out.end()) {
                out.push_back(std::move(path));
            }
        }
    }

    if (!haveFolder && out.empty()) {
        ENGINE_LOG_WARN(Channels::kFileSys,
                        "'{}' is not a folder and no archive holds it (looked in '{}')",
                        virtualDirectory, real);
        return false;
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
        // may legitimately not exist yet - a folder before the first
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

bool FileSystem::Remove(std::string_view virtualPath, std::size_t& outDeletedCount,
                        std::string& outError) {
    outDeletedCount = 0;

    // ========================================================================
    //  AN EMPTY PATH IS THE ASSETS ROOT. Refused before anything else.
    //
    //  Resolve("") hands back the assets folder itself, so a caller that
    //  passed an empty string through by mistake would delete the entire
    //  project. Guarding here rather than at each call site means a new
    //  caller cannot reintroduce it.
    // ========================================================================
    if (virtualPath.empty()) {
        outError = "refusing to delete the assets folder itself";
        return false;
    }

    const std::string real = Resolve(virtualPath);

    std::error_code ec;
    if (!fs::exists(real, ec)) {
        outError = "'" + std::string(virtualPath) + "' is not there";
        return false;
    }

    // remove_all handles both cases - one file, or a folder and its whole
    // contents - and returns how many things it deleted, which is exactly
    // what the confirmation dialog wants to be able to say.
    const std::uintmax_t removed = fs::remove_all(real, ec);
    if (ec) {
        outError = "cannot delete '" + std::string(virtualPath) + "': " + ec.message();
        return false;
    }

    outDeletedCount = static_cast<std::size_t>(removed);
    outError.clear();
    return true;
}

bool FileSystem::Rename(std::string_view virtualPath, std::string_view newName,
                        std::string& outError) {
    if (virtualPath.empty()) {
        outError = "refusing to rename the assets folder itself";
        return false;
    }
    if (newName.empty()) {
        outError = "a name cannot be empty";
        return false;
    }

    // A NAME, NOT A PATH. Letting a separator through would turn a rename
    // into a move - including a move to somewhere outside the project, with
    // "..". See the note on this function in the header.
    if (newName.find('/') != std::string_view::npos ||
        newName.find('\\') != std::string_view::npos) {
        outError = "a name cannot contain a slash - rename does not move things";
        return false;
    }

    const fs::path from = fs::path(Resolve(virtualPath));
    const fs::path to   = from.parent_path() / fs::path(std::string(newName));

    std::error_code ec;
    if (!fs::exists(from, ec)) {
        outError = "'" + std::string(virtualPath) + "' is not there";
        return false;
    }

    // REFUSED rather than overwriting. Renaming one file onto another
    // destroys the second one, and nothing about typing a name suggests that
    // is what you are asking for.
    if (fs::exists(to, ec)) {
        outError = "there is already something called '" + std::string(newName) + "'";
        return false;
    }

    fs::rename(from, to, ec);
    if (ec) {
        outError = "cannot rename '" + std::string(virtualPath) + "': " + ec.message();
        return false;
    }

    outError.clear();
    return true;
}

bool FileSystem::Move(std::string_view virtualPath,
                      std::string_view virtualTargetDirectory, std::string& outError) {
    if (virtualPath.empty()) {
        outError = "refusing to move the assets folder itself";
        return false;
    }

    // ========================================================================
    //  A FOLDER CANNOT GO INSIDE ITSELF, and this is checked before anything
    //  touches the disk.
    //
    //  std::filesystem::rename does not stop you, and what you get is a
    //  branch of the tree that no longer has a route to it. Dragging a folder
    //  onto one of its own children is an easy slip to make with a mouse, so
    //  the guard belongs here rather than at each call site.
    //
    //  IT IS A TEXT COMPARISON, which is what makes it exact. "textures" is
    //  a prefix of both "textures/ui" and "textures2", and only the first of
    //  those is inside it - so the character after the prefix has to be the
    //  separator. Getting that wrong would refuse perfectly legal moves while
    //  looking, in a quick reading, entirely correct.
    // ========================================================================
    if (virtualTargetDirectory == virtualPath ||
        (virtualTargetDirectory.size() > virtualPath.size() &&
         virtualTargetDirectory.starts_with(virtualPath) &&
         virtualTargetDirectory[virtualPath.size()] == '/')) {
        outError = "cannot move '" + std::string(virtualPath) + "' inside itself";
        return false;
    }

    const fs::path    from = fs::path(Resolve(virtualPath));
    const std::string leaf = from.filename().string();

    std::error_code ec;
    if (!fs::exists(from, ec)) {
        outError = "'" + std::string(virtualPath) + "' is not there";
        return false;
    }

    const fs::path targetDirectory = fs::path(Resolve(virtualTargetDirectory));
    if (!fs::is_directory(targetDirectory, ec)) {
        outError = "'" + std::string(virtualTargetDirectory) + "' is not a folder";
        return false;
    }

    // Already where it is being asked to go. Said out loud rather than
    // reported as a successful move that moved nothing.
    if (fs::equivalent(from.parent_path(), targetDirectory, ec)) {
        outError = "'" + leaf + "' is already in there";
        return false;
    }

    const fs::path to = targetDirectory / from.filename();
    if (fs::exists(to, ec)) {
        outError = "there is already something called '" + leaf + "' in there";
        return false;
    }

    fs::rename(from, to, ec);
    if (ec) {
        outError = "cannot move '" + std::string(virtualPath) + "': " + ec.message();
        return false;
    }

    outError.clear();
    return true;
}

bool FileSystem::Import(std::string_view realSourcePath,
                        std::string_view virtualTargetDirectory,
                        std::string& outVirtualPath, std::string& outError) {
    outVirtualPath.clear();

    const fs::path  source = fs::path(std::string(realSourcePath));
    std::error_code ec;
    if (!fs::exists(source, ec)) {
        outError = "'" + std::string(realSourcePath) + "' is not there";
        return false;
    }

    const fs::path targetDirectory = fs::path(Resolve(virtualTargetDirectory));
    if (!fs::is_directory(targetDirectory, ec)) {
        outError = "'" + std::string(virtualTargetDirectory) + "' is not a folder";
        return false;
    }

    // ========================================================================
    //  IS IT ALREADY IN THE PROJECT?
    //
    //  Opening assets/ in the file explorer and dragging something out of it
    //  back into this panel asks for a file to be copied on top of itself. At
    //  best that leaves a duplicate nobody wanted; the person was almost
    //  certainly trying to MOVE it, which the panel can do directly.
    //
    //  Decided by comparing the real paths, not by any rule about names,
    //  because that is the only test that is actually true. weakly_canonical
    //  resolves ".." and symbolic links, so the same file reached two
    //  different ways compares equal.
    // ========================================================================
    const fs::path assets         = fs::weakly_canonical(fs::path(Resolve("")), ec);
    const fs::path realSource     = fs::weakly_canonical(source, ec);
    if (IsInside(realSource, assets)) {
        outError = "'" + source.filename().string() +
                   "' is already in the project - drag it inside the Assets panel to "
                   "move it";
        return false;
    }

    // ---- a name nothing else is using --------------------------------------
    //
    // See the header: a second `tiles.png` becomes `tiles (2).png`. The count
    // is bounded because a loop looking for a free name must terminate even
    // if something outside is creating files as fast as we check.
    const std::string stem      = source.stem().string();
    const std::string extension = source.extension().string();
    std::string       leaf      = source.filename().string();
    for (int attempt = 2; attempt < 1000 && fs::exists(targetDirectory / leaf, ec);
         ++attempt) {
        leaf = stem + " (" + std::to_string(attempt) + ")" + extension;
    }
    if (fs::exists(targetDirectory / leaf, ec)) {
        outError = "could not find an unused name for '" + source.filename().string() + "'";
        return false;
    }

    // copy_options::recursive is what brings a dropped FOLDER's contents in
    // with it. For a single file it makes no difference at all.
    fs::copy(source, targetDirectory / leaf, fs::copy_options::recursive, ec);
    if (ec) {
        outError =
            "cannot import '" + source.filename().string() + "': " + ec.message();
        return false;
    }

    outVirtualPath = JoinVirtual(virtualTargetDirectory, leaf);
    outError.clear();
    return true;
}

bool FileSystem::ReadTextFile(std::string_view virtualPath, std::string& outText,
                              std::string& outError) {
    // From an archive when one holds it. Text and bytes are the same thing to
    // an archive, so this borrows the binary read and then says "those bytes
    // are characters" - which is all a text file ever was.
    if (AssetArchive::AnyMounted() && CouldBeArchived(virtualPath) &&
        AssetArchive::Contains(virtualPath)) {
        std::vector<unsigned char> bytes;
        if (!AssetArchive::Read(virtualPath, bytes, outError)) {
            return false;
        }
        outText.assign(bytes.begin(), bytes.end());
        outError.clear();
        return true;
    }

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
    if (AssetArchive::AnyMounted() && CouldBeArchived(virtualPath) &&
        AssetArchive::Contains(virtualPath)) {
        return AssetArchive::Read(virtualPath, outBytes, outError);
    }

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
