// ============================================================================
//  EventPump.cpp - turns SDL events into the engine's own RawEvent list.
//
//  Every mention of SDL in the input path is in this file. The header has
//  none, so the rest of the engine reads input without knowing SDL exists.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/platform/EventPump.h>
#include <engine/tools/GuiHooks.h>

#include <SDL3/SDL.h>

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
    m_dropped.clear();
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

            // ================================================================
            //  FILES DRAGGED IN FROM THE DESKTOP.
            //
            //  Four event types for one gesture, and each of them matters:
            //
            //    BEGIN     the cursor entered the window carrying something
            //    POSITION  it moved, and this is where it is now
            //    FILE      one file was let go of - sent once PER FILE
            //    COMPLETE  the drop is finished, or the cursor left again
            //
            //  They are recorded and then dropped out of the RawEvent list
            //  rather than added to it, because a RawEvent has nowhere to put
            //  a file name. See DroppedFile in the header.
            //
            //  THE POSITION IS STAMPED ON THE FILE EVENTS TOO, by SDL, from
            //  the last POSITION it saw. That is the only reason a panel can
            //  tell a file dropped on IT from one dropped on the panel next
            //  door: the mouse does not move during somebody else's drag, so
            //  asking where the cursor is afterwards answers a different
            //  question entirely.
            // ================================================================
            case SDL_EVENT_DROP_BEGIN:
                m_dragHovering = true;
                recognised     = false;
                break;

            case SDL_EVENT_DROP_POSITION:
                m_dragHovering = true;
                m_dragX        = sdlEvent.drop.x;
                m_dragY        = sdlEvent.drop.y;
                recognised     = false;
                break;

            case SDL_EVENT_DROP_FILE:
                // COPIED, not pointed at. SDL owns that string and frees it
                // once the event has been dealt with, so keeping the pointer
                // would leave the list full of dangling ones by the time a
                // panel got round to reading it - and a dangling pointer that
                // usually still happens to hold the right text is about the
                // worst kind there is.
                if (sdlEvent.drop.data != nullptr) {
                    m_dropped.push_back(DroppedFile{std::string(sdlEvent.drop.data),
                                                    sdlEvent.drop.x, sdlEvent.drop.y});
                }
                recognised = false;
                break;

            case SDL_EVENT_DROP_COMPLETE:
                // Sent both when the user lets go AND when they drag back out
                // without dropping anything, which is what makes this the one
                // safe place to turn the highlight off. Missing it would leave
                // a panel glowing for the rest of the session.
                m_dragHovering = false;
                recognised     = false;
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
                consumed = guiHandled && gui.WantsKeyboard != nullptr &&
                           gui.WantsKeyboard();
                break;

            // ================================================================
            //  A RELEASE IS NEVER CLAIMED. Not by the GUI, not by anything.
            //
            //  Claiming an input means "I am acting on this instead of the
            //  game". That makes sense for a press, which starts something.
            //  It makes no sense for a release, which can only ever END
            //  something that has already started.
            //
            //  The bug this fixes: hold a key while play-testing, click Stop,
            //  then let go. Clicking Stop puts the keyboard focus in the GUI,
            //  so the key-up was claimed and never reached the input map -
            //  which only ever clears a key on seeing its release. The action
            //  stayed down forever, and the next play session began with the
            //  player already walking.
            //
            //  Delivering it unconditionally is safe in the other direction
            //  too. If the GUI claimed the PRESS - somebody typing in a text
            //  box - the action was never marked down, so the release finds
            //  it already up and changes nothing. No phantom Released is
            //  produced, because that state is worked out by comparing this
            //  frame with the last and both are false.
            // ================================================================
            case RawEventKind::KeyUp:
            case RawEventKind::MouseButtonUp:
                consumed = false;
                break;

            case RawEventKind::MouseButtonDown:
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
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    m_mouseX = x;
    m_mouseY = y;

    // The same reasoning for the BUTTON: an interface asks "is it held down
    // right now", which is a state rather than an event. Reconstructing it by
    // counting Down and Up events would drift the first time one was missed -
    // which is exactly the stuck-input bug that the key-release rule above
    // exists to prevent. Asking the system is immune to it.
    m_mouseLeftDown = (buttons & SDL_BUTTON_LMASK) != 0;
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
