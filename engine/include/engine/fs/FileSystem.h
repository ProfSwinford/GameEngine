#pragma once

// =============================================================================
//  WEEK 9 - the file system layer. Ch. 7.1. *** KEPT THIN ON PURPOSE. ***
//
//  THE ONE PROBLEM IT SOLVES: assets live next to the source; the executable
//  lives in build/debug/bin; a lab PC has a different drive letter and a
//  different path than a laptop. An absolute path in source works on exactly
//  one machine, and the M3 verification is explicitly that a fresh clone on a
//  machine with a DIFFERENT PATH LAYOUT loads the same scene with no edits.
//
//  THE FIX: every path in engine code is VIRTUAL - "textures/player.bmp" -
//  resolved at runtime against a root discovered by walking UP from the
//  executable's own location looking for a marker.
//
//  *** NOT the current working directory. *** It differs between running from
//  an IDE, from a terminal, and from a double-click, and that inconsistency is
//  a classic half-day of confusion. The executable's own location does not.
//
//  THE RESOLVED ROOT IS LOGGED AT INFO, EVERY BOOT. When an asset is not found
//  on someone else's machine, that line is the first thing anyone wants.
//
//  This also retired the Week 8 stopgap that copied config/ next to the
//  executable after every build.
//
//  ---------------------------------------------------------------------------
//  *** WHICH THREAD AM I ON? *** - the decision, written down here because
//  whoever reads this next needs to know.
//
//  ReadFileAsync pushes the read onto the WEEK 5 QUEUE, so the file read
//  itself happens on a WORKER THREAD. The completion callback does NOT run
//  there. Finished reads are posted into a second, completed-results queue
//  that the main thread drains in PumpCompletions(), and the callback runs
//  from there.
//
//  Why: the obvious design runs the callback on the worker, and the callback
//  then touches the renderer (creating a texture) or the resource manager's
//  tables. Both are main-thread-only, and the thread sanitizer says so
//  immediately. Deferring costs at most one frame of latency on an
//  asynchronous load, which is the thing that was already asynchronous.
//
//  So: EVERY callback passed to ReadFileAsync runs on the MAIN THREAD.
// =============================================================================

#include <engine/core/Types.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class FileSystem {
public:
    // Discovers the asset root and remembers it. Logs what it resolved to.
    static bool Init();
    static void Shutdown();

    static const std::string& AssetRoot();

    // Virtual path -> real path. Never fails; a path that resolves to
    // something that does not exist is Exists()'s problem, not this one's.
    static std::string Resolve(std::string_view virtualPath);

    static bool Exists(std::string_view virtualPath);

    // Lists the files directly inside a virtual directory, returning VIRTUAL
    // paths ready to hand straight back to ReadFile or Scene::Load. Sorted, so
    // a menu built from it does not reorder itself between runs. An extension
    // filter of "" lists everything; non-recursive, because nothing yet needs
    // recursion and a recursive default is a surprise waiting to happen.
    //
    // THIS EXISTS BECAUSE THE EDITOR NEEDED IT, and that is the same story as
    // the Week 3 in-memory log sink and the Week 8 CVar enumeration: the tool
    // could not be written, the missing capability turned out to be a real gap
    // in the engine's API rather than a convenience, and the engine got better.
    // An engine that can load a scene by name but cannot tell you WHICH scenes
    // exist can only ever be driven by something that already knows the answer.
    static bool ListFiles(std::string_view virtualDirectory, std::string_view extension,
                          std::vector<std::string>& out);

    // One entry in a directory listing. `virtualPath` is ready to hand back to
    // ReadFile / Scene::Load; `name` is the last component, for display.
    struct DirEntry {
        std::string name;
        std::string virtualPath;
        bool        isDirectory = false;
        u64         byteSize    = 0;   // 0 for directories
    };

    // The listing an asset BROWSER needs, as opposed to the one a menu needs.
    // ListFiles answers "which scenes exist"; this answers "what is in this
    // folder", which means directories have to come back too - a browser that
    // cannot see a subfolder cannot navigate into it.
    //
    // Directories first, then files, each group sorted by name: the order every
    // file manager uses, and stable between runs for the same reason ListFiles
    // sorts.
    static bool ListDirectory(std::string_view virtualDirectory,
                              std::vector<DirEntry>& out);

    // Creates a directory and any missing parents. Needed because the asset
    // browser can create a script before `scripts/` exists, and failing that
    // with "cannot open file" would name the wrong problem.
    static bool CreateDirectory(std::string_view virtualDirectory, std::string& outError);

    // Synchronous read of an entire file into bytes. A missing file is an
    // ENVIRONMENT failure, not a programmer error - error return with a
    // readable reason, never an assert.
    static bool ReadFile(std::string_view virtualPath, std::vector<u8>& outBytes,
                         std::string& outError);

    static bool WriteFile(std::string_view virtualPath, const void* data, usize bytes,
                          std::string& outError);

    // Asynchronous read through the Week 5 queue. The callback runs on the
    // MAIN THREAD, from PumpCompletions - see the block comment above.
    using ReadCallback = std::function<void(bool success, std::vector<u8>&& bytes,
                                            const std::string& error)>;
    static void ReadFileAsync(std::string_view virtualPath, ReadCallback onComplete);

    // Drains completed async reads and fires their callbacks. Called once per
    // frame from the main thread.
    static void PumpCompletions();

    // How many reads are in flight. The Jobs panel shows it; it is also how
    // Scene::Load knows whether to wait.
    static usize PendingReadCount();
};

} // namespace eng
