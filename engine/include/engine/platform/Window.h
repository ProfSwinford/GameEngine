#pragma once

// ============================================================================
//  Window.h - the actual operating-system window the game appears in.
//
//  One of these exists for the whole program. It owns two things that SDL
//  gives us: the window itself, and the "renderer" attached to it (SDL's name
//  for the object that draws into a window).
//
//  NOTICE WHAT IS NOT IN THIS HEADER: the word SDL. Nothing outside the
//  platform layer needs to know which library opens the window, and keeping
//  that name out of the public headers is what would make swapping SDL for
//  something else an edit to a handful of .cpp files rather than to the whole
//  engine.
//
//  WHY COPYING A WINDOW IS FORBIDDEN
//  A Window owns a resource that belongs to the operating system. If you could
//  copy one, both copies would refer to the SAME OS window, and both would try
//  to close it when they were destroyed - closing something twice. There is no
//  sensible meaning for "a second copy of this window", so the class refuses
//  to compile the attempt (see the `= delete` lines below).
// ============================================================================

#include <engine/core/Subsystem.h>
#include <engine/platform/SdlHandles.h>

#include <string>

namespace eng {

class Window : public Subsystem {
public:
    // A window object starts out closed. Init is what opens it.
    Window() = default;

    // Opens a window of the size and title in the settings.
    //
    // If anything fails - no display attached, a driver problem - the object
    // is left INVALID rather than half-built, an explanation is written to the
    // log, and false comes back. No exception is thrown: a display that will
    // not open is a problem with the machine, not a bug in the code, and the
    // engine should be able to react to it and exit tidily.
    bool Init(const BootConfig& config) override;

    // Closes the renderer first and then the window, in that order. A window
    // destroyed out from under its own renderer is a crash. Doing this twice
    // is harmless.
    void Shutdown() override;

    // Closes the window if it is somehow still open. In the engine, the
    // ordered shutdown has already called Shutdown by the time this runs.
    ~Window() override;

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool IsValid() const;

    int  Width() const;
    int  Height() const;
    void SetTitle(const char* title);

    // ======================================================================
    //  FULL SCREEN, which the engine could not do at all until now.
    //
    //  BORDERLESS FULL SCREEN - the window grows to the size of the display
    //  it is on and keeps the desktop's own mode. The alternative, changing
    //  the display's resolution, is slower to enter and leave, flickers every
    //  other window on the machine, and is the wrong tool once a game can pin
    //  its own render size: see render/Presentation.h, which is how a 640x360
    //  game fills a 4K screen without asking the monitor to change.
    //
    //  AND LETTERBOXING IS WHAT MAKES IT USABLE. A pinned aspect in full
    //  screen on a differently-shaped display is the one case with nowhere to
    //  put the spare space, so Presentation fills it with black.
    //
    //  Returns false when the request could not be carried out, which is
    //  reported rather than silent: a full-screen toggle that does nothing
    //  looks like a dead key.
    // ======================================================================
    bool SetFullscreen(bool on);
    bool IsFullscreen() const;

    // ======================================================================
    //  MAY THE PLAYER DRAG THE WINDOW TO ANY SIZE?
    //
    //  Changed after creation rather than only as a creation flag, because
    //  the setting that decides it is read from the project's settings file
    //  AFTER the window exists - and because the editor must be able to leave
    //  its own window alone while the game it hosts does not. See
    //  Engine::Options::gameOwnsTheWindow.
    //
    //  OFF IS NOT THE SAME AS FIXED-AT-A-SIZE. This stops the player dragging
    //  the edges; it does not choose a size. The size comes from
    //  "window" -> "width"/"height", and if a game pins a render resolution
    //  it almost certainly wants the window to match - the two are separate
    //  keys and nothing makes them agree, which is worth knowing because a
    //  720p render in a locked 1280x800 window is black bars for ever.
    // ======================================================================
    void SetResizable(bool on);
    bool IsResizable() const;

    // ======================================================================
    //  PUT THE WINDOW BACK TO A GIVEN SIZE.
    //
    //  THIS EXISTS BECAUSE OF A DETAIL IN SDL'S OWN DOCUMENTATION. A window
    //  CREATED resizable is constrained "to the dimensions recommended by the
    //  compositor to fit it within the usable desktop space" - so a project
    //  asking for 1280x720 on a 1366x768 laptop is quietly given something
    //  smaller, and a lock applied afterwards pins it at the smaller size.
    //  SDL's advice is to clear the flag after creation if a specific size is
    //  wanted; this is the other half of taking that advice, asking for the
    //  size back once the constraint is gone.
    //
    //  Returns false when SDL refused. What actually resulted is whatever
    //  Width() and Height() say afterwards, which is not necessarily what was
    //  asked for - nothing can give a 4K window to a 1080p display.
    // ======================================================================
    bool SetSize(int width, int height);

    // Fills the whole window with one colour. Each channel is 0-255, which is
    // why they are `unsigned char` - that type holds exactly 0 to 255.
    void Clear(unsigned char r, unsigned char g, unsigned char b);

    // Shows the finished frame. Nothing drawn this frame is visible until this
    // is called.
    void Present();

    // ---------------------------------------------------------------------
    //  Engine-internal access to the underlying SDL objects.
    //
    //  Three parts of the engine legitimately need them: the editor GUI layer,
    //  the drawing layer, and the texture loader. All three live inside the
    //  engine.
    //
    //  They are returned as `void*` on purpose. Naming the SDL types here
    //  would put SDL back into the engine's public interface, which is exactly
    //  what this header is arranged to avoid. A `void*` says "this is plumbing,
    //  not part of the API" about as loudly as C++ can.
    //
    //  Game code never needs either of these.
    // ---------------------------------------------------------------------
    void* NativeWindowHandle() const;
    void* NativeRendererHandle() const;

private:
    // These two are declared in this order for a reason. C++ destroys members
    // in the REVERSE of their declaration order, so m_renderer (declared
    // second) is destroyed first - which is the order SDL requires.
    WindowPtr   m_window;
    RendererPtr m_renderer;

    bool        m_videoInitialised = false;
    std::string m_title            = "Engine2D";
};

} // namespace eng
