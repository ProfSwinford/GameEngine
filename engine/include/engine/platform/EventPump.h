#pragma once

// ============================================================================
//  EventPump.h - reads what the user did this frame.
//
//  The operating system collects everything the user does - key presses, mouse
//  movement, clicking the window's close button - into a queue. Once per frame
//  the EventPump empties that queue and stores what it found in a simple list
//  that the rest of the engine can look at.
//
//  WHY THIS LAYER EXISTS RATHER THAN CALLING SDL FROM THE GAME LOOP
//  Two reasons.
//
//  First, it keeps SDL in one place. Nothing in this header mentions SDL, so
//  game code, the editor, and the input system all read user input without
//  ever including an SDL header.
//
//  Second, what comes out of here is deliberately RAW and low-level: "scancode
//  44 went down". Game code should not be written against key numbers - see
//  input/InputMap.h, which turns these into named actions like "Jump" that the
//  player can rebind. This layer is the single doorway raw input comes through.
// ============================================================================

#include <vector>

namespace eng {

// The kinds of thing that can happen. Deliberately coarse: InputMap turns
// these into named actions, and nothing above that layer sees a key number
// again.
enum class RawEventKind {
    None,
    Quit,              // the user asked to close the program
    KeyDown,
    KeyUp,
    MouseButtonDown,
    MouseButtonUp,
    MouseMove,
    MouseWheel,
    WindowResized,
    WindowFocusGained,   // the user switched back to this window
    WindowFocusLost,     // the user switched away from it
};

const char* ToString(RawEventKind kind);

// One thing that happened. Which fields are meaningful depends on `kind`:
// a KeyDown fills in `code`, a MouseMove fills in `mouseX`/`mouseY`, and so on.
struct RawEvent {
    int          code   = 0;      // which key, or which mouse button
    float        mouseX = 0.0f;   // in window pixels, measured from the top-left
    float        mouseY = 0.0f;
    float        wheelY = 0.0f;   // positive is scroll up
    RawEventKind kind   = RawEventKind::None;
};

class EventPump {
public:
    // Empties the operating system's queue and records everything in it.
    // Call this exactly once per frame.
    //
    // It keeps looping until the queue reports empty. Taking only one event
    // per call would make input lag by one frame for every event still waiting
    // - a bug that is very hard to recognise months later.
    //
    // The editor's GUI gets first look at each event. When a text box has
    // keyboard focus, the GUI claims the key presses; they are still recorded
    // here (so the list is complete) but marked as consumed, and the input
    // system skips those. Without that, typing a name into the Inspector would
    // also make the player jump.
    void Poll();

    // How many events arrived in the most recent Poll().
    std::size_t Count() const;

    // Event number `index` from the most recent Poll(). An index past the end
    // returns an inert "None" event and logs a warning rather than reading
    // memory that is not there.
    const RawEvent& At(std::size_t index) const;

    // True when the user asked to close the window this frame.
    bool QuitRequested() const;

    // True on the frame the window became the one being typed into - the user
    // alt-tabbed back, or clicked on it.
    //
    // The editor uses this to notice that scripts may have been edited while
    // it was in the background. A game could use it to pause itself when the
    // player switches away.
    bool FocusGainedThisFrame() const { return m_focusGained; }
    bool FocusLostThisFrame() const   { return m_focusLost; }

    // True when the editor GUI claimed event `index`. Gameplay input ignores
    // those.
    bool WasConsumed(std::size_t index) const;

    // Where the mouse is right now, in window pixels.
    //
    // Two separate floats rather than a Vec2 so that this header does not have
    // to depend on the maths layer for one field.
    float MouseX() const { return m_mouseX; }
    float MouseY() const { return m_mouseY; }

    // Turning key numbers into readable names and back. The config file stores
    // bindings as text like "Space", and these are what translate between that
    // and the numbers the operating system actually reports.
    static const char* KeyName(int code);
    static int         KeyCodeFromName(const char* name);
    static int         MouseButtonFromName(const char* name);

private:
    // A std::vector is used here and cleared (not destroyed) each frame.
    // clear() empties the list but keeps the memory it already had, so after
    // the first busy frame no further frame has to ask for more - which keeps
    // a per-frame function from allocating.
    std::vector<RawEvent> m_events;

    // Runs alongside m_events: one entry per event saying whether the GUI
    // claimed it.
    //
    // It stores `char` rather than `bool` on purpose. std::vector<bool> is a
    // special case in the standard library that packs its values into
    // individual bits, and as a result it does not behave like other vectors.
    // Avoiding it here avoids explaining that.
    std::vector<char> m_consumed;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    bool  m_quitRequested = false;
    bool  m_focusGained   = false;
    bool  m_focusLost     = false;
};

} // namespace eng
