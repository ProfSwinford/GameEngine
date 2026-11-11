// ============================================================================
//  PhysicsToggle - switches a collider off and on again, on a timer.
//
//  SetEnabled(false) is meant to be a COMPLETE switch: no events, nothing
//  pushed, invisible to every query. It is how a door is opened, how a
//  platform is made to drop you through, and how an attack's hitbox is live
//  for three frames and then is not.
//
//  "Complete" is a strong claim and this is the station that checks it. The
//  outline turns grey, and three things have to follow at once:
//
//    whatever was resting on it falls through
//    the PhysicsReporter beside it sends an EXIT, rather than just going quiet
//    a PhysicsProbe aimed at it stops finding it
//
//  THE EXIT IS THE ONE THAT GETS MISSED. A pair that stops being tested is
//  easy to drop from the list without telling anybody, and the symptom in a
//  real game is a script that believes it is still standing on a platform that
//  has gone - so it never jumps again. Watch the "now" count on the reporter
//  come back to zero.
// ============================================================================

#include <engine/Engine.h>


class PhysicsToggle final : public eng::ScriptBehaviour {
public:
    float onSeconds  = 2.0f;   // [0.1..10] how long it stays solid
    float offSeconds = 1.5f;   // [0.1..10] how long it stays switched off

    void OnStart() {
        // FindCollider and not Find<BoxColliderComponent>, because this script
        // should work on whatever shape somebody put here. Asking for a box
        // works right up until the collider becomes a capsule, and then fails
        // silently. See physics/Collider.h.
        eng::Entity* self = Owner();
        m_collider = self != nullptr ? eng::FindCollider(*self) : nullptr;

        if (m_collider == nullptr) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "PhysicsToggle on '{}' found no collider to switch",
                            Name());
            return;
        }
        m_on    = m_collider->Enabled();
        m_timer = m_on ? onSeconds : offSeconds;
    }

    void OnFixedUpdate() {
        if (m_collider == nullptr) {
            return;
        }

        m_timer -= eng::Time::FixedDeltaTime();
        if (m_timer > 0.0f) {
            return;
        }

        m_on    = !m_on;
        m_timer = m_on ? onSeconds : offSeconds;
        m_collider->SetEnabled(m_on);
    }

private:
    // HELD AS A POINTER, which is safe here and would not be in general: a
    // component lives as long as its entity, and this script dies with the
    // same entity. A pointer to a component on ANOTHER entity would have to
    // be looked up fresh every time, because that entity can be destroyed.
    eng::ColliderComponent* m_collider = nullptr;

    bool  m_on    = true;
    float m_timer = 0.0f;
};


ENGINE_REGISTER_SCRIPT(PhysicsToggle)
