// ============================================================================
//  PhysicsMover - drives a KINEMATIC body back and forth, the right way and
//  the wrong way, so the difference can be watched rather than read about.
//
//  A moving platform, a lift, a sliding door and a character controller are
//  all the same thing: a body that moves when told to and is not pushed back.
//  That is what kinematic means, and there are exactly two ways to tell one
//  where to go.
//
//  ==========================================================================
//  MovePosition SHOVES. WRITING THE TRANSFORM TELEPORTS.
//
//  MovePosition says "be there by the end of the step", and the solver gets
//  it there by giving the body the velocity to arrive - so the crate standing
//  on it is pushed along, and anything in its way is pushed out of it.
//
//  Writing transform->SetWorldPosition puts the body at the new place with no
//  velocity and no contact ever solved. The platform passes straight through
//  the crate and the crate stays where it was, hanging in the air until it
//  notices and falls.
//
//  Untick `useMovePosition` and watch that happen. It is the single most
//  common mistake in a 2D physics game and it is much easier to recognise
//  once you have seen it on purpose.
//
//  ==========================================================================
//  IT NEEDS A KINEMATIC RIGIDBODY.
//
//  On a DYNAMIC body MovePosition fights gravity and the solver every step,
//  and on a STATIC one it does nothing at all - a static body has no velocity
//  to be given. The script says so in the Console once rather than every step,
//  because a warning that repeats sixty times a second is a warning nobody
//  reads.
// ============================================================================

#include <engine/Engine.h>

#include <cmath>


class PhysicsMover final : public eng::ScriptBehaviour {
public:
    // HOW FAR IT GOES AND BACK, from wherever it starts, in world units. The
    // start is remembered in OnStart, so moving the entity in the editor moves
    // the whole patrol with it rather than leaving the path behind.
    eng::Vec2 travel{200.0f, 0.0f};

    // One leg of the trip. The full cycle is twice this.
    float seconds = 2.0f;   // [0.1..10] how long one leg takes

    // THE WHOLE POINT OF THIS SCRIPT. Ticked: MovePosition, so it carries and
    // shoves. Unticked: the transform is written directly, so it teleports and
    // touches nothing.
    bool useMovePosition = true;

    // Spins it as well, through MoveRotation - which a lift does not want and
    // a rotating platform does.
    float degreesPerSecond = 0.0f;   // [-360..360] 0 does not turn

    // SMOOTHED rather than linear. A platform that reverses instantly has an
    // infinite acceleration at each end, and the crate standing on it is left
    // behind for a step every time it turns round - which looks like a bug in
    // the solver and is not one.
    bool easeAtTheEnds = true;

    void OnStart() {
        if (transform != nullptr) {
            m_start = transform->WorldPosition();
            m_startRadians = transform->WorldRotation();
        }

        m_body = Find<eng::Rigidbody2DComponent>();
        if (m_body == nullptr) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "PhysicsMover on '{}' has no Rigidbody2DComponent, so "
                            "MovePosition has nothing to drive - it will teleport "
                            "whatever `useMovePosition` says",
                            Name());
            return;
        }
        if (m_body->BodyType() != eng::RigidbodyType::Kinematic) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "PhysicsMover on '{}' expects a Kinematic body; this one "
                            "is {}, and will fight the solver",
                            Name(),
                            m_body->IsDynamic() ? "Dynamic" : "Static");
        }
    }

    void OnFixedUpdate() {
        if (transform == nullptr || seconds <= 0.0f) {
            return;
        }

        // 0 at one end, 1 at the other, turned round each time it arrives.
        m_phase += eng::Time::FixedDeltaTime() / seconds * (m_outbound ? 1.0f : -1.0f);
        if (m_phase >= 1.0f) { m_phase = 1.0f; m_outbound = false; }
        if (m_phase <= 0.0f) { m_phase = 0.0f; m_outbound = true; }

        // Smoothstep: flat at both ends, so the reversal is gentle. The
        // straight-line version is one multiply and is what `easeAtTheEnds`
        // turns this back into.
        const float t = easeAtTheEnds ? m_phase * m_phase * (3.0f - 2.0f * m_phase)
                                      : m_phase;

        const eng::Vec2 target  = m_start + travel * t;
        const float     radians = m_startRadians +
                                  m_spin * degreesPerSecond * eng::kDegToRad;
        m_spin += eng::Time::FixedDeltaTime();

        if (useMovePosition && m_body != nullptr) {
            m_body->MovePosition(target);
            if (degreesPerSecond != 0.0f) {
                m_body->MoveRotation(radians);
            }
            return;
        }

        // THE WRONG WAY, on purpose. No velocity, no contact, nothing pushed.
        transform->SetWorldPosition(target);
        if (degreesPerSecond != 0.0f) {
            transform->SetWorldRotation(radians);
        }
    }

private:
    eng::Rigidbody2DComponent* m_body = nullptr;

    eng::Vec2 m_start{};
    float     m_startRadians = 0.0f;
    float     m_phase    = 0.0f;
    float     m_spin     = 0.0f;
    bool      m_outbound = true;
};


ENGINE_REGISTER_SCRIPT(PhysicsMover)
