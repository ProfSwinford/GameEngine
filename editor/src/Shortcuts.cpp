// ============================================================================
//  Shortcuts.cpp - see Shortcuts.h for why the chord and its printed form are
//  in the same row.
// ============================================================================

#include "Shortcuts.h"

#include <imgui.h>

namespace editor {
namespace Shortcuts {
namespace {

struct Entry {
    Action        action;
    ImGuiKeyChord chord;
    const char*   hint;

    // A SECOND CHORD FOR THE SAME ACTION, for the one case that genuinely has
    // two conventions: redo is Ctrl+Y on Windows and Ctrl+Shift+Z almost
    // everywhere else, and people arrive with the habit they already have.
    // Only the first is printed, because a menu listing both is a menu nobody
    // reads.
    ImGuiKeyChord also = ImGuiKey_None;
};

// ---------------------------------------------------------------------------
//  THE TABLE.
//
//  Chosen to match what these keys already do in Unity, Blender, Visual
//  Studio and every text editor, in that order of precedence - a shortcut
//  that is nearly standard is worse than one that is missing, because muscle
//  memory fires before you can stop it.
//
//  The play controls are Unity's exactly: Ctrl+P plays and stops, Ctrl+Shift+P
//  pauses, Ctrl+Alt+P takes one step.
// ---------------------------------------------------------------------------
constexpr Entry kTable[] = {
    {Action::NewScene,    ImGuiMod_Ctrl | ImGuiKey_N,                  "Ctrl+N"},
    {Action::OpenScene,   ImGuiMod_Ctrl | ImGuiKey_O,                  "Ctrl+O"},
    {Action::SaveScene,   ImGuiMod_Ctrl | ImGuiKey_S,                  "Ctrl+S"},
    {Action::SaveSceneAs, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, "Ctrl+Shift+S"},

    {Action::Undo,        ImGuiMod_Ctrl | ImGuiKey_Z,                  "Ctrl+Z"},
    {Action::Redo,        ImGuiMod_Ctrl | ImGuiKey_Y,                  "Ctrl+Y",
     ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z},

    {Action::Duplicate,   ImGuiMod_Ctrl | ImGuiKey_D,                  "Ctrl+D"},
    {Action::SelectAll,   ImGuiMod_Ctrl | ImGuiKey_A,                  "Ctrl+A"},
    {Action::DeselectAll, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_A, "Ctrl+Shift+A"},

    {Action::PlayStop,    ImGuiMod_Ctrl | ImGuiKey_P,                  "Ctrl+P"},
    {Action::PauseResume, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, "Ctrl+Shift+P"},
    {Action::Step,        ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P,   "Ctrl+Alt+P"},
};

const Entry* Find(Action action) {
    for (const Entry& entry : kTable) {
        if (entry.action == action) {
            return &entry;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  IS THE KEYBOARD OURS TO READ?
//
//  Two cases where it is not, and both are the difference between a shortcut
//  and a bug:
//
//    SOMEBODY IS TYPING. Ctrl+A in a name box selects the text, and must not
//    also select every entity in the scene.
//
//    A POPUP OR MENU IS OPEN. The Save As box, the unsaved-changes question
//    and the New Project dialog all expect the keyboard. So does an open
//    menu, which is being navigated. Any popup at any level counts - the test
//    is deliberately broad, because the cost of a shortcut not firing while a
//    menu is open is nothing, and the cost of one firing behind a modal is an
//    action nobody can see.
// ---------------------------------------------------------------------------
bool KeyboardIsOurs() {
    if (ImGui::GetIO().WantTextInput) {
        return false;
    }
    return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                            ImGuiPopupFlags_AnyPopupLevel);
}

} // namespace

const char* Hint(Action action) {
    const Entry* entry = Find(action);

    // NEVER NULL. This goes straight into ImGui::MenuItem, where a null means
    // "no hint" and would silently print nothing - so a row somebody forgot to
    // add would look like a deliberate absence rather than a mistake.
    return entry != nullptr ? entry->hint : "?";
}

bool Pressed(Action action) {
    if (!KeyboardIsOurs()) {
        return false;
    }

    const Entry* entry = Find(action);
    if (entry == nullptr) {
        return false;
    }
    if (ImGui::IsKeyChordPressed(entry->chord)) {
        return true;
    }
    return entry->also != ImGuiKey_None && ImGui::IsKeyChordPressed(entry->also);
}

} // namespace Shortcuts
} // namespace editor
