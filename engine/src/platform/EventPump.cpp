// WEEK 2 - EventPump. See EventPump.h.
//
// Everything SDL lives in this file. Nothing SDL escapes into the header. The
// moment you want to put an SDL type in EventPump.h, that is the abstraction
// leaking, and the bill arrives in Week 8.

#include <engine/core/Assert.h>
#include <engine/platform/EventPump.h>
#include <engine/tools/EditorGui.h>

#include <SDL3/SDL.h>

#include <iterator>

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
    }
    return "?";
}

void EventPump::Poll() {
    m_events.clear();      // keeps capacity - see the storage note in the header
    m_consumed.clear();
    m_quitRequested = false;

    // Reserve once, on the first frame. After that clear() has left the
    // capacity in place and this is a no-op.
    if (m_events.capacity() == 0) {
        m_events.reserve(64);
        m_consumed.reserve(64);
    }

    SDL_Event sdlEvent;

    // Drain until EMPTY. Not once. Not "a few". Until SDL_PollEvent returns
    // false.
    while (SDL_PollEvent(&sdlEvent)) {
        // The GUI gets first look, because a text field with focus must be
        // able to swallow the keyboard.
        const bool guiHandled = EditorGui::ProcessEvent(&sdlEvent);

        RawEvent event;
        bool     recognised = true;

        switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
                event.kind      = RawEventKind::Quit;
                m_quitRequested = true;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                event.kind      = RawEventKind::Quit;
                m_quitRequested = true;
                break;

            case SDL_EVENT_KEY_DOWN:
                // repeat events are dropped: "held" is InputMap's job, derived
                // from state across frames, not from the OS key-repeat rate,
                // which is a user preference and differs per machine.
                if (sdlEvent.key.repeat) {
                    recognised = false;
                    break;
                }
                event.kind = RawEventKind::KeyDown;
                event.code = static_cast<i32>(sdlEvent.key.scancode);
                break;

            case SDL_EVENT_KEY_UP:
                event.kind = RawEventKind::KeyUp;
                event.code = static_cast<i32>(sdlEvent.key.scancode);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event.kind   = RawEventKind::MouseButtonDown;
                event.code   = static_cast<i32>(sdlEvent.button.button);
                event.mouseX = sdlEvent.button.x;
                event.mouseY = sdlEvent.button.y;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                event.kind   = RawEventKind::MouseButtonUp;
                event.code   = static_cast<i32>(sdlEvent.button.button);
                event.mouseX = sdlEvent.button.x;
                event.mouseY = sdlEvent.button.y;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                event.kind   = RawEventKind::MouseMove;
                event.mouseX = sdlEvent.motion.x;
                event.mouseY = sdlEvent.motion.y;
                m_mouseX     = sdlEvent.motion.x;
                m_mouseY     = sdlEvent.motion.y;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                event.kind   = RawEventKind::MouseWheel;
                event.wheelY = sdlEvent.wheel.y;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                event.kind = RawEventKind::WindowResized;
                event.code = sdlEvent.window.data1;
                event.mouseX = static_cast<f32>(sdlEvent.window.data1);
                event.mouseY = static_cast<f32>(sdlEvent.window.data2);
                break;

            default:
                recognised = false;
                break;
        }

        if (!recognised) {
            continue;
        }

        // Which events count as consumed: the GUI's capture flags are per
        // DEVICE, so a keyboard event is consumed only when ImGui wants the
        // keyboard, and likewise for the mouse. A quit is never consumed - the
        // window close button must always work.
        bool consumed = false;
        switch (event.kind) {
            case RawEventKind::KeyDown:
            case RawEventKind::KeyUp:
                consumed = guiHandled && EditorGui::WantsKeyboard();
                break;
            case RawEventKind::MouseButtonDown:
            case RawEventKind::MouseButtonUp:
            case RawEventKind::MouseMove:
            case RawEventKind::MouseWheel:
                consumed = guiHandled && EditorGui::WantsMouse();
                break;
            default:
                consumed = false;
                break;
        }

        m_events.push_back(event);
        m_consumed.push_back(consumed ? u8{1} : u8{0});

        const auto slot = static_cast<usize>(event.kind);
        if (slot < std::size(m_totals)) {
            ++m_totals[slot];
        }
    }

    // Keep the cached cursor position current even when nothing moved this
    // frame - the viewport panel's world-space readout wants it every frame.
    float x = 0.0f, y = 0.0f;
    SDL_GetMouseState(&x, &y);
    m_mouseX = x;
    m_mouseY = y;
}

usize EventPump::Count() const {
    return m_events.size();
}

const RawEvent& EventPump::At(usize index) const {
    // Week 2 said "undefined, and I said so". Week 3 promised to make it an
    // assert; here it is, with a release-build fallback that returns an
    // obviously-inert event rather than reading past the end.
    ENGINE_ASSERT_MSG(index < m_events.size(), "EventPump::At index out of range");
    if (index >= m_events.size()) {
        static const RawEvent kNone{};
        return kNone;
    }
    return m_events[index];
}

bool EventPump::QuitRequested() const {
    return m_quitRequested;
}

bool EventPump::WasConsumed(usize index) const {
    return index < m_consumed.size() && m_consumed[index] != 0;
}

u64 EventPump::TotalOfKind(RawEventKind kind) const {
    const auto slot = static_cast<usize>(kind);
    return (slot < std::size(m_totals)) ? m_totals[slot] : 0;
}

void EventPump::ResetTotals() {
    for (u64& total : m_totals) {
        total = 0;
    }
}

const char* EventPump::KeyName(i32 code) {
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
    return (name != nullptr && name[0] != '\0') ? name : "?";
}

i32 EventPump::KeyCodeFromName(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    const SDL_Scancode code = SDL_GetScancodeFromName(name);
    return (code == SDL_SCANCODE_UNKNOWN) ? -1 : static_cast<i32>(code);
}

i32 EventPump::MouseButtonFromName(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    if (SDL_strcasecmp(name, "Left") == 0)   { return SDL_BUTTON_LEFT; }
    if (SDL_strcasecmp(name, "Right") == 0)  { return SDL_BUTTON_RIGHT; }
    if (SDL_strcasecmp(name, "Middle") == 0) { return SDL_BUTTON_MIDDLE; }
    return -1;
}

} // namespace eng
