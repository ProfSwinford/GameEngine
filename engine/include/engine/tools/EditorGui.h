#pragma once

// ============================================================================
//  EditorGui.h - starting and stopping the editor's user interface.
//
//  WHAT DEAR IMGUI IS AND WHY THE EDITOR USES IT
//  Dear ImGui is a library for building tool interfaces - windows, buttons,
//  sliders, tables, dockable panels. It is what the whole editor is drawn
//  with. It is used here because it needs no separate designer, no layout
//  files and no build step: a panel is a function that draws itself, and the
//  entire editor is a few hundred lines of ordinary C++.
//
//  It works in "immediate mode", which is genuinely different from the
//  interface toolkits you may have met. There is no button OBJECT and no click
//  handler. You call a function every frame, and it returns true on the frame
//  the button was clicked:
//
//      if (ImGui::Button("Reload")) { DoReload(); }
//
//  THE SPLIT: LIFECYCLE HERE, PANELS IN THE EDITOR
//  ImGui needs three things the engine owns and nobody else should touch: the
//  window and the renderer, the stream of input events, and a defined moment
//  in the frame to draw at. So the engine handles starting up, per-frame
//  begin/end, passing events along, and shutting down. The editor does nothing
//  but call ImGui functions to draw panels.
//
//  The result is that the engine never grows a panel and the editor never
//  needs to know SDL exists.
//
//  WHY THE KEYBOARD-CAPTURE FUNCTIONS ARE HERE
//  When a text box in a panel has focus, the interface has to SWALLOW the
//  keyboard - otherwise typing an entity's name into the Inspector also makes
//  the player jump. WantsKeyboard() and WantsMouse() expose that without
//  making every caller include an ImGui header.
//
//  WHEN THE EDITOR IS NOT BEING BUILT (ENGINE_WITH_IMGUI is not defined),
//  every function below becomes an empty inline no-op and no ImGui code is
//  compiled or linked at all. That is what lets the standalone game call
//  EventPump::Poll - which asks these functions about capture - in a build
//  with no interface library in it.
// ============================================================================

namespace eng {

class Window;

class EditorGui {
public:
#ifdef ENGINE_WITH_IMGUI
    // Creates the ImGui context, turns docking on, and starts BOTH of ImGui's
    // backends - one that speaks to SDL for input, one that speaks to SDL's
    // renderer for drawing. Forgetting the second gives you a program that
    // runs perfectly and draws nothing.
    static bool Init(Window& window);

    // The exact reverse of Init.
    static void Shutdown();

    // Hands one input event to the interface. Called from EventPump::Poll.
    // Returns true when ImGui did something with it.
    static bool ProcessEvent(const void* sdlEvent);

    // Starts a frame. Call before any panel draws.
    static void BeginFrame();

    // Draws everything the panels queued up. Call AFTER the game has been
    // drawn and BEFORE the frame is shown, so the interface lands on top.
    static void EndFrame();

    static bool WantsKeyboard();
    static bool WantsMouse();

    // Sets up the full-window docking area that panels are arranged in, and
    // builds the default layout the first time the editor is ever run. ImGui
    // remembers any rearrangement in a file called imgui.ini.
    static void BeginDockspace();
    static void EndDockspace();

    // Hands the keyboard to the GAME rather than to the editor.
    //
    // Two things have to happen together here, and missing either one gives
    // you a Game view that looks focused and does not respond: ImGui's
    // keyboard NAVIGATION has to be switched off (or the arrow keys move
    // between widgets instead of moving the player), and WantsKeyboard has to
    // stop reporting capture (or every key is marked as claimed and the input
    // system skips it).
    static void SetGameInputFocus(bool focused);
    static bool HasGameInputFocus();

    static bool IsInitialised();
#else
    // The do-nothing versions, used when the editor is not part of the build.
    static bool Init(Window&)             { return true; }
    static void Shutdown()                {}
    static bool ProcessEvent(const void*) { return false; }
    static void BeginFrame()              {}
    static void EndFrame()                {}
    static bool WantsKeyboard()           { return false; }
    static bool WantsMouse()              { return false; }
    static void BeginDockspace()          {}
    static void EndDockspace()            {}
    static void SetGameInputFocus(bool)   {}
    static bool HasGameInputFocus()       { return false; }
    static bool IsInitialised()           { return false; }
#endif
};

} // namespace eng
