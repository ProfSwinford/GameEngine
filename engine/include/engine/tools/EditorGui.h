#pragma once

// =============================================================================
//  WEEK 2 - the ImGui lifecycle.
//
//  THE SPLIT: LIFECYCLE IN THE ENGINE, PANELS IN THE EDITOR.
//
//  ImGui needs three things the engine owns and nobody else should touch:
//    - the window and the renderer  (behind RAII since Week 3)
//    - the platform event stream    (EventPump owns it)
//    - a defined point in the frame to render at
//
//  So the engine does init, per-frame begin/end, event forwarding, and
//  shutdown. The editor does nothing but call ImGui:: functions to draw
//  panels, using public engine APIs to get the data.
//
//  Result: the engine never grows a panel, and the editor never grows a
//  pointer to an SDL type.
//
//  ---------------------------------------------------------------------------
//  THE INPUT-CAPTURE PROBLEM, handled in Week 2 rather than in Week 8.
//
//  When a text field in a panel has focus, ImGui wants to SWALLOW the
//  keyboard - otherwise typing an entity's name also makes the player jump.
//  WantsKeyboard()/WantsMouse() expose that without making every caller
//  include an ImGui header, and EventPump::Poll consults them. Week 8's
//  InputMap sits on top of EventPump, so getting it right here means it is
//  right forever.
//
//  ---------------------------------------------------------------------------
//  WHEN ENGINE_WITH_IMGUI IS OFF, every function below is an inline no-op and
//  no ImGui code is compiled or linked. That is what lets `sandbox` be built
//  with the IDE feature switched off entirely and still call EventPump::Poll,
//  which asks these functions whether input was captured.
// =============================================================================

namespace eng {

class Window;

class EditorGui {
public:
#ifdef ENGINE_WITH_IMGUI
    // Creates the context, enables the DOCKING config flag, and initialises
    // BOTH backends - the SDL3 platform backend and the SDL_Renderer3 renderer
    // backend. Two calls; forgetting the second produces a window that runs
    // fine and draws nothing.
    //
    // Docking is not on by default. The config flag must be set explicitly or
    // no panel docks, and the symptom looks exactly like having cloned the
    // wrong ImGui branch.
    static bool Init(Window& window);

    // Shuts down renderer backend, platform backend, context - the exact
    // reverse of Init. Week 3's ordered-teardown discipline, a week early.
    static void Shutdown();

    // Hands one platform event to ImGui. Called from EventPump::Poll.
    // Returns true if ImGui processed it; combine with WantsKeyboard/
    // WantsMouse to decide whether gameplay should ignore it.
    static bool ProcessEvent(const void* sdlEvent);

    // Begins a frame. Call before any panel code runs.
    static void BeginFrame();

    // Renders every queued panel. Call AFTER the game has drawn and BEFORE the
    // frame is presented, so the IDE lands on top of the game.
    static void EndFrame();

    static bool WantsKeyboard();
    static bool WantsMouse();

    // A full-window dockspace so panels can be arranged, tabbed, and the
    // layout persisted. ImGui writes imgui.ini for that; it is in .gitignore
    // because a window arrangement is a personal preference, not source.
    static void BeginDockspace();
    static void EndDockspace();

    // Hands the keyboard to the GAME rather than to the IDE.
    //
    // Two things have to happen together, and missing either one produces a
    // Game view that looks focused and does not respond. ImGui's keyboard
    // NAVIGATION has to be switched off, or the arrow keys move focus between
    // widgets instead of moving the player; and WantsKeyboard has to stop
    // reporting capture, or EventPump marks every key consumed and InputMap
    // skips them.
    static void SetGameInputFocus(bool focused);
    static bool HasGameInputFocus();

    static bool IsInitialised();
#else
    static bool Init(Window&)         { return true; }
    static void Shutdown()            {}
    static bool ProcessEvent(const void*) { return false; }
    static void BeginFrame()          {}
    static void EndFrame()            {}
    static bool WantsKeyboard()       { return false; }
    static bool WantsMouse()          { return false; }
    static void BeginDockspace()      {}
    static void EndDockspace()        {}
    static void SetGameInputFocus(bool) {}
    static bool HasGameInputFocus()   { return false; }
    static bool IsInitialised()       { return false; }
#endif
};

} // namespace eng
