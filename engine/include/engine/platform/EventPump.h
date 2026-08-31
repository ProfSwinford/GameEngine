#pragma once

// =============================================================================
//  WEEK 2 - the engine's first real subsystem.
//
//  It drains the platform event queue once per frame and hands out a coarse,
//  platform-free description of what it found, instead of leaving a giant
//  switch statement in main().
//
//  NO SDL TYPE APPEARS IN THIS HEADER. That is checked by the fact that
//  editor/src/panels/EventInspectorPanel.cpp reads and displays every field
//  below without including a single SDL header - which is exactly the test the
//  panel was assigned to be. It is also what makes Week 8's InputMap
//  straightforward: raw input has one door into the engine.
// =============================================================================

#include <engine/core/Types.h>

#include <vector>

namespace eng {

// Deliberately coarse. Week 8's InputMap turns these into named actions, and
// nothing above that layer ever sees a key code again.
enum class RawEventKind : u8 {
    None,
    Quit,
    KeyDown,
    KeyUp,
    MouseButtonDown,
    MouseButtonUp,
    MouseMove,
    MouseWheel,
    WindowResized,
};

const char* ToString(RawEventKind kind);

// WEEK 4 SIZEOF AUDIT SUBJECT, and one of the audit's honest NEGATIVE results.
//
// Members are ordered largest-alignment-first out of habit, and it makes no
// difference at all: this measures 20 bytes either way. Four 4-byte members
// plus one 1-byte enum is 17, rounded up to 20 for the struct's 4-byte
// alignment - and declaring `kind` first would just move the same three
// padding bytes from the end to the middle.
//
// A struct with only ONE sub-word member cannot be improved by reordering,
// because there is only ever one run of padding to place. That is worth
// knowing rather than guessing, and it is why docs/week04-sizeof-audit.md
// reports "0 bytes saved" for five of six rows and shows the arithmetic
// instead of hunting for a saving that is not there.
struct RawEvent {
    i32          code   = 0;      // key scancode, or mouse button index
    f32          mouseX = 0.0f;   // window coordinates, pixels
    f32          mouseY = 0.0f;
    f32          wheelY = 0.0f;
    RawEventKind kind   = RawEventKind::None;
};

class EventPump {
public:
    // Drains the platform queue COMPLETELY and stores what it found. Call
    // exactly once per frame.
    //
    // Draining completely means looping until the queue reports empty. One
    // event per call produces input that lags by a frame per queued event -
    // a bug that is hard to recognise months later, and the reason this is
    // stated twice.
    //
    // WEEK 2 (IMGUI): every event is offered to the editor GUI first. When the
    // GUI reports that it wants the keyboard or the mouse - a text field has
    // focus - the corresponding events are still RECORDED (so the Event
    // Inspector can show them, and so the pause toggle means something) but
    // are marked consumed, and InputMap skips consumed events. Otherwise
    // typing an entity's name into the Inspector also makes the player jump.
    void Poll();

    // How many events arrived in the most recent Poll().
    usize Count() const;

    // Event `index` from the most recent Poll().
    //
    // OUT OF RANGE: asserts in debug, and returns a static None event in
    // release rather than reading past the end. Week 2 answered this
    // "undefined, and I said so out loud"; Week 3 made it an assert as
    // promised.
    const RawEvent& At(usize index) const;

    // True if a Quit event arrived in the most recent Poll().
    bool QuitRequested() const;

    // True if the GUI swallowed event `index`. Gameplay input ignores these.
    bool WasConsumed(usize index) const;

    // Running totals per kind, for the Event Inspector. Cheap and it means the
    // panel does not have to accumulate state the engine already has.
    u64 TotalOfKind(RawEventKind kind) const;
    void ResetTotals();

    // Last known cursor position in window pixels. Deliberately two scalars
    // rather than a Vec2: this header sits below the math layer and including
    // Vec2.h here would make the platform layer depend on it for one field.
    f32 MouseX() const { return m_mouseX; }
    f32 MouseY() const { return m_mouseY; }

    // Human-readable name for a key code, so the Event Inspector and the CVar
    // panel can show "Space" instead of 44 without either of them learning
    // what a scancode is.
    static const char* KeyName(i32 code);
    // The inverse, for parsing bindings out of the config file.
    static i32 KeyCodeFromName(const char* name);
    static i32 MouseButtonFromName(const char* name);

private:
    // STORAGE NOTE (Week 2 question, answered).
    //
    // A std::vector, reserved once at construction and cleared - not freed -
    // each frame. clear() keeps the capacity, so after the first frame that
    // sees N events, no further frame allocates. Week 8's requirement is that
    // the update path performs no heap allocation after warm-up, and this
    // satisfies it without a custom container: the allocator counters in
    // docs/week08-verification.md are flat over 600 frames.
    //
    // What would change it: if events could arrive in unbounded numbers, this
    // would need a fixed-capacity ring with an overflow policy. They cannot -
    // SDL's queue is bounded by what the OS delivers in one frame.
    std::vector<RawEvent> m_events;
    std::vector<u8>       m_consumed;   // parallel; u8 rather than bool because
                                        // vector<bool> is a bitfield and hands
                                        // out proxies instead of references

    u64  m_totals[16]{};
    f32  m_mouseX = 0.0f;
    f32  m_mouseY = 0.0f;
    bool m_quitRequested = false;
};

} // namespace eng
