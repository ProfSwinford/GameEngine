// =============================================================================
//  EventPump.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. EventPump.h is the specification; read it first.
// =============================================================================

#include <engine/platform/EventPump.h>
#include <engine/tools/GuiHooks.h>
#include <engine/core/Log.h>
#include <SDL3/SDL.h>


namespace eng {

// Turns an event kind into a readable name, for the log.
const char* ToString(RawEventKind kind) {
    switch (kind) {
        using enum RawEventKind;
        case None:              return "None";
        case Quit:              return "Quit";
        case KeyDown:           return "KeyDown";
        case KeyUp:             return "KeyUp";
        case MouseButtonDown:   return "MouseButtonDown";
        case MouseButtonUp:     return "MouseButtonUp";
        case MouseMove:         return "MouseMove";
        case MouseWheel:        return "MouseWheel";
        case WindowResized:     return "WindowResized";
        case WindowFocusGained: return "WindowFocusGained";
        case WindowFocusLost:   return "WindowFocusLost";
    }
        return "?";
}

// Empties the operating system's event queue into this object's list, once per
// frame. When a tool is attached it gets first refusal on each event, so typing
// in a text box does not also drive the game.
void EventPump::Poll() {
    m_events.clear();
    m_consumed.clear();
    m_quitRequested, m_focusGained, m_focusLost = false;

    if (m_events.capacity() == 0) {
        m_events.reserve(64);
        m_consumed.reserve(64);
    }

    const GuiHooks& gui = GetGuiHooks();

    SDL_Event sdlEvent;

    while (SDL_PollEvent(&sdlEvent)) {
        const bool guiHandled = (gui.ProcessEvent != nullptr) && gui.ProcessEvent(&sdlEvent);

        RawEvent event;
        bool recognized = true;

        switch (sdlEvent.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            event.kind = RawEventKind::Quit;
            m_quitRequested = true;
            break;
        case SDL_EVENT_KEY_DOWN: 
            if (sdlEvent.key.repeat){
                recognized = false;
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
            event.kind = RawEventKind::MouseButtonDown;
            event.code = static_cast<int>(sdlEvent.button.button);
            event.mouseX = sdlEvent.button.x;
            event.mouseY = sdlEvent.button.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event.kind = RawEventKind::MouseButtonUp;
            event.code = static_cast<int>(sdlEvent.button.button);
            event.mouseX = sdlEvent.button.x;
            event.mouseY = sdlEvent.button.y;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            event.kind = RawEventKind::MouseMove;
            event.mouseX = sdlEvent.motion.x;
            event.mouseY = sdlEvent.motion.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            event.kind = RawEventKind::MouseWheel;
            event.wheelY = sdlEvent.wheel.y;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            event.kind = RawEventKind::WindowResized;
            event.mouseX = static_cast<float>(sdlEvent.window.data1); // new width
            event.mouseY = static_cast<float>(sdlEvent.window.data2); // new height
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            event.kind = RawEventKind::WindowFocusGained;
            m_focusGained = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            event.kind = RawEventKind::WindowFocusLost;
            m_focusLost = true;
            break;
        default:
            recognized = false; // Unhandled or unrecognized event type
        }

        if (!recognized) {
            continue;
        }
    
        bool consumed = false;
        switch (event.kind) {
        case RawEventKind::KeyDown:
        case RawEventKind::KeyUp:
            consumed = guiHandled && gui.WantsKeyboard != nullptr && gui.WantsKeyboard();
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

    float x, y = 0.0;
    SDL_GetMouseState(&x, &y);
    m_mouseX = x;
    m_mouseY = y;
}

// How many events arrived this frame.
std::size_t EventPump::Count() const {
    return m_events.size();
}

// One event from this frame's list.
const RawEvent& EventPump::At(std::size_t index) const {
    if (index >= m_events.size()) {
        ENGINE_LOG_WARN(Channels::kInput, "EventPump::At({}) is past the end of {} events", index, m_events.size());
        static const RawEvent kNone{};
        return kNone;
    }
    return m_events[index];
}

// Did the user ask to close the window this frame?
bool EventPump::QuitRequested() const {
    return m_quitRequested;
}

// Was this event already claimed by a tool? Game code checks this before acting
// on it.
bool EventPump::WasConsumed(std::size_t index) const {
    return index < m_consumed.size() && m_consumed[index] != 0;
    ;
}

// The readable name of a key code, so the settings file can say "Key.Space"
// rather than a number.
const char* EventPump::KeyName(int code) {
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
    return (name != nullptr && name[0] != '\0') ? name : "?";
}

// The reverse: turns a name from the settings file into a key code. Returns a
// negative number when the name is not a key.
int EventPump::KeyCodeFromName(const char* /*name*/) {
    return -1;
}

// The same, for mouse buttons: Left, Right or Middle.
int EventPump::MouseButtonFromName(const char* /*name*/) {
    return -1;
}

} // namespace eng
