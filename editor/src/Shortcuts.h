#pragma once

// ============================================================================
//  Shortcuts.h - the keyboard, in one table.
//
//  ==========================================================================
//  ONE TABLE, TWO READERS, AND THAT IS THE WHOLE POINT
//
//  A shortcut is two things that must agree: the key that fires it, and the
//  "Ctrl+S" a menu prints beside the item so anybody can find it. Written
//  separately they drift - somebody moves a key and the menu goes on
//  advertising the old one, which is worse than no hint at all because it is
//  confidently wrong.
//
//  So the chord and its printed form live in the same row of the same array,
//  Pressed() reads the chord and Hint() reads the string, and the menu bar
//  passes Hint(...) straight into ImGui::MenuItem's second argument. Changing
//  a key changes what the menu says, because there is nowhere else for the
//  menu to have got it from. It is the same argument ObjectMenu.h makes about
//  the object list being in one place.
//
//  ==========================================================================
//  WHAT IS NOT IN HERE: Delete, F2 AND F
//
//  Deliberately, and the reason is the same for all three: they are PLAIN KEYS
//  with no modifier, so which panel has the keyboard has to decide what they
//  mean. Delete removes the selected entities in the Hierarchy and the Scene
//  view, and a FILE in the Assets panel - the same key, two different and
//  equally correct answers, chosen by focus. F2 renames whatever that panel
//  is showing. F frames the selection, but only while the Scene view is
//  hovered.
//
//  A global table cannot make that choice, so those keys stay where the
//  context is, next to the thing they act on. The Edit menu still prints
//  "Del" beside its Delete item, because that is true and the menu is where
//  people look - but the hint there is a literal rather than a row in this
//  table, which is the one place the no-drift rule above is knowingly broken.
//
//  ==========================================================================
//  AND WHAT DOES NOT GET A SHORTCUT AT ALL
//
//  Build Game, and quitting.
//
//  Build Game opens an operating-system folder dialog and then compiles and
//  copies a whole game. It is a once-a-release action, and a chord away from
//  something you type all day is a dialog in your face for no reason.
//
//  Quitting has no shortcut because this editor does not ask before closing a
//  scene with unsaved work in it - only before switching PROJECT. Until it
//  does, Ctrl+Q would be a single keystroke that silently discards an hour,
//  and adding the keystroke before the question is the wrong order.
// ============================================================================

namespace editor {
namespace Shortcuts {

enum class Action {
    NewScene,
    OpenScene,
    SaveScene,
    SaveSceneAs,
    Undo,
    Redo,
    Duplicate,
    SelectAll,
    DeselectAll,
    PlayStop,
    PauseResume,
    Step,
};

// What a menu should print beside the item - "Ctrl+Shift+S". Never null, so it
// can go straight into ImGui::MenuItem.
const char* Hint(Action action);

// Was this action's chord pressed this frame?
//
// NOTHING FIRES WHILE THE KEYBOARD BELONGS TO SOMETHING ELSE: not while a text
// box has it, and not while any popup or menu is open. Ctrl+Z reaching the
// scene from behind a modal dialog is how a dialog comes to feel haunted.
//
// Exact modifiers, so Ctrl+Shift+S is not also a Ctrl+S. That comes free from
// ImGui::IsKeyChordPressed, which compares the whole modifier state rather
// than testing for the ones it wants.
bool Pressed(Action action);

} // namespace Shortcuts
} // namespace editor
