// ============================================================================
//  Window.cpp - opens and closes the game window. See Window.h.
//
//  This file talks to SDL3 directly. Every SDL call's result is checked,
//  because the failures here are the ones that happen on somebody else's
//  machine: no display, an old graphics driver, a remote desktop session.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/platform/Window.h>

#include <SDL3/SDL.h>

namespace eng {

Window::Window(const char* title, int width, int height)
    : m_title(title != nullptr ? title : "Engine2D") {

    // Step 1: start SDL's video support.
    //
    // SDL_InitSubSystem is used instead of SDL_Init because other parts of the
    // engine may already have started SDL for their own reasons; this turns on
    // just the piece the window needs and leaves the rest alone.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not start SDL video: {}",
                         SDL_GetError());
        return;   // both pointers stay null, so IsValid() will report false
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
        return;
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
}

Window::~Window() {
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
    // also use SDL, and shutting the whole library down from this destructor
    // would pull the floor out from under them. The engine calls SDL_Quit once
    // at the very end of its own shutdown.
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
