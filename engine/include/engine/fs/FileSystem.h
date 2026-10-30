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
//  "scenes/level1.oscene" - and this class works out the real location at run
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

#include <engine/core/Subsystem.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

class FileSystem : public Subsystem {
public:
    // Works out where the project's files are and remembers it. Nothing in the
    // settings affects that, so the config is accepted and ignored.
    //
    // Returns FALSE when it could not find a project, and the caller must act
    // on that - see Engine::Init. A failed Init leaves no root at all rather
    // than a guessed one, so Resolve returns nothing and nothing can be
    // written outside a project by accident.
    bool Init(const BootConfig& config) override;
    void Shutdown() override;

    // ---- which project to open -------------------------------------------
    //
    // Call before Init to name the project folder explicitly, which is what
    // `editor --project <folder>` does. This is the reason a project no
    // longer has to sit beside the executable.
    //
    // Checked ahead of ENGINE_ASSET_ROOT and ahead of the upward search, and
    // unlike both of those a wrong answer here FAILS rather than falling back
    // to guessing: somebody who named one folder must not silently get a
    // different project. Passing an empty string means "nobody said", which
    // restores the original search entirely - that is what the tests and the
    // player rely on.
    //
    // It survives Shutdown deliberately; see the note in the .cpp.
    static void UseRoot(std::string_view realDirectory);

    // Is a project open? False before Init, after Shutdown, and after an Init
    // that found nothing.
    static bool HasRoot();

    static const std::string& AssetRoot();

    // Turns "textures/player.bmp" into a real path on this machine. It never
    // fails; whether the file actually exists is Exists()'s question.
    static std::string Resolve(std::string_view virtualPath);

    static bool Exists(std::string_view virtualPath);

    // Does this path end with this extension - ".oscene", ".png" - IGNORING
    // CAPITALS?
    //
    // Ignoring them is the whole point. Nobody controls the case of a file
    // name reliably: Windows treats "Boss.OSCENE" and "boss.oscene" as the
    // same file while Linux does not, and an image exported from a camera or a
    // paint program frequently arrives as .PNG. A rule that matched only lower
    // case would make the same file visible to one part of the editor and
    // invisible to another.
    //
    // That is not hypothetical - it is the bug this function was added to end.
    // ListFiles used to compare exactly while the asset browser ignored
    // capitals, so a scene saved as .OSCENE appeared in the Assets panel and
    // never showed up in the Open Scene menu, with nothing anywhere to explain
    // the difference.
    static bool HasExtension(std::string_view virtualPath, std::string_view extension);

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

    // ========================================================================
    //  Deletes a file, or a folder AND EVERYTHING INSIDE IT.
    //
    //  `outDeletedCount` says how many things went, which is what lets the
    //  editor ask "delete this folder and the 23 files in it?" rather than a
    //  bare "are you sure?". The difference matters: one of those questions
    //  can be answered and the other cannot.
    //
    //  AN EMPTY PATH IS REFUSED. An empty virtual path resolves to the assets
    //  root, so a bug that passed one through would delete the entire
    //  project. That is not a hypothetical worth leaving open.
    // ========================================================================
    static bool Remove(std::string_view virtualPath, std::size_t& outDeletedCount,
                       std::string& outError);

    // Renames a file or folder WITHIN ITS OWN FOLDER. `newName` is a bare
    // name, not a path - moving things between folders is a different
    // operation and giving one function both jobs is how you end up with a
    // rename that silently moves something.
    //
    // Refuses when the target already exists, rather than overwriting it.
    static bool Rename(std::string_view virtualPath, std::string_view newName,
                       std::string& outError);

    // ========================================================================
    //  Moves a file or folder INTO ANOTHER FOLDER, keeping its own name.
    //
    //  The other half of Rename, which changes a name without changing where
    //  something lives. Keeping them apart is what stops either one doing the
    //  other's job by accident - see the note on Rename just above.
    //
    //  `virtualTargetDirectory` is a FOLDER, and the assets root IS the empty
    //  string, so "" is a legitimate destination meaning "back out to the
    //  top".
    //
    //  THREE THINGS ARE REFUSED, and every one of them is something a drag
    //  can ask for by accident:
    //
    //    * moving a folder into itself, or into one of its own sub-folders.
    //      std::filesystem::rename will do this without complaint and leave
    //      the whole branch unreachable, so it is caught here first.
    //    * moving something onto a name that is already taken, which would
    //      destroy whatever was there.
    //    * moving something into the folder it is already in. Nothing is
    //      harmed, but reporting a successful move that moved nothing is how
    //      a user concludes the feature does not work.
    // ========================================================================
    static bool Move(std::string_view virtualPath,
                     std::string_view virtualTargetDirectory, std::string& outError);

    // ========================================================================
    //  Copies something from ANYWHERE ON THIS MACHINE into the project.
    //
    //  This is what dragging a file from the desktop onto the Assets panel
    //  does. It is the only function here that takes a REAL path rather than
    //  a virtual one, for the unavoidable reason that the thing being
    //  imported is not in the project yet and so has no virtual path to give.
    //
    //  IT COPIES RATHER THAN MOVES. A tool that silently emptied a file out
    //  of somebody's Downloads folder because they dragged it somewhere would
    //  be indefensible, however convenient it occasionally was.
    //
    //  A FOLDER ARRIVES WITH ITS CONTENTS. Dropping a sprite pack in should
    //  bring the sprites, not an empty folder named after them.
    //
    //  THE NAME IS MADE UNIQUE rather than overwritten or refused: a second
    //  `tiles.png` is written as `tiles (2).png`. Refusing is safe but
    //  maddening half way through a drop of twenty files, and overwriting is
    //  not safe at all. `outVirtualPath` reports where it actually landed,
    //  which is the only way the caller can say so.
    // ========================================================================
    static bool Import(std::string_view realSourcePath,
                       std::string_view virtualTargetDirectory,
                       std::string& outVirtualPath, std::string& outError);

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
