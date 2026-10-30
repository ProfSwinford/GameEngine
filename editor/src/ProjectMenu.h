#pragma once

// ============================================================================
//  ProjectMenu.h - creating, opening and closing projects.
//
//  ==========================================================================
//  SWITCHING PROJECTS RESTARTS THE EDITOR, AND THAT IS THE DESIGN
//
//  Unity does the same thing, and for the same reason. It looks like the lazy
//  answer and it is very nearly the opposite.
//
//  The alternative is to re-root the file system while the program keeps
//  running. The engine starts twelve subsystems in a written-down order and
//  shuts them down in exactly the reverse, and the ones that depend on the
//  project are NOT a contiguous slice of that list - Log and FileSystem are
//  first and second, below Window, Renderer and the editor's own interface,
//  which must survive. SubsystemStack has Add, InitAll and ShutdownAll and
//  nothing else, and Add is a plain push_back: a second Engine::Init would
//  append a second copy of all twelve entries rather than replace them.
//
//  So "restart just the project-dependent parts" is not a thing this engine
//  can currently express, and teaching it to means new lifecycle machinery in
//  the one file whose entire stated purpose is that the order is written down
//  once and never improvised.
//
//  And even with that machinery, correctness would rest permanently on a long
//  list of things to reset: the texture and font caches are keyed on VIRTUAL
//  paths, so a new project asking for "textures/player.png" would be handed
//  the old project's image - silently, and with no crash to notice. ImGui's
//  ini path is a const char* it keeps rather than copies. Every panel holding
//  a folder, a selection or a scene path would need to be told. Every panel
//  added afterwards would need to remember to be told.
//
//  Ending the process discharges all of it at once. It costs about a second
//  and one window flicker, on something done a handful of times a day - and
//  the two things anybody would actually miss are already files on disk
//  beside the project: .editor/session.json remembers the open scene, the
//  selection and the Scene view's camera, and .editor/imgui.ini remembers the
//  panel layout. Reopening a project puts you back where you were.
//
//  It also makes engine version management nearly free. Launching a project
//  is already "start an executable with an argument", so opening one with a
//  different engine build is a different executable path and no engine code
//  at all. In one process it would mean two engine.dll files in one address
//  space with the project's scripts compiled against one of them, and the
//  documented symptom of getting that wrong is a library that loads, a
//  Console that says it worked, and a crash on the first allocation that
//  crosses the heap boundary.
//
//  ==========================================================================
//  HOW THE RESTART IS SEQUENCED
//
//  Nothing is torn down until the new project has been CHECKED. A folder that
//  turns out not to be a project leaves the editor exactly as it was, with a
//  line in the Console - rather than closing the editor and then failing to
//  open anything, which would look like a crash.
//
//  Then: the request is recorded, the frame loop finishes the frame it is on
//  and breaks, Shutdown runs in full - the session is saved, the scripts are
//  unloaded, the window goes - and only THEN is the new process started. In
//  that order because Windows will not let a loaded DLL be replaced, and
//  because two editors briefly sharing one project's .build/ folder is a
//  race over the same output files.
// ============================================================================

#include <string>
#include <vector>

namespace editor {

class ProjectMenu {
public:
    // Draws the project items for the File menu. Must be called between
    // ImGui's BeginMenu and EndMenu for "File".
    static void Draw();

    // Main thread, once a frame: collects a folder the picker has returned
    // and acts on it. Sits beside GameExport's own collection in the frame
    // loop, for the same reason and at the same point.
    static void CollectPickerResult();

    // ---- what the editor should do when the frame loop ends ---------------
    //
    // Shutdown happens either way; this is only about what comes after it.
    enum class Exit {
        Quit,            // the ordinary case: the window was closed
        OpenProject,     // start again on ProjectToOpen()
        OpenHub,         // close this project and go back to the launcher
    };

    static Exit               WhatNext();
    static const std::string& ProjectToOpen();

    // Asks for a project to be opened once the editor has shut down. Public
    // because the Hub-facing paths and the recents list both use it, and
    // because a test or a tool may want to drive it.
    //
    // CHECKS THE FOLDER FIRST and returns false, with a reason in the
    // Console, if it is not a project - leaving the editor running.
    static bool RequestOpen(const std::string& realRoot);

    // Closes the project, which closes the editor - there is no project-less
    // editor state, and inventing one would mean every panel learning to
    // draw nothing. A launcher that started this editor is still running.
    static bool RequestClose();

    // The window title: "<project> - <scene> - Engine2D". Called when either
    // part changes; it only touches the window when the string is different,
    // because setting a title every frame is a system call every frame.
    static void UpdateWindowTitle();
};

// The New Project dialog.
//
// DRAWN OUTSIDE THE MENU, by EditorApp, and it has to be: a modal opened from
// inside a BeginMenu block closes the moment the menu does, so a popup drawn
// there appears for exactly one frame and then vanishes.
void DrawNewProjectPopup();

// The "you have unsaved changes" question, asked only when switching away
// from a project would lose something. Drawn by EditorApp for the same
// reason as the one above.
void DrawUnsavedProjectPopup();

} // namespace editor
