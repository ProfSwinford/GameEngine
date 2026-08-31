#pragma once

// ============================================================================
//  FileSystem.h - finding and reading the game's files.
//
//  THE PROBLEM IT SOLVES
//  The pictures and scene files live in `assets/` next to the source code. The
//  program that has to read them lives somewhere like `build/debug/bin/`. And
//  the whole project sits at a different place on every machine - a different
//  drive letter, a different user name, a different folder.
//
//  Writing a path like "C:/Users/me/GameEngine/assets/player.bmp" into the
//  source works on exactly one computer.
//
//  THE FIX: VIRTUAL PATHS
//  Everything in the engine names files the short way - "textures/player.bmp",
//  "scenes/level1.json" - and this class works out the real location at run
//  time. It does that by starting at the program's own location and walking
//  UP the folder tree until it finds one containing an `assets` folder.
//
//  WHY NOT THE "CURRENT DIRECTORY"
//  The current directory is whatever folder you happened to be in when you
//  started the program, and it differs between running from Visual Studio,
//  running from a terminal, and double-clicking the executable. That
//  inconsistency is a classic half-day of confusion. Where the executable
//  itself lives does not change.
//
//  The folder it settles on is written to the log at start-up, so when a file
//  will not load on somebody else's machine, that line is the first thing to
//  look at.
// ============================================================================

#include <string>
#include <string_view>
#include <vector>

namespace eng {

class FileSystem {
public:
    // Works out where the project's files are and remembers it.
    static bool Init();
    static void Shutdown();

    static const std::string& AssetRoot();

    // Turns "textures/player.bmp" into a real path on this machine. It never
    // fails; whether the file actually exists is Exists()'s question.
    static std::string Resolve(std::string_view virtualPath);

    static bool Exists(std::string_view virtualPath);

    // Lists the files directly inside a folder, giving back VIRTUAL paths that
    // can be handed straight back to ReadFile or Scene::Load.
    //
    // Pass "" as the extension to list everything. The result is sorted, so a
    // menu built from it does not reshuffle itself between runs.
    static bool ListFiles(std::string_view virtualDirectory, std::string_view extension,
                          std::vector<std::string>& out);

    // One row of a folder listing, for the Assets browser.
    struct DirEntry {
        std::string       name;          // just the last part, for display
        std::string       virtualPath;   // ready to pass back to ReadFile
        bool              isDirectory = false;
        unsigned long long byteSize   = 0;   // 0 for folders
    };

    // The listing a BROWSER needs, as opposed to the one a menu needs.
    // ListFiles answers "which scenes exist"; this answers "what is in this
    // folder", so it includes sub-folders - a browser that cannot see a
    // sub-folder cannot navigate into it.
    //
    // Folders come first, then files, each group sorted by name. That is what
    // every file manager does.
    static bool ListDirectory(std::string_view virtualDirectory,
                              std::vector<DirEntry>& out);

    // Creates a folder, including any missing parent folders.
    static bool CreateDirectory(std::string_view virtualDirectory, std::string& outError);

    // Reads a whole TEXT file - a scene, the config - into a std::string.
    //
    // std::string is used rather than a list of bytes because that is what the
    // JSON parser wants and what is easiest to inspect while debugging. For a
    // file that is genuinely text, this is the simplest thing that works.
    static bool ReadTextFile(std::string_view virtualPath, std::string& outText,
                             std::string& outError);

    // Reads a whole BINARY file - an image - into a list of bytes.
    //
    // `unsigned char` is the standard type for "one byte of raw data". A
    // std::vector of them is the standard replacement for the old C style of
    // "a pointer and a length you have to keep in step": the vector knows its
    // own size and frees itself.
    static bool ReadFile(std::string_view virtualPath, std::vector<unsigned char>& outBytes,
                         std::string& outError);

    // Writes text out to a file, creating folders as needed. Used by Save
    // Scene and by the Assets panel's "new script" button.
    static bool WriteTextFile(std::string_view virtualPath, std::string_view text,
                              std::string& outError);
};

} // namespace eng
