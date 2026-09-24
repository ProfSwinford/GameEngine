#include <engine/platform/Window.h>
#include <engine/core/Config.h>
#include <engine/core/Log.h>

#include <SDL3/SDL.h>

namespace eng {

bool Window::Init(const BootConfig& config) {
    m_title = config.windowTitle.empty() ? "Orion Engine" : config.windowTitle;

    const int width = config.windowWidth;
    const int height = config.windowHeight;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not start SDL vdieo: {}", SDL_GetError());
        return false;
    }

    m_videoinitialized = true;

    SDL_Window* rawWindow = nullptr;
    SDL_Renderer* rawRenderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(m_title.c_str(), width, height, SDL_WINDOW_RESIZABLE,
                                     &rawWindow, &rawRenderer)) {
        ENGINE_LOG_ERROR(Channels::kPlatform, "could not create the window: {}", SDL_GetError());
        
        m_window.reset(rawWindow);
        m_renderer.reset(rawRenderer);
        return false;
    }

    m_window.reset(rawWindow);
    m_renderer.reset(rawRenderer);
    
    if (!SDL_SetRenderVSync(m_renderer.get(), 1)) {
        ENGINE_LOG_WARN(Channels::kPlatform, "vsync is not available: {}", SDL_GetError());
    }

    ENGINE_LOG_INFO(Channels::kPlatform, "window created: {}x{} \"{}\" (drawing with {})", width, height, m_title, SDL_GetRendererName(m_renderer.get()));

    return true;
}

void Window::Shutdown() {
    if (m_window == nullptr && m_renderer == nullptr && !m_videoinitialized) {
        return;
    }

    m_renderer.reset();
    m_window.reset();

    if (m_videoinitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_videoinitialized = false;
    }

    ENGINE_LOG_INFO(Channels::kPlatform, "window closed");
}

// Closes the window. The renderer has to go first, which is the order the
// members are declared in - see Window.h.
Window::~Window() {
    Shutdown();
}

// Did the window actually open? Start-up stops here if it did not.
bool Window::IsValid() const {
    return m_window != nullptr && m_renderer != nullptr;
}

// How wide the window is, in pixels.
int Window::Width() const {
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    return w;
}

// How tall the window is, in pixels.
int Window::Height() const {
    int w = 0;
    int h = 0;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }
    return h;
}

// Changes the text in the window's title bar.
void Window::SetTitle(const char* title) {
    if (m_window == nullptr || title == nullptr) {
        return;
    }
    m_title = title;
    SDL_SetWindowTitle(m_window.get(), m_title.c_str());
}

// Fills the whole window with one colour, wiping last frame's picture.
void Window::Clear(unsigned char r, unsigned char g, unsigned char b) {
    if (m_renderer == nullptr) {
        return;
    }
    SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(m_renderer.get());
}

// Shows whatever has been drawn since the last Clear.
void Window::Present() {
    if (m_renderer == nullptr) {
        return;
    }
    SDL_RenderPresent(m_renderer.get());
}

// The underlying SDL window, as a plain pointer. Only the editor needs this, to
// attach its interface - which is why it is handed out without naming SDL.
void* Window::NativeWindowHandle() const { return m_window.get(); }

// The underlying SDL renderer, handed out for the same reason.
void* Window::NativeRendererHandle() const { return m_renderer.get(); }

} // namespace eng
