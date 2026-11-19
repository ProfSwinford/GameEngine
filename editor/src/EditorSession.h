#pragma once

// ============================================================================
//  EditorSession.h - picking up where you left off.
//
//  The editor writes .editor/session.json when it closes and reads it when it
//  opens, so the scene you were working on is the scene you come back to.
//  Closing the editor to go and read something should not cost you the three
//  clicks of finding your way back.
//
//  ==========================================================================
//  WHAT THE LAYOUT DOES NOT NEED, because ImGui already handles it.
//
//  Where the panels are docked, which are open, how wide they are - all of
//  that is already saved in imgui.ini without anybody asking. This file is for
//  the things ImGui knows nothing about: which scene, which entity, where the
//  Scene view was looking.
//
//  ==========================================================================
//  THREE THINGS ARE REMEMBERED, AND THE REASONS DIFFER
//
//    THE OPEN SCENE, by path. The whole point.
//
//    THE SELECTED ENTITY, by NAME rather than by EntityId. An id is a slot
//      plus a generation (see scene/EntityId.h) and both are rebuilt from
//      scratch when the scene loads, so last session's id either names a
//      different entity or nothing at all. A name survives a reload, which is
//      what makes it the right thing to write down - and if the name is gone
//      or duplicated, nothing is selected and no harm is done.
//
//    WHERE THE SCENE VIEW WAS LOOKING - its own camera's position and zoom,
//      not the game camera's. The game camera belongs to the scene file and is
//      restored from it; this one belongs to the person.
//
//    WHAT SHAPE THE GAME VIEW IS RENDERING AT, and how big it is drawn - the
//      aspect or resolution chosen in its dropdown, the scale, and any CUSTOM
//      entries typed into it. Same argument as the line above: which aspect
//      you are checking your UI against is a fact about you, not about the
//      level, so it does not belong in the scene file - and it has to survive
//      a restart or it is a setting nobody uses twice. The custom entries
//      most of all: retyping "1366:768" every session is worse than not
//      having been able to add it.
//
//  ==========================================================================
//  WHAT IS DELIBERATELY NOT REMEMBERED
//
//  PLAY MODE. Reopening the editor into a running game would start scripts
//  nobody asked to start, and the first thing you would see is a level already
//  in motion. Edit mode is the state the editor should always open in.
//
//  THE UNSAVED FLAG. A session file cannot restore unsaved work - only the
//  memory of having had some - and an editor that opened claiming UNSAVED with
//  nothing to save would be lying.
//
//  ==========================================================================
//  IT IS WRITTEN ON CLOSE, which means a crash or a killed process loses it.
//  That is the honest trade for not writing a file every time the camera
//  moves; the worst case is that the editor opens where it did last time
//  instead of where it was, which is a disappointment rather than a problem.
// ============================================================================

#include <string>

namespace editor {

class ScenePanel;
class GamePanel;

class EditorSession {
public:
    // Where the file lives, as a virtual path. .editor/ resolves beside
    // assets/ rather than inside it - see FileSystem::Resolve.
    static constexpr const char* kVirtualPath = ".editor/session.json";

    // Reads the file and applies what it holds. Call AFTER the panels exist
    // and the engine has started, because it may load a scene and move the
    // Scene view's camera.
    //
    // A missing or unreadable file is not an error: it means a first run, and
    // the editor simply opens as it always did.
    static void Restore(ScenePanel* scenePanel, GamePanel* gamePanel);

    // Writes the current state. Call BEFORE the panels are destroyed and
    // before the engine shuts down, because it reads from both.
    static void Save(ScenePanel* scenePanel, GamePanel* gamePanel);
};

} // namespace editor
