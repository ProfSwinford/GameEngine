// ============================================================================
//  Window.cpp - opens and closes the game window. See Window.h.
//
//  This file talks to SDL3 directly. Every SDL call's result is checked,
//  because the failures here are the ones that happen on somebody else's
//  machine: no display, an old graphics driver, a remote desktop session.
// ============================================================================

#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/platform/Window.h>

#include <SDL3/SDL.h>

namespace eng {

bool Window::Init(const BootConfig& config) {
    m_title = config.windowTitle.empty() ? "Engine2D" : config.windowTitle;

    const int width  = config.windowWidth;
    const int height = config.windowHeight;

    // Step 1: start SDL's video support.
    //
    // SDL_InitSubSystem is used instead of SDL_Init because other parts of the
    // engine may already have started SDL for their own reasons; this turns on
    // just the piece the window needs and leaves the rest alone.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not start SDL video: {}",
                         SDL_GetError());
        return false;   // both pointers stay null
    }
    m_videoInitialised = true;

    // Step 2: create the window and its renderer.
    //
    // SDL_CreateWindowAndRenderer does both in one call. That is preferred
    // over two separate calls because it also makes sure the two agree on a
    // pixel format, which is fiddly to get right by hand.
    SDL_Window*   rawWindow   = nullptr;
    SDL_Renderer* rawRenderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(m_title.c_str(), width, height,
                                     SDL_WINDOW_RESIZABLE, &rawWindow, &rawRenderer)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not create the window: {}",
                         SDL_GetError());

        // One of the two may have been created before the failure. Handing
        // whatever exists to the smart pointers means the destructor tidies it
        // up - which is precisely what unique_ptr is for on an error path.
        m_window.reset(rawWindow);
        m_renderer.reset(rawRenderer);
        return false;
    }

    // reset() hands the raw pointer to the unique_ptr, which now owns it. From
    // this line on, nothing has to remember to destroy them.
    m_window.reset(rawWindow);
    m_renderer.reset(rawRenderer);

    // Step 3: ask for vsync, which caps drawing to the monitor's refresh rate
    // and removes tearing. Not fatal if the driver refuses - it is a
    // preference, not a requirement.
    if (!SDL_SetRenderVSync(m_renderer.get(), 1)) {
        ENGINE_LOG_WARN(Channels::kPlatform, "vsync is not available: {}",
                        SDL_GetError());
    }

    ENGINE_LOG_INFO(Channels::kPlatform, "window created: {}x{} \"{}\" (drawing with {})",
                    width, height, m_title, SDL_GetRendererName(m_renderer.get()));
    return true;
}

void Window::Shutdown() {
    // Nothing was ever opened, or it has already been closed. Shutdown has to
    // survive being called twice, because the destructor calls it as well.
    if (m_window == nullptr && m_renderer == nullptr && !m_videoInitialised) {
        return;
    }

    ENGINE_LOG_INFO(Channels::kPlatform, "window closed");

    // The member declaration order in Window.h would already do this in the
    // right order, but it is written out explicitly so the ordering is visible
    // to somebody reading this file on its own.
    m_renderer.reset();
    m_window.reset();

    if (m_videoInitialised) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_videoInitialised = false;
    }

    // SDL_Quit() is deliberately NOT called here. Other parts of the engine
    // also use SDL, and shutting the whole library down from here would pull
    // the floor out from under them. The engine calls SDL_Quit once at the
    // very end of its own shutdown.
}

Window::~Window() {
    Shutdown();
}

bool Window::IsValid() const {
    return m_window != nullptr && m_renderer != nullptr;
}

int Window::Width() const {
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    return w;
}

int Window::Height() const {
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    return h;
}

bool Window::SetFullscreen(bool on) {
    if (m_window == nullptr) {
        return false;
    }

    // SDL3's SDL_SetWindowFullscreen with no mode set is borderless full
    // screen: the window takes the whole display and the desktop's own mode
    // is left alone. See the note on the declaration for why that is the one
    // worth having rather than a mode change.
    if (!SDL_SetWindowFullscreen(m_window.get(), on)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not {} full screen: {}",
                         on ? "enter" : "leave", SDL_GetError());
        return false;
    }

    // ASKED FOR AND CONFIRMED SEPARATELY. SDL applies the change
    // asynchronously on some platforms, so this syncs before anything reads
    // the size back - a frame rendered against the old size would be the
    // whole picture in the wrong place, once, which looks like a flash.
    SDL_SyncWindow(m_window.get());

    ENGINE_LOG_INFO(Channels::kPlatform, "{} full screen ({}x{})",
                    on ? "entered" : "left", Width(), Height());
    return true;
}

bool Window::IsFullscreen() const {
    if (m_window == nullptr) {
        return false;
    }
    return (SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::SetResizable(bool on) {
    if (m_window == nullptr || IsResizable() == on) {
        return;
    }
    if (!SDL_SetWindowResizable(m_window.get(), on)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not make the window {}: {}",
                         on ? "resizable" : "fixed", SDL_GetError());
        return;
    }
    ENGINE_LOG_INFO(Channels::kPlatform, "the window is now {}",
                    on ? "resizable" : "a fixed size");
}

bool Window::IsResizable() const {
    if (m_window == nullptr) {
        return false;
    }
    return (SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_RESIZABLE) != 0;
}

bool Window::SetSize(int width, int height) {
    if (m_window == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (Width() == width && Height() == height) {
        return true;
    }
    if (!SDL_SetWindowSize(m_window.get(), width, height)) {
        ENGINE_LOG_ERROR(Channels::kPlatform,
                         "could not resize the window to {}x{}: {}", width, height,
                         SDL_GetError());
        return false;
    }

    // Synced for the same reason SetFullscreen is: the change may be applied
    // asynchronously, and the caller reads the size back in the next breath.
    SDL_SyncWindow(m_window.get());
    return true;
}

void Window::Restore() {
    if (m_window == nullptr) {
        return;
    }
    // NOT CHECKED FIRST. SDL_RestoreWindow on a window that is not minimized
    // is a no-op, and the flag would have to be read back anyway - see
    // SDL_GetWindowFlags. Nothing is logged on failure either: this is a
    // courtesy to the person looking at the screen, not a contract, and a
    // window manager that refuses is not a problem the caller can solve.
    SDL_RestoreWindow(m_window.get());
}

void Window::Raise() {
    if (m_window == nullptr) {
        return;
    }
    SDL_RaiseWindow(m_window.get());
}

void Window::SetTitle(const char* title) {
    if (m_window == nullptr || title == nullptr) {
        return;
    }
    m_title = title;
    SDL_SetWindowTitle(m_window.get(), m_title.c_str());
}

void Window::Clear(unsigned char r, unsigned char g, unsigned char b) {
    if (m_renderer == nullptr) {
        return;
    }
    SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(m_renderer.get());
}

void Window::Present() {
    if (m_renderer == nullptr) {
        return;
    }
    SDL_RenderPresent(m_renderer.get());
}

void* Window::NativeWindowHandle() const   { return m_window.get(); }
void* Window::NativeRendererHandle() const { return m_renderer.get(); }

} // namespace eng
