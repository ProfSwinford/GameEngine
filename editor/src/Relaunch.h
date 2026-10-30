#pragma once

// ============================================================================
//  Relaunch.h - starting the editor again on a different project.
//
//  ==========================================================================
//  WHY THIS IS A SEPARATE FILE AND NOT THREE LINES IN main()
//
//  Because of WHEN it has to happen, which is the whole difficulty.
//
//  The new process must not start until the old one has finished shutting
//  down. Two reasons, and both are hard failures rather than untidiness:
//
//    Windows will not let a loaded DLL be replaced or deleted. The editor
//    holds engine.dll and one library per script in the project's .build/
//    folder, and the new editor will want to compile into that same folder
//    the moment it starts.
//
//    Two editors briefly sharing one project are two processes writing
//    .editor/session.json and .editor/imgui.ini. Whichever exits last wins,
//    so the project would remember whatever the one that was only passing
//    through happened to think.
//
//  So the request travels as a FLAG - see ProjectMenu - the frame loop ends,
//  EditorApp::Shutdown runs in full, and main() calls this afterwards. The
//  ordering is the feature; putting the call where the menu item is would
//  look identical and be wrong.
//
//  ==========================================================================
//  SDL_CreateProcess RATHER THAN system() OR exec
//
//  system() would open a console window, which the script build went to some
//  trouble to stop doing - see RunCaptured in ScriptBuild.cpp. exec() does
//  not exist on Windows, and the spawn family would make this file two
//  implementations.
//
//  SDL is already linked by the editor and has exactly this. The handle is
//  destroyed immediately: SDL_DestroyProcess releases our side of it without
//  waiting and without killing the child, which is what "start this and let
//  go" means - the new editor must outlive the one that started it.
// ============================================================================

#include <string>

namespace editor {

// ===========================================================================
//  WHAT THE LAUNCHER SHOULD BE TOLD WHEN IT IS OPENED.
//
//  NEW OPTIONS MAY BE ADDED HERE FREELY, and that is worth saying because the
//  same is emphatically NOT true in the other direction.
//
//  This starts the launcher sitting BESIDE this executable - see HubPath - and
//  CMake makes the editor target depend on the hub target, so the two are
//  always the same build. The launcher, by contrast, starts editors in OTHER
//  folders on purpose: that is how it opens a project with a different engine
//  version. An option it passed that an older editor did not recognise would
//  make that editor print its usage text and exit, which from the outside is
//  a project that will not open.
// ===========================================================================
struct HubRequest {
    // The editor had no project to open, so the launcher is standing in for
    // the window that would otherwise have appeared. It says so when it
    // starts: somebody who ran the editor and got a launcher is owed the
    // reason rather than left to deduce it.
    bool becauseNoProject = false;

    // Passed straight through. `editor --frames 3` means "start, run briefly,
    // exit" - and when there is no project it is the LAUNCHER that starts, so
    // it has to obey the same limit. Otherwise an automated run that was
    // meant to end by itself leaves a launcher window open forever, which is
    // worse than the hang it replaced.
    int frameLimit = 0;

    // ----------------------------------------------------------------------
    //  BLOCK UNTIL THE LAUNCHER HAS FINISHED, instead of letting go of it.
    //
    //  Set for a run that promised to end by itself - `editor --frames N`.
    //  Such a caller is entitled to assume that when the command returns,
    //  nothing of ours is still running; `editor --frames 2 && cmake --build`
    //  depends on it. Letting go breaks that promise in a way that names
    //  nothing to do with the cause, because hub.exe is itself a build
    //  output:
    //
    //     LINK : fatal error LNK1168: cannot open bin\hub.exe for writing
    //
    //  LEFT FALSE FOR THE INTERACTIVE CASE, where the opposite is right: the
    //  terminal should come back, and the launcher should be nobody's child.
    // ----------------------------------------------------------------------
    bool waitForIt = false;
};

// The folder this executable is in, with no trailing separator.
//
// One copy, in one place. There were three private ones - in ScriptBuild, in
// GameExport and in EditorGui - all wrapping the same SDL call for the same
// reason, which is fine until one of them is fixed and the others are not.
std::string ExecutableDirectory();

// Starts a new editor on `projectRoot` and returns at once.
//
// MUST BE CALLED AFTER THE EDITOR HAS SHUT DOWN. See the header note.
//
// Returns false with a reason if the new process could not be started, in
// which case nothing is running and the caller should say so - the old editor
// has already closed by then, so a silent failure looks exactly like the
// editor crashing on exit.
bool Relaunch(const std::string& projectRoot, std::string& outError);

// Where the launcher is expected to be, whether or not it is there.
//
// Exists so that a message about a MISSING launcher can name the path it
// looked at. "There is no launcher beside the editor" is only ever as true as
// ExecutableDirectory() is, and that has a known limit - see the note on
// HubIsInstalled - so the honest form of the complaint shows its working
// instead of asserting a conclusion.
std::string LauncherPath();

// Is there a launcher beside this editor?
//
// A FALSE ANSWER IS NOT PROOF THERE IS NO LAUNCHER. SDL_GetBasePath returns
// UTF-8, and `fs::path` built from a `const char*` decodes it with MSVC's
// narrow code page instead, so every non-ASCII byte in this executable's own
// path is mis-read and the file is looked for somewhere that does not exist.
// A launcher sitting right there is then reported missing.
//
// That decode is older than the hand-off and lives in three places - here,
// hub/src/main.cpp's ThisDirectory and FileSystem::Init's starting point - so
// the upward search was already blind in the same folders. It is not fixed
// here because fixing it properly means auditing every path that goes back
// out through .string(), which is a change of its own. What IS done here is
// that the refusal names LauncherPath(), so somebody looking at a folder that
// plainly contains hub.exe can see what the editor looked for.
//
// Asked because Close Project has two sensible meanings depending on the
// answer. With a launcher, closing a project should land you back at the list
// of them; without one, there is nowhere to land and closing the project is
// simply closing the editor. Both are honest; silently doing the second while
// a menu item implies the first is not.
bool HubIsInstalled();

// Starts the launcher and returns at once.
//
// SAME TIMING RULE AS Relaunch when the editor is running: only after it has
// shut down. The one exception is the hand-off at start-up, where nothing has
// been initialised yet and so neither reason in the header note applies -
// there is no engine.dll held open and no session file to race over. See
// editor/src/main.cpp, which is the only caller that does it.
bool RelaunchHub(std::string& outError, const HubRequest& request = {});

} // namespace editor
