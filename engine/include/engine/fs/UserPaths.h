#pragma once

// ============================================================================
//  UserPaths.h - where things that belong to the PERSON live, rather than to
//  a project.
//
//  ==========================================================================
//  WHY THIS IS NOT FileSystem
//
//  FileSystem answers "where is this project's content?" and every path it
//  produces is inside a project. That is the right answer for scenes,
//  textures, settings and compiled scripts - all of which travel with the
//  project, belong in its version control, and mean nothing without it.
//
//  A list of recently opened projects is the opposite of all of that. It is
//  not about any one project, it cannot live inside one (which project's copy
//  would be right?), and it is specific to one person on one machine.
//
//  So it goes where the operating system keeps that sort of thing:
//  %APPDATA% on Windows, ~/Library/Application Support on macOS,
//  ~/.local/share on Linux. SDL_GetPrefPath knows which, and creates it.
//
//  ==========================================================================
//  WHAT BELONGS HERE, AND WHAT EMPHATICALLY DOES NOT
//
//  HERE: the recent-projects list, the list of installed engine builds, and
//  anything else a launcher needs before any project is open - which is the
//  load-bearing part. A launcher has no project, therefore no project root,
//  therefore no working FileSystem::Resolve. It still has to remember things.
//
//  NOT HERE: anything a project needs in order to be itself. If a file has to
//  be copied along with a project for the project to work, it belongs under
//  the project root and FileSystem already puts it there. Putting such a file
//  here would make projects silently depend on the machine they were made on,
//  which is the bug this separation exists to prevent.
//
//  Per-project editor state - the open scene, the camera, the panel layout -
//  is a middle case that is deliberately NOT here: it is one person's state,
//  but it is one person's state ABOUT ONE PROJECT, so it lives in the
//  project's own .editor/ folder and is git-ignored. See EditorSession.h.
// ============================================================================

#include <string>

namespace eng {

class UserPaths {
public:
    // The folder for this user's settings, created if it was not there, with
    // a trailing separator. Empty only if the operating system refused, which
    // callers must cope with rather than assume away - a read-only or
    // sandboxed home directory is unusual, not impossible.
    //
    // Read once and remembered.
    static const std::string& Dir();

    // A file inside it. `Dir()` plus a name, or empty when Dir() is.
    //
    // A NAME, NOT A PATH: it is joined, never resolved, so a caller cannot
    // reach outside the settings folder by passing "../" - this is one of the
    // few places in the engine that writes outside a project, so it does not
    // also get to write anywhere.
    static std::string File(std::string_view name);
};

} // namespace eng
