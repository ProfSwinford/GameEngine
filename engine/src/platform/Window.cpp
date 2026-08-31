// =============================================================================
//  WEEK 1 / WEEK 3 - the window.
//
//  Every function declared in Window.h is defined here, or the LINKER rejects
//  the build - not the compiler. The Ch. 2.2 exercise (comment out Present's
//  definition and read the error) is written up in docs/week01-notes.md.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/platform/Window.h>

#include <SDL3/SDL.h>

namespace eng {

Window::Window(const char* title, i32 width, i32 height)
    : m_title(title != nullptr ? title : "Engine2D") {
    // 1. Video subsystem. SDL_InitSubSystem rather than SDL_Init so that
    //    FileSystem, which came up before us and used SDL_GetBasePath, is not
    //    disturbed. Its return value is checked like everything else here.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "SDL_InitSubSystem(VIDEO) failed: {}",
                         SDL_GetError());
        return;   // m_window and m_renderer stay null: the object is invalid
    }
    m_videoInitialised = true;

    // 2. Window and renderer. SDL_CreateWindowAndRenderer does both in one
    //    call and gets the pixel format agreement between them right, which is
    //    the reason to prefer it over two separate calls.
    SDL_Window*   rawWindow   = nullptr;
    SDL_Renderer* rawRenderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(m_title.c_str(), width, height,
                                     SDL_WINDOW_RESIZABLE, &rawWindow, &rawRenderer)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "SDL_CreateWindowAndRenderer failed: {}",
                         SDL_GetError());
        // Either or both may have been created before the failure. Adopting
        // them into the unique_ptrs means the destructor cleans up whatever
        // exists, which is the whole reason for RAII on a failure path.
        m_window.reset(rawWindow);
        m_renderer.reset(rawRenderer);
        return;
    }

    m_window.reset(rawWindow);
    m_renderer.reset(rawRenderer);

    // Not fatal if it fails - vsync is a preference, not a requirement.
    if (!SDL_SetRenderVSync(m_renderer.get(), 1)) {
        ENGINE_LOG_WARN(Channels::kPlatform, "vsync unavailable: {}", SDL_GetError());
    }

    ENGINE_LOG_INFO(Channels::kPlatform, "Window created: {}x{} \"{}\" (renderer: {})",
                    width, height, m_title,
                    SDL_GetRendererName(m_renderer.get()));
}

Window::~Window() {
    ENGINE_LOG_INFO(Channels::kPlatform, "Window destroyed");

    // Explicit and ordered, rather than relying only on member destruction
    // order, because the ORDER IS THE POINT of this week and a reader should
    // be able to see it without knowing the reverse-declaration-order rule.
    m_renderer.reset();
    m_window.reset();

    if (m_videoInitialised) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_videoInitialised = false;
    }
    // SDL_Quit() is NOT called here. The engine calls it once, at the very end
    // of teardown, because FileSystem also touched SDL before the window
    // existed. Quitting SDL from a destructor that is not the last thing to
    // run is how you get "leaks reported inside SDL with no frame in your
    // code".
}

bool Window::IsValid() const {
    return m_window != nullptr && m_renderer != nullptr;
}

i32 Window::Width() const {
    int w = 0, h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    (void)h;
    return static_cast<i32>(w);
}

i32 Window::Height() const {
    int w = 0, h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    (void)w;
    return static_cast<i32>(h);
}

void Window::SetTitle(const char* title) {
    if (m_window == nullptr || title == nullptr) {
        return;
    }
    m_title = title;
    SDL_SetWindowTitle(m_window.get(), m_title.c_str());
}

void Window::Clear(u8 r, u8 g, u8 b) {
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

void* Window::NativeWindowHandle() const {
    return m_window.get();
}

void* Window::NativeRendererHandle() const {
    return m_renderer.get();
}

} // namespace eng
