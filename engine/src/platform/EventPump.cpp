// =============================================================================
//  EventPump.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. EventPump.h is the specification; read it first.
// =============================================================================

#include <engine/platform/EventPump.h>

namespace eng {

// Turns an event kind into a readable name, for the log.
const char* ToString(RawEventKind /*kind*/) {
    return "None";
}

// Empties the operating system's event queue into this object's list, once per
// frame. When a tool is attached it gets first refusal on each event, so typing
// in a text box does not also drive the game.
void EventPump::Poll() {
}

// How many events arrived this frame.
std::size_t EventPump::Count() const {
    return 0;
}

// One event from this frame's list.
const RawEvent& EventPump::At(std::size_t /*index*/) const {
    static const RawEvent none{};
    return none;
}

// Did the user ask to close the window this frame?
bool EventPump::QuitRequested() const {
    return false;
}

// Was this event already claimed by a tool? Game code checks this before acting
// on it.
bool EventPump::WasConsumed(std::size_t /*index*/) const {
    return false;
}

// The readable name of a key code, so the settings file can say "Key.Space"
// rather than a number.
const char* EventPump::KeyName(int /*code*/) {
    return "";
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
