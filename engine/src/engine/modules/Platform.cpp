// ============================================================================
//  Platform.cpp - the operating-system window, the events it sends, and the SDL handles.
//  See Platform.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Platform.h>
#include <engine/modules/Settings.h>

#include <SDL3/SDL.h>

// ============================================================================
//  SdlHandles.cpp - the four clean-up functions declared in SdlHandles.h.
//
//  This is the ONLY file in the engine's platform layer that includes the full
//  SDL header, which is what keeps SDL out of every other file. Each function
//  below simply calls the matching SDL_Destroy* function.
//
//  None of them checks for null first. That is not an oversight: SDL's destroy
//  functions are documented to accept a null pointer and do nothing, so a
//  window that failed to open still cleans up correctly.
// ============================================================================



namespace eng {

void SdlWindowDeleter::operator()(SDL_Window* window) const noexcept {
    SDL_DestroyWindow(window);
}

void SdlRendererDeleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

void SdlSurfaceDeleter::operator()(SDL_Surface* surface) const noexcept {
    SDL_DestroySurface(surface);
}

void SdlTextureDeleter::operator()(SDL_Texture* texture) const noexcept {
    SDL_DestroyTexture(texture);
}

} // namespace eng


// ============================================================================
//  Window.cpp - opens and closes the game window. See Window.h.
//
//  This file talks to SDL3 directly. Every SDL call's result is checked,
//  because the failures here are the ones that happen on somebody else's
//  machine: no display, an old graphics driver, a remote desktop session.
// ============================================================================



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


// ============================================================================
//  EventPump.cpp - turns SDL events into the engine's own RawEvent list.
//
//  Every mention of SDL in the input path is in this file. The header has
//  none, so the rest of the engine reads input without knowing SDL exists.
// ============================================================================



namespace eng {

const char* ToString(RawEventKind kind) {
    switch (kind) {
        case RawEventKind::None:            return "None";
        case RawEventKind::Quit:            return "Quit";
        case RawEventKind::KeyDown:         return "KeyDown";
        case RawEventKind::KeyUp:           return "KeyUp";
        case RawEventKind::MouseButtonDown: return "MouseButtonDown";
        case RawEventKind::MouseButtonUp:   return "MouseButtonUp";
        case RawEventKind::MouseMove:       return "MouseMove";
        case RawEventKind::MouseWheel:      return "MouseWheel";
        case RawEventKind::WindowResized:   return "WindowResized";
        case RawEventKind::WindowFocusGained: return "WindowFocusGained";
        case RawEventKind::WindowFocusLost:   return "WindowFocusLost";
    }
    return "?";
}

void EventPump::Poll() {
    // clear() empties both lists but keeps the memory they already own, so
    // this function stops asking the system for memory after the first frame.
    m_events.clear();
    m_consumed.clear();
    m_quitRequested = false;
    m_focusGained   = false;
    m_focusLost     = false;

    if (m_events.capacity() == 0) {
        m_events.reserve(64);
        m_consumed.reserve(64);
    }

    // The tool layer, if one is attached. In the editor these three point at
    // ImGui; in the standalone game they are all null and every event reaches
    // the game untouched. See tools/GuiHooks.h.
    const GuiHooks& gui = GetGuiHooks();

    SDL_Event sdlEvent;

    // Keep going until SDL_PollEvent reports there is nothing left. Reading
    // only one event per frame would make input fall further and further
    // behind whenever several things happened at once.
    while (SDL_PollEvent(&sdlEvent)) {
        // The editor's interface sees every event first, so that a text box
        // with focus can claim the keyboard.
        const bool guiHandled =
            (gui.ProcessEvent != nullptr) && gui.ProcessEvent(&sdlEvent);

        RawEvent event;
        bool     recognised = true;

        switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                event.kind      = RawEventKind::Quit;
                m_quitRequested = true;
                break;

            case SDL_EVENT_KEY_DOWN:
                // The operating system sends repeated KeyDown events while a
                // key is held. Those are thrown away here: how long a key has
                // been held is worked out by InputMap from one frame to the
                // next, which behaves the same on every machine. The OS repeat
                // rate is a personal setting and differs from user to user.
                if (sdlEvent.key.repeat) {
                    recognised = false;
                    break;
                }
                event.kind = RawEventKind::KeyDown;
                event.code = static_cast<int>(sdlEvent.key.scancode);
                break;

            case SDL_EVENT_KEY_UP:
                event.kind = RawEventKind::KeyUp;
                event.code = static_cast<int>(sdlEvent.key.scancode);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event.kind   = RawEventKind::MouseButtonDown;
                event.code   = static_cast<int>(sdlEvent.button.button);
                event.mouseX = sdlEvent.button.x;
                event.mouseY = sdlEvent.button.y;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                event.kind   = RawEventKind::MouseButtonUp;
                event.code   = static_cast<int>(sdlEvent.button.button);
                event.mouseX = sdlEvent.button.x;
                event.mouseY = sdlEvent.button.y;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                event.kind   = RawEventKind::MouseMove;
                event.mouseX = sdlEvent.motion.x;
                event.mouseY = sdlEvent.motion.y;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                event.kind   = RawEventKind::MouseWheel;
                event.wheelY = sdlEvent.wheel.y;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                event.kind   = RawEventKind::WindowResized;
                event.mouseX = static_cast<float>(sdlEvent.window.data1);   // new width
                event.mouseY = static_cast<float>(sdlEvent.window.data2);   // new height
                break;

            // "Keyboard focus" is the operating system's idea of which window
            // the user is typing into - which is what changes when somebody
            // alt-tabs away and back. The editor watches for the "gained" one
            // to notice that scripts may have been edited while it was in the
            // background.
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                event.kind    = RawEventKind::WindowFocusGained;
                m_focusGained = true;
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                event.kind  = RawEventKind::WindowFocusLost;
                m_focusLost = true;
                break;

            default:
                // Plenty of SDL event types are of no interest here. Ignoring
                // them keeps the list short and meaningful.
                recognised = false;
                break;
        }

        if (!recognised) {
            continue;
        }

        // Whether the GUI claimed this event is decided per DEVICE: a key
        // press is claimed only when the GUI wants the keyboard, and a click
        // only when it wants the mouse. A Quit is never claimed - the window's
        // close button has to work no matter what has focus.
        bool consumed = false;
        switch (event.kind) {
            case RawEventKind::KeyDown:
            case RawEventKind::KeyUp:
                consumed = guiHandled && gui.WantsKeyboard != nullptr &&
                           gui.WantsKeyboard();
                break;
            case RawEventKind::MouseButtonDown:
            case RawEventKind::MouseButtonUp:
            case RawEventKind::MouseMove:
            case RawEventKind::MouseWheel:
                consumed = guiHandled && gui.WantsMouse != nullptr && gui.WantsMouse();
                break;
            default:
                consumed = false;
                break;
        }

        m_events.push_back(event);
        m_consumed.push_back(consumed ? char{1} : char{0});
    }

