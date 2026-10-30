#pragma once

// ============================================================================
//  UndoStack.h - Ctrl+Z, by snapshotting the scene rather than by recording
//  what each action did.
//
//  ==========================================================================
//  WHY SNAPSHOTS AND NOT A COMMAND PATTERN
//
//  The textbook answer to undo is a command object per action, each knowing
//  how to apply and reverse itself. It is the right answer when actions are
//  few and well defined. Here they are neither: the editor changes the scene
//  from about forty places - every component's fields in the Inspector, the
//  gizmo, the Hierarchy's reparenting and renaming, the tile brush, the
//  object menus, asset drag-and-drop - and every one of them would need a
//  reverse written by hand, correctly, forever. The first one somebody forgot
//  would not fail loudly; it would make Ctrl+Z quietly wrong, which is worse
//  than having no undo at all, because by then it is trusted.
//
//  SO THE TRIGGER IS THE `dirty` FLAG AND THE RECORD IS THE WHOLE SCENE.
//  Every one of those forty places already sets EditorState::dirty, because
//  the title bar needs it - so the signal "something changed" is already
//  universal and already maintained. And `eng::Scene` can already write
//  itself to a string and read itself back: that is what makes the Play
//  button non-destructive, and it is the most heavily exercised round-trip in
//  the engine. This feature is those two facts put together.
//
//  The consequences are worth being explicit about, because they are not all
//  favourable:
//
//    IT DOES NOT NEED TELLING WHAT CHANGED. Whatever an edit did to the
//    scene, the snapshot has it - including edits written after this, and
//    kinds of edit nobody anticipated. That is the whole reason to prefer it,
//    and it is why painting tiles became undoable without a line being added
//    to any paint tool.
//
//    WHAT IT DOES NEED is to be told that SOMETHING happened, which is
//    EditorState::MarkDirty - see the note there on why the flag behind it is
//    private. So the honest statement of completeness is: anything that marks
//    the scene dirty is captured in full, and anything that changes the scene
//    WITHOUT marking it dirty is invisible. Two consequences worth naming:
//
//      A new edit site that forgets MarkDirty is a missed undo step. The
//      compiler cannot catch that one - it can only stop the flag being set
//      round the back - so it is the thing to check when adding an edit.
//
//      "Align game camera" in the Scene view, on a scene with no camera
//      entity, writes the engine's camera directly. There is nothing in the
//      scene document to snapshot, so undo cannot reach it. It is the one
//      known gap, and the editor guide says so.
//
//    THERE ARE NO PER-ACTION LABELS. The menu says "Undo", not "Undo Move" -
//    because the trigger is "the scene differs from what it was", which does
//    not know what you did. A command pattern would give better labels; it
//    would also give silently wrong undo, and that is not a trade worth
//    making for a menu string.
//
//    THE GRANULARITY IS ONE GESTURE. A snapshot is taken when nothing is
//    being held down - see the note on Poll - so a gizmo drag across the
//    screen is one undo step rather than one per frame.
//
//  ==========================================================================
//  WHAT A SNAPSHOT HOLDS, AND WHY THE SELECTION IS IN IT
//
//  Reloading a scene destroys every entity and builds new ones, so every
//  EntityId changes. A selection made of ids is therefore meaningless
//  afterwards - EditorState::PruneSelection would simply drop the lot, and
//  undoing a move would leave you with the entity back where it was and
//  nothing selected, which feels broken even though the data is right.
//
//  So the selection travels as NAMES. That works because names in a scene are
//  unique and the scene file carries them: Scene::RenameEntity refuses a
//  duplicate and Scene keeps a name index, which is also what lets a script's
//  entity reference be a name. See EntityRef.h.
// ============================================================================

#include <cstddef>
#include <string>

namespace editor {
namespace Undo {

// Starts again from the scene that is open now: both stacks are emptied and
// the current state becomes the baseline.
//
// CALLED WHENEVER THE SCENE CHANGES IDENTITY - loaded, closed, or newly
// started. Undo must not step from one scene back into another one, which is
// what would happen if the stack survived a scene load.
void Reset();

// Once a frame, after the panels have drawn.
//
// Notices that the scene is no longer what it was and records the state it
// was in before. On the overwhelming majority of frames it compares one
// integer and returns.
//
// THAT INTEGER IS THE POINT. Serialising the scene is far too expensive to do
// speculatively - the measurement and the method are written down beside
// g_seenSerial in UndoStack.cpp - so this asks EditorState::EditSerial
// instead, which every edit site advances because the flag behind it is
// private. The snapshot is taken only when that counter has moved AND the
// gesture moving it has finished.
//
// TWO THINGS IT REFUSES TO DO:
//
//   NOT WHILE PLAYING. The simulation changes the scene every frame, so a
//   snapshot during play would fill the stack with steps belonging to a play
//   session that Stop is about to throw away anyway.
//
//   NOT MID-GESTURE. While a mouse button is down or an ImGui widget is
//   active, a change is still in progress: a gizmo drag and a DragFloat both
//   rewrite the scene on every frame they are held. Waiting for the gesture
//   to end is what makes one drag one undo step.
void Poll();

bool        CanUndo();
bool        CanRedo();
std::size_t UndoDepth();
std::size_t RedoDepth();

// Steps back, or forward again.
//
// Returns false with a reason - there was nothing to step to, or the snapshot
// would not load - and in that case NOTHING HAS CHANGED, including the stacks.
// A failed undo that had already discarded its step would be unrecoverable.
bool StepBack(std::string& outError);
bool StepForward(std::string& outError);

// The scene has just been written to its file, so the state it is in now is
// the saved one.
//
// Lets undo put the title bar's asterisk back correctly: stepping back to
// exactly what is on disk clears it instead of leaving the editor claiming
// unsaved work it does not have.
void NoteSaved();

} // namespace Undo
} // namespace editor
