#pragma once

// ============================================================================
//  InputMap.h - turns key presses into named actions.
//
//  Game code should never ask "was the W key pressed?". It should ask "does
//  the player want to move up?":
//
//      if (InputMap::IsDown("MoveUp")) { ... }
//
//  WHY THAT MATTERS
//  * The player can rebind the controls. WASD, arrow keys, or both at once -
//    all of it lives in config/engine.json and needs no rebuild.
//  * The game reads the same on a keyboard and on a gamepad, because the
//    device only ever appears in this one file.
//  * Anything able to produce a stream of actions can drive the game - a demo
//    recording, or the automatic playthrough used to test the sample game. See
//    InjectAction at the bottom.
//
//  This sits on top of EventPump, which is what supplies the raw key numbers.
//
//  ==========================================================================
//  CONTEXTS ARE A STACK, not a single "current" one.
//
//  A pause menu opened over the game needs menu controls on top with the game
//  controls still underneath: Escape should close the menu, and the game's own
//  Escape binding must not also fire.
//
//  So a key is looked up starting at the TOP of the stack and working down,
//  and the FIRST context that binds that key wins. Lower contexts never see
//  it. A context therefore hides the contexts below it for the keys it binds,
//  and is invisible for the keys it does not.
//
//  With ["gameplay", "menu"] on the stack, Escape (bound in both) produces
//  only the menu's action, while W (bound only in gameplay) still reaches the
//  game. That is what makes a pause menu behave the way players expect.
//  ==========================================================================
// ============================================================================

#include <engine/core/Json.h>
#include <engine/math/Vec2.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

class EventPump;

// What an action is doing right now. The difference between Pressed and Held
// is what lets a jump happen once per press instead of every frame the key is
// down.
enum class ActionState {
    Idle,
    Pressed,    // went down THIS frame
    Held,       // still down, was already down last frame
    Released,   // came up THIS frame
};

const char* ToString(ActionState state);

class InputMap {
public:
    // ---- contexts ---------------------------------------------------------
    static void        PushContext(std::string_view context);
    static void        PopContext();
    static void        ClearContexts();
    static std::string ActiveContext();     // the one on top
    static std::size_t ContextDepth();

    // ---- asking about an action ------------------------------------------
    static bool        IsPressed(std::string_view action);    // only on the frame it went down
    static bool        IsHeld(std::string_view action);
    static bool        IsReleased(std::string_view action);
    static bool        IsDown(std::string_view action);       // pressed OR held
    static ActionState GetState(std::string_view action);

    // 0 when the action is not active, 1 when it is. Useful for movement.
    static float GetAxis(std::string_view action);

    // Combines four actions into a direction, e.g.
    //     GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp")
    //
    // The result is never longer than 1. Without that, holding two keys at
    // once would move a character diagonally about 40% faster than moving
    // straight, which players notice immediately.
    static Vec2 GetAxis2D(std::string_view negX, std::string_view posX,
                          std::string_view negY, std::string_view posY);

    // Refreshes every action from this frame's events. Called once per frame,
    // before anything asks a question.
    //
    // Events the editor's GUI claimed are skipped, which is what stops typing
    // a name into the Inspector from also making the player jump.
    static void Update(const EventPump& pump);

    // ---- bindings ---------------------------------------------------------
    //
    // Loaded from the "input" section of config/engine.json, which looks like:
    //
    //     "input": {
    //       "contexts": {
    //         "gameplay": {
    //           "MoveLeft": ["Key.A", "Key.Left"],
    //           "Jump":     ["Key.Space"],
    //           "Fire":     ["Mouse.Left"]
    //         }
    //       }
    //     }
    //
    // A binding is "Device.Name": "Key." followed by any key name SDL knows,
    // or "Mouse." followed by Left, Right or Middle.
    static void LoadBindings(const Json& inputSection, std::string& outWarnings);

    // Adds a binding from code, for tests and for a rebinding screen.
    static void Bind(std::string_view context, std::string_view action,
                     std::string_view binding);
    static void ClearBindings();

    // ---- driving the game without a keyboard ------------------------------
    //
    // Holds or releases an action as if a real key had done it. The next
    // Update works out Pressed/Held/Released from it exactly as it would for a
    // real key, so game code cannot tell the difference. That is what makes it
    // worth having rather than a test shortcut that skips the layer it is
    // supposed to be testing.
    //
    // Injected actions go through the SAME context stack, so an autopilot
    // cannot drive the player around while a menu is open.
    static void InjectAction(std::string_view action, bool down);
    static void ClearInjectedActions();

    // Every action and what it is bound to, for the editor to display.
    struct BindingInfo {
        std::string context;
        std::string action;
        std::string binding;
    };
    static void Snapshot(std::vector<BindingInfo>& out);
};

} // namespace eng
