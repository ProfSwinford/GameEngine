// =============================================================================
//  Window.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Window.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/platform/Window.h>

namespace eng {

// Opens an operating-system window of the given size, and the object that draws
// into it. If anything fails the object is left INVALID rather than half-built,
// and no exception is thrown - a display that will not open is a problem with
// the machine, not a bug, and the caller should be able to exit tidily.
Window::Window(const char* /*title*/, int /*width*/, int /*height*/) {
}

// Closes the window. The renderer has to go first, which is the order the
// members are declared in - see Window.h.
Window::~Window() {
}

// Did the window actually open? Start-up stops here if it did not.
bool Window::IsValid() const {
    return false;
}

// How wide the window is, in pixels.
int Window::Width() const {
    return 0;
}

// How tall the window is, in pixels.
int Window::Height() const {
    return 0;
}

// Changes the text in the window's title bar.
void Window::SetTitle(const char* /*title*/) {
}

// Fills the whole window with one colour, wiping last frame's picture.
void Window::Clear(unsigned char /*r*/, unsigned char /*g*/, unsigned char /*b*/) {
}

// Shows whatever has been drawn since the last Clear.
void Window::Present() {
}

// The underlying SDL window, as a plain pointer. Only the editor needs this, to
// attach its interface - which is why it is handed out without naming SDL.
void* Window::NativeWindowHandle() const {
    return nullptr;
}

// The underlying SDL renderer, handed out for the same reason.
void* Window::NativeRendererHandle() const {
    return nullptr;
}

} // namespace eng
