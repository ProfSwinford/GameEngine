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

// Is there a launcher beside this editor?
//
// Asked because Close Project has two sensible meanings depending on the
// answer. With a launcher, closing a project should land you back at the list
// of them; without one, there is nowhere to land and closing the project is
// simply closing the editor. Both are honest; silently doing the second while
// a menu item implies the first is not.
bool HubIsInstalled();

// Starts the launcher and returns at once. Same timing rule as Relaunch: only
// after the editor has shut down.
bool RelaunchHub(std::string& outError);

} // namespace editor
