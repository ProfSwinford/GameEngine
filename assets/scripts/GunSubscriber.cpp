// =============================================================================
//  GunSubscriber - a method called when ONE named action fires, which is the
//  half of the input system the OnFire() spelling cannot reach.
//
//  Unity calls this "Invoke C# Events". There, one callback sees every action
//  and the script tells them apart itself; here the component does the telling
//  apart, so what a script writes is one line per action:
//
//      input->Subscribe("Fire", this, &GunSubscriber::FireGun);
//
//  WHEN TO WANT THIS RATHER THAN void OnFire(). The method already has a name
//  worth keeping, or one method is to answer to several actions, or the script
//  wants the automatic dispatch off for everything on the entity and itself
//  still listening - which is what SetNotifications(Callbacks) below does.
//
//  =============================================================================
//  THE THREE THINGS IT TAKES CARE OF, each of which a hand-written
//  input->Subscribe(lambda) has to get right for itself:
//
//    THE NAME. The callback is only run for the action that was named.
//
//    THE PHASE. One press is ONE call. Started arrives in the same breath as
//    Performed in this engine, so a lambda filtering on the name alone runs
//    its body twice; this applies eng::ActionSendsMessage, which is the rule
//    the generated OnFire() dispatch uses.
//
//    THE LIFETIME. Nothing is stored on this side - no token, no vector of
//    them, no OnDestroy. ~ScriptBehaviour withdraws the subscription, which
//    matters because the Player Input component OUTLIVES this object: a
//    script rebuild destroys the behaviour and builds another one while the
//    component beside it carries on.
//
//  THE PARAMETER IS THE VALUE, chosen by the method's own shape - the same
//  ladder OnMove/OnLean/OnJump are picked by. Take eng::Vec2 for a Vector2
//  action, float for an Axis, const eng::ActionEvent& for the whole event, or
//  nothing at all.
// =============================================================================

#include <engine/Engine.h>


class GunSubscriber final : public eng::ScriptBehaviour {
public:
    // Drag the entity carrying the Player Input onto this. Usually the same
    // entity this script is on, which is what the fallback below assumes.
    PlayerInput input;

    void OnStart() {
        if (!input) {
            input = Find<eng::PlayerInputComponent>();   // same entity, if any
        }
        if (!input) {
            ENGINE_LOG_WARN(eng::Channels::kGame, "GunSubscriber: no Player Input");
            return;
        }

        // NO MESSAGES AT ALL. This is the component's setting, so it turns
        // OnFire() off for every script on the entity - including this one.
        // The subscriptions below are not messages and keep running, which is
        // the whole reason that mode is usable from one script.
        input->SetNotifications(eng::InputNotification::Callbacks);

        input->Subscribe("Fire", this, &GunSubscriber::FireGun);
        input->Subscribe("Move", this, &GunSubscriber::Aim);
        input->Subscribe("Jump", this, &GunSubscriber::Jump);
    }

    void FireGun() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "FireGun ({}) facing {},{}", ++m_shots,
                        m_aim.x, m_aim.y);
    }

    // THE PARAMETER IS HOW THE VALUE ARRIVES. Move is a Value action with a
    // Vector2 control, so a method taking eng::Vec2 is handed the direction -
    // and is handed (0,0) once more when the keys are let go.
    void Aim(eng::Vec2 direction) { m_aim = direction; }

    void Jump() { ENGINE_LOG_INFO(eng::Channels::kGame, "Jump"); }

private:
    eng::Vec2 m_aim{};
    int       m_shots = 0;
};


ENGINE_REGISTER_SCRIPT(GunSubscriber)