    // Ask SDL where the cursor is even if it did not move this frame, so that
    // anything reading MouseX/MouseY always gets a current answer.
    float x = 0.0f;
    float y = 0.0f;
    SDL_GetMouseState(&x, &y);
    m_mouseX = x;
    m_mouseY = y;
}

std::size_t EventPump::Count() const {
    return m_events.size();
}

const RawEvent& EventPump::At(std::size_t index) const {
    if (index >= m_events.size()) {
        ENGINE_LOG_WARN(Channels::kInput,
                        "EventPump::At({}) is past the end of {} events",
                        index, m_events.size());
        // `static` makes this one object that outlives the call, so returning
        // a reference to it is safe. Returning a reference to an ordinary
        // local variable would dangle the instant the function ended.
        static const RawEvent kNone{};
        return kNone;
    }
    return m_events[index];
}

bool EventPump::QuitRequested() const {
    return m_quitRequested;
}

bool EventPump::WasConsumed(std::size_t index) const {
    return index < m_consumed.size() && m_consumed[index] != 0;
}

const char* EventPump::KeyName(int code) {
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
    return (name != nullptr && name[0] != '\0') ? name : "?";
}

int EventPump::KeyCodeFromName(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    const SDL_Scancode code = SDL_GetScancodeFromName(name);
    return (code == SDL_SCANCODE_UNKNOWN) ? -1 : static_cast<int>(code);
}

int EventPump::MouseButtonFromName(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    // SDL_strcasecmp compares ignoring capitalisation, so "left" and "Left"
    // both work in a config file.
    if (SDL_strcasecmp(name, "Left") == 0)   { return SDL_BUTTON_LEFT; }
    if (SDL_strcasecmp(name, "Right") == 0)  { return SDL_BUTTON_RIGHT; }
    if (SDL_strcasecmp(name, "Middle") == 0) { return SDL_BUTTON_MIDDLE; }
    return -1;
}

} // namespace eng


// ============================================================================
//  GuiHooks.cpp - storage for the three function pointers. See GuiHooks.h.
// ============================================================================


namespace eng {
namespace {

// Starts out full of nullptr, which is exactly what "no tool layer is
// attached" should look like. A game never touches this.
GuiHooks g_hooks;

} // namespace

void SetGuiHooks(const GuiHooks& hooks) { g_hooks = hooks; }

const GuiHooks& GetGuiHooks() { return g_hooks; }

} // namespace eng
