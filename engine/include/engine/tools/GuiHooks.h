#pragma once

// ============================================================================
//  GuiHooks.h - the one place the engine lets a tool see input first.
//
//  WHY THIS EXISTS
//  When a text box in the editor has keyboard focus, that key press must NOT
//  also reach the game - otherwise typing an entity's name into the Inspector
//  makes the player jump. So something has to look at every event before the
//  input system does, and say "I claimed that one".
//
//  That something is Dear ImGui, and ImGui lives in the EDITOR, not in the
//  engine. The engine cannot call it directly without depending on the tool
//  that is built on top of it, which is backwards.
//
//  The answer is three function pointers. The engine calls them if they have
//  been set and carries on if they have not, so:
//
//    * the editor fills them in at start-up and gets first look at input
//    * the game leaves them empty and every event reaches it unfiltered
//
//  Nothing else in the engine knows the editor exists.
//
//  WHY PLAIN FUNCTION POINTERS RATHER THAN std::function
//  There is exactly one implementation, it is set once at start-up, and it is
//  called for every input event. A plain pointer is the simplest thing that
//  works and it makes "is this set?" a null check anyone can read.
// ============================================================================

namespace eng {

struct GuiHooks {
    // Offers one platform event to the tool layer. Returns true if it did
    // something with it. The pointer is an SDL_Event; it is void* here so this
    // header does not have to mention SDL.
    bool (*ProcessEvent)(const void* platformEvent) = nullptr;

    // True while the tool layer wants the keyboard - a text box has focus.
    bool (*WantsKeyboard)() = nullptr;

    // True while the tool layer wants the mouse - the cursor is over a panel.
    bool (*WantsMouse)() = nullptr;
};

// Called once by the editor at start-up. The game never calls it.
void SetGuiHooks(const GuiHooks& hooks);

// Used by EventPump. Returns hooks full of nullptr when nothing has been set.
const GuiHooks& GetGuiHooks();

} // namespace eng
