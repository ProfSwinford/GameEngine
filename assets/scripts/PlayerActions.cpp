// =============================================================================
//  PlayerActions - the worked example for the EVENT-DRIVEN input system.
//
//  There is no Subscribe here, no polling, and nothing to register. An action
//  called "Jump" in assets/input/player.oinput calls a method named
//  void OnJump() on any script on the entity, and writing the method IS the
//  subscription. Delete a handler and that action stops arriving; rename an
//  action in the Actions window and the method it calls changes with it.
//
//  -----------------------------------------------------------------------------
//  THE THREE SHAPES A HANDLER MAY TAKE, all of them below:
//
//      void OnJump()                     // just that it happened
//      void OnLean(float amount)         // one number,  -1 to 1
//      void OnMove(eng::Vec2 direction)  // the whole value
//
//  Pick the one that suits the action. The engine works out which you wrote
//  and calls it; a method whose parameters are none of these three is
//  reported as unreachable rather than failing the build, so a typo costs you
//  that one handler and nothing else.
//
//  THE METHOD MUST BE PUBLIC and must return void. Both are silent failures:
//  the engine calls it from outside the class, and the scanner that finds
//  handlers anchors on the literal word "void".
//
//  -----------------------------------------------------------------------------
//  WHICH PHASES ARRIVE, WHICH IS NOT ALL OF THEM
//
//  A Button calls its method ONCE, on the press. There is no second call on
//  the release - poll eng::InputMap or use a Value action if you need one.
//
//  A Value calls its method on every change AND once more with zero when it
//  stops, so a script that started moving when OnMove gave it a direction is
//  told when the direction becomes nothing. That final zero is the reason a
//  Value action is the right shape for movement.
//
//  -----------------------------------------------------------------------------
//  WHY THIS SCRIPT MOVES NOTHING, which is the one odd thing about it.
//
//  PlayerMovement on this same entity already drives the player, through the
//  OTHER input system - config/engine.json's bindings, polled with
//  eng::InputMap. The two systems are independent and both read the same
//  keyboard, so a second script translating the transform would simply move
//  the player twice as far per frame. This one therefore only RECORDS, and
//  draws what it recorded, which is also what makes it readable: the numbers
//  on screen are exactly what the handlers were handed.
//
//  To drive a player from this system instead, delete PlayerMovement from the
//  entity and move the body of OnMove into an OnFixedUpdate that applies
//  `direction`. Doing it in the handler would tie movement to the frames an
//  action happened to change on.
// =============================================================================

#include <engine/Engine.h>


class PlayerActions final : public eng::ScriptBehaviour {
public:
    // A PUBLIC FIELD IS AN INSPECTOR CONTROL with no annotation needed, and
    // these are live while the game runs - which makes them the quickest way
    // to see whether an action is arriving at all.
    bool showOnScreen = true;   // Draws the readout over the game view.

    void OnStart() {
        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "PlayerActions: WASD or the arrows to Move, Space to Jump, "
                        "E and Q to Lean, Escape to Pause");
    }

    void OnFixedUpdate() {
        if (showOnScreen) {
            DrawReadout();
        }
    }

    // ---- the handlers -------------------------------------------------------

    // THE WHOLE VALUE, already combined. Four keys became one direction, and
    // a diagonal is cut back to length 1 rather than being 41% faster - the
    // composite in the .oinput did that, not this.
    void OnMove(eng::Vec2 direction) {
        m_move = direction;
        ++m_moveCalls;
    }

    // ONE NUMBER, from the 1DAxis composite: E is +1, Q is -1, neither is 0.
    void OnLean(float amount) {
        m_lean = amount;
        ++m_leanCalls;
    }

    // NO PARAMETER, because there is nothing to say beyond that it happened.
    void OnJump() {
        ++m_jumps;
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerActions: OnJump ({} so far)",
                        m_jumps);
    }

    void OnFire() {
        ++m_shots;
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerActions: OnFire ({} so far)",
                        m_shots);
    }

    // Escape is Pause in the Gameplay map and Cancel in the Menu map, which
    // is the whole reason one file holds both. A map REPLACES rather than
    // layering, so while Menu is current nothing in Gameplay is consulted and
    // there is nothing to arbitrate between the two meanings of the key.
    //
    // SwitchCurrentActionMap is a pointer change inside an asset already in
    // memory - no disk, no parsing, no frame cost.
    void OnPause() {
        eng::PlayerInputComponent* input = Find<eng::PlayerInputComponent>();
        if (input == nullptr) {
            return;
        }
        const bool toMenu = input->CurrentActionMapName() != "Menu";
        input->SwitchCurrentActionMap(toMenu ? "Menu" : "Gameplay");
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerActions: now on the {} map",
                        input->CurrentActionMapName());
    }

    // The Menu map's own actions. Writing them here, on the same script, is
    // what makes the switch above worth anything: Escape leaves gameplay,
    // Escape again comes back, and Enter confirms in between.
    void OnCancel() { OnPause(); }

    void OnSubmit() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerActions: OnSubmit from the menu");
    }

    void OnNavigate(eng::Vec2 direction) { m_move = direction; }

private:
    void DrawReadout() const {
        const eng::PlayerInputComponent* input = Find<eng::PlayerInputComponent>();
        const std::string map =
            (input != nullptr) ? input->CurrentActionMapName() : std::string("<none>");

        // SCREEN space, so the readout sits still wherever the camera is.
        eng::Gizmos::Text(eng::Vec2{16.0f, 16.0f}, "map: " + map, eng::Color::White(),
                          0.0f, eng::GizmoSpace::Screen);
        eng::Gizmos::Text(eng::Vec2{16.0f, 40.0f},
                          "OnMove " + std::to_string(m_move.x) + ", " +
                              std::to_string(m_move.y) + "  (" +
                              std::to_string(m_moveCalls) + " calls)",
                          eng::Color::Green(), 0.0f, eng::GizmoSpace::Screen);
        eng::Gizmos::Text(eng::Vec2{16.0f, 64.0f},
                          "OnLean " + std::to_string(m_lean) + "  (" +
                              std::to_string(m_leanCalls) + " calls)",
                          eng::Color::Green(), 0.0f, eng::GizmoSpace::Screen);
        eng::Gizmos::Text(eng::Vec2{16.0f, 88.0f},
                          "OnJump x" + std::to_string(m_jumps) + "   OnFire x" +
                              std::to_string(m_shots),
                          eng::Color::Yellow(), 0.0f, eng::GizmoSpace::Screen);
    }

    eng::Vec2 m_move{};
    float     m_lean      = 0.0f;
    int       m_moveCalls = 0;
    int       m_leanCalls = 0;
    int       m_jumps     = 0;
    int       m_shots     = 0;
};


ENGINE_REGISTER_SCRIPT(PlayerActions)
