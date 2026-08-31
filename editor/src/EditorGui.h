#pragma once

// ============================================================================
//  EditorGui.h - starting and stopping Dear ImGui, the library the whole
//  editor interface is drawn with.
//
//  WHY THIS LIVES IN THE EDITOR AND NOT IN THE ENGINE
//  It used to be part of the engine, and it moved here for a concrete reason:
//  the engine is now a shared library that a project's compiled scripts link
//  against, and ImGui keeps some of its state in a global variable that does
//  not survive being reached across a library boundary. Keeping ImGui in the
//  program that actually draws with it sidesteps that entirely.
//
//  It is also simply the right place. ImGui is a tool library; the engine
//  should not know it exists.
//
//  WHAT THE ENGINE STILL NEEDS FROM IT
//  One thing: first look at input, so that a text box with focus can swallow a
//  key press before the game sees it. That is arranged through three function
//  pointers the engine offers - see engine/tools/GuiHooks.h - which Init()
//  fills in below.
//
//  WHAT DEAR IMGUI IS
//  A library for building tool interfaces: windows, buttons, sliders, tables,
//  dockable panels. It works in "immediate mode", which is genuinely different
//  from most interface toolkits - there is no button object and no click
//  handler. You call a function every frame and it returns true on the frame
//  the button was clicked:
//
//      if (ImGui::Button("Reload")) { DoReload(); }
// ============================================================================

namespace eng {
class Window;
}

namespace editor {

class EditorGui {
public:
    // Creates the ImGui context, turns docking on, starts both of ImGui's
    // backends, and registers the input hooks with the engine.
    //
    // Called by the engine as part of its ordered start-up rather than
    // directly by the editor - see Engine::Options::guiInit for why.
    static bool Init(eng::Window& window);

    // The exact reverse of Init, and it clears the engine's hooks first so
    // that nothing can call into ImGui after it has gone.
    static void Shutdown();

    // Starts a frame. Call before any panel draws.
    static void BeginFrame();

    // Draws everything the panels queued up. Call AFTER the game has been
    // drawn and BEFORE the frame is shown, so the interface lands on top.
    static void EndFrame();

    // Sets up the full-window docking area the panels are arranged in, and
    // builds the default layout the first time the editor is ever run.
    static void BeginDockspace();
    static void EndDockspace();

    // Hands the keyboard to the GAME rather than to the editor.
    //
    // Two things have to happen together here, and missing either one gives
    // you a Game view that looks focused and ignores every key: ImGui's
    // keyboard NAVIGATION has to be switched off (or the arrow keys move
    // between widgets instead of moving the player), and the engine has to
    // stop being told the interface wants the keyboard (or every key is marked
    // as claimed and the input system skips it).
    static void SetGameInputFocus(bool focused);
    static bool HasGameInputFocus();

    static bool IsInitialised();
};

} // namespace editor
