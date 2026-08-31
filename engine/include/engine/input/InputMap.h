#pragma once

// =============================================================================
//  WEEK 8 - the input abstraction layer. Ch. 9.5.
//
//  Turns "scancode 44 went down" into "the player wants to Jump".
//
//  The test is mechanical: grep gameplay-facing code for SDL_SCANCODE, SDLK_,
//  SDL_BUTTON. Zero results. If gameplay code knows which KEY was pressed
//  rather than what the player INTENDED, this layer is not finished. The
//  result of that grep is in docs/week08-verification.md.
//
//  Sits on top of Week 2's EventPump. Because SDL was kept out of
//  EventPump.h, this week was straightforward; the bill for leaking it would
//  have arrived here.
//
//  ---------------------------------------------------------------------------
//  ACTIONS ARE StringId, NOT AN ENUM. An enum cannot be extended by a config
//  file, and the entire point is that bindings are data. "Jump"_sid costs the
//  same as an enum at runtime and can be written by a designer.
//
//  ---------------------------------------------------------------------------
//  CONTEXTS: A STACK, not a single active context. Decided and recorded.
//
//  A pause menu over gameplay wants menu bindings on top with gameplay still
//  underneath - Escape should close the menu, and the gameplay binding for
//  Escape should not fire while it is open.
//
//  RESOLUTION ORDER, stated explicitly because "input works in menus but also
//  fires in gameplay" is the bug that comes from leaving it vague:
//
//    A physical key is resolved against contexts from the TOP of the stack
//    DOWNWARD, and the FIRST context that binds that key wins. Lower contexts
//    do not also see it. A context therefore SHADOWS the ones below it for the
//    keys it binds, and is transparent for the keys it does not.
//
//  So with [gameplay, menu] on the stack, Escape (bound in both) produces only
//  the menu's action, while W (bound only in gameplay) still reaches gameplay.
//  That is what makes a pause menu behave the way players expect.
//
//  ---------------------------------------------------------------------------
//  DEAD ZONES: RADIAL, not per-axis. Recorded as Ch. 9.5 asks.
//
//  An analog stick at rest does not report zero - it drifts, and different
//  sticks drift differently. Without a dead zone a character slowly walks off
//  on their own while nobody touches the controller.
//
//  A PER-AXIS dead zone tests |x| and |y| separately, which makes the dead
//  region a SQUARE: pushing the stick diagonally to 0.2, 0.2 is inside the
//  dead zone on both axes and reports nothing, even though the stick is
//  clearly deflected. Diagonal movement then feels subtly dead near the
//  centre, which is hard to describe and easy to feel.
//
//  A RADIAL dead zone tests the LENGTH of the (x, y) pair, making the dead
//  region a circle, and rescales what is left so that the first responsive
//  value is 0 rather than jumping to the dead zone size. That is what is
//  implemented here.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/math/Vec2.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

class EventPump;
class ConfigNode;

enum class ActionState : u8 {
    Idle,
    Pressed,    // went down THIS frame
    Held,       // down, and was down last frame
    Released,   // came up THIS frame
};

const char* ToString(ActionState state);

class InputMap {
public:
    // --- contexts ---------------------------------------------------------
    static void PushContext(StringId context);
    static void PopContext();
    static void ClearContexts();
    static StringId ActiveContext();          // the top of the stack
    static usize ContextDepth();
    static std::vector<std::string> ContextNames();   // for the editor

    // --- digital queries --------------------------------------------------
    static bool        IsPressed(StringId action);    // this frame only
    static bool        IsHeld(StringId action);
    static bool        IsReleased(StringId action);
    static bool        IsDown(StringId action);       // pressed OR held
    static ActionState GetState(StringId action);

    // --- analog queries ---------------------------------------------------
    // A keyboard key feeding an axis reports exactly -1, 0 or 1. A stick
    // reports whatever it reports, AFTER the radial dead zone and rescale.
    static f32  GetAxis(StringId action);
    static Vec2 GetAxis2D(StringId negX, StringId posX, StringId negY, StringId posY);

    static void SetDeadZone(f32 deadZone);
    static f32  DeadZone();

    // Updated once per frame from the EventPump, BEFORE anything queries it.
    // Week 10's system order makes that explicit: Input is stage 1.
    //
    // Events the editor GUI consumed are skipped, which is what stops typing
    // in an Inspector field from making the player jump.
    static void Update(const EventPump& pump);

    // --- bindings ---------------------------------------------------------
    // Loaded from the config file's input section. Rebindable with no rebuild,
    // which is the milestone verification.
    //
    // Binding syntax, matching config/engine.json:
    //     "Key.A"            a keyboard key, by SDL's own key name
    //     "Key.Space"
    //     "Mouse.Left"       a mouse button
    //     "Gamepad.A"        recognised and ignored - see the note in the .cpp
    static void LoadBindings(const ConfigNode& inputNode, std::string& outWarnings);

    // Programmatic binding, for tests and for a rebinding screen.
    static void Bind(StringId context, StringId action, std::string_view binding);
    static void ClearBindings();

    // --- driving actions without a device ---------------------------------
    //
    // Reason 4 in the list at the top of this header - "anything that can
    // produce an action stream can drive the game: replay, AI" - was written
    // down in Week 8 as a benefit of the abstraction and then never actually
    // built. This is it, and it is three lines because the abstraction was
    // built correctly.
    //
    // Holds or releases an action as though a bound device had done it. The
    // NEXT InputMap::Update derives Pressed/Held/Released from it exactly as
    // it would for a real key, so gameplay code cannot tell the difference -
    // which is the property that makes it worth having rather than a test hack
    // that bypasses the layer it is meant to be testing.
    //
    // A real device event for the same action in the same frame overrides it;
    // a human at the keyboard always wins over the autopilot.
    static void InjectAction(StringId action, bool down);
    static void ClearInjectedActions();

    // Enumeration for the editor: every action in a context and what it is
    // bound to. Same argument as the CVar registry - a registry that cannot be
    // browsed cannot be shown.
    struct BindingInfo {
        std::string context;
        std::string action;
        std::string binding;
    };
    static void Snapshot(std::vector<BindingInfo>& out);
};

} // namespace eng
