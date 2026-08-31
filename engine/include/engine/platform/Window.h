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

#include <engine/platform/SdlHandles.h>

#include <string>

namespace eng {

class Window {
public:
    // Opens a window of the given size with the given title.
    //
    // If anything fails - no display attached, a driver problem - the object
    // is left INVALID rather than half-built, an explanation is written to the
    // log, and IsValid() returns false. No exception is thrown: a display that
    // will not open is a problem with the machine, not a bug in the code, and
    // the caller should be able to react to it and exit tidily.
    Window(const char* title, int width, int height);

    // Closes the renderer first and then the window, in that order. A window
    // destroyed out from under its own renderer is a crash.
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool IsValid() const;

    int  Width() const;
    int  Height() const;
    void SetTitle(const char* title);

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
    std::string m_title;
};

} // namespace eng
