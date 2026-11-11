// ============================================================================
//  PhysicsLauncher - puts a body back where it started and fires it again.
//
//  THE STATION THIS EXISTS FOR IS TUNNELLING. This engine's collision is
//  DISCRETE: every fixed step moves a body first and looks for overlaps
//  afterwards - see SystemOrder.h, which says so plainly. So a body moving
//  faster than its own thickness per step can be on one side of a thin wall at
//  the end of one step and the far side at the end of the next, with no step
//  in between where the two overlapped. Nothing is reported and nothing is
//  pushed, because as far as the collision system is concerned they never met.
//
//  The number is easy to work out and worth knowing for any game: at a sixtieth
//  of a second a body travelling 1200 units a second moves 20 units a step. A
//  wall 8 units thick will be missed; one 40 units thick will not.
//
//  THIS IS A PROPERTY TO DESIGN AROUND, NOT A BUG TO FILE. Fast things get a
//  CircleCast ahead of themselves - Physics2D has one for exactly this - or
//  thick walls. The playground fires three identical bodies at three identical
//  walls at three speeds so the threshold can be seen rather than guessed.
//
//  ==========================================================================
//  IT ALSO MAKES EVERY OTHER STATION RE-RUNNABLE.
//
//  Drop one on anything and Jump fires it again from its starting place, so a
//  bounce test or a stack can be re-run without reloading the scene. With
//  `repeatSeconds` set it re-fires on a timer and needs no key at all.
// ============================================================================

#include <engine/Engine.h>


class PhysicsLauncher final : public eng::ScriptBehaviour {
public:
    // The velocity it is given, in world units per second. Set straight on the
    // body rather than as a force, because a force is spread over one step and
    // "fire this at exactly 2000" is the point of the station.
    eng::Vec2 launch{1200.0f, 0.0f};

    // Fires once this long after the scene starts. Negative waits for the key.
    float firstDelaySeconds = 0.75f;   // [-1..10]

    // Fires again this often. 0 fires once and then only on the key.
    float repeatSeconds = 3.0f;        // [0..20]

    // The action that fires it by hand. Jump is Space by default; see the
    // input bindings in config/engine.json.
    bool fireOnJump = true;

    // DRAWS WHERE IT HAS BEEN. This is what turns tunnelling from "it ended
    // up over there somehow" into a visible trail that passes straight
    // through a wall.
    bool leaveATrail = true;

    // ======================================================================
    //  HOW OFTEN A MARK IS LEFT, AND IT IS NOT EVERY STEP.
    //
    //  A gizmo with a lifetime stays in the queue until its time is up, and
    //  the whole queue is redrawn every frame. One dot per step at a three
    //  second lifetime is 180 shapes per trailing body - and a Gizmos::Circle
    //  is drawn as twenty-four line segments, so thirteen bodies doing that
    //  came to roughly fifty-six THOUSAND lines a frame and dropped the
    //  playground to ten frames a second in a debug build.
    //
    //  Every fourth step, for two seconds, as a short dash along the
    //  direction of travel: thirty shapes per body, one segment each, and the
    //  trail reads better for it because the dashes show speed as well as
    //  position.
    // ======================================================================
    int   trailEverySteps  = 4;     // [1..20] 1 is one mark per step
    float trailSeconds     = 2.0f;  // [0.2..6] how long a mark lasts

    void OnStart() {
        if (transform != nullptr) {
            m_home         = transform->WorldPosition();
            m_homeRotation = transform->WorldRotation();
        }
        m_body = Find<eng::Rigidbody2DComponent>();
        if (m_body == nullptr) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "PhysicsLauncher on '{}' has no Rigidbody2DComponent, so "
                            "there is nothing to launch",
                            Name());
        }
        m_timer = firstDelaySeconds;
    }

    void OnFixedUpdate() {
        const float step = eng::Time::FixedDeltaTime();

        if (m_timer >= 0.0f) {
            m_timer -= step;
            if (m_timer < 0.0f) {
                Fire();
                m_timer = repeatSeconds > 0.0f ? repeatSeconds : -1.0f;
            }
        }

        if (fireOnJump && eng::InputMap::IsPressed("Jump")) {
            Fire();
            m_timer = repeatSeconds > 0.0f ? repeatSeconds : -1.0f;
        }

        if (leaveATrail && transform != nullptr && m_body != nullptr &&
            m_body->Velocity().LengthSquared() > 1.0f &&
            ++m_sinceMark >= (trailEverySteps > 0 ? trailEverySteps : 1)) {
            m_sinceMark = 0;

            // A LIFETIME IS THE WHOLE REASON GIZMOS CARRY ONE. The body is
            // long gone by the time you look at where it went; the dash is
            // still there. See render/Gizmos.h.
            //
            // A LINE AND NOT A CIRCLE, for the reason on trailEverySteps: a
            // circle costs twenty-four segments and this costs one.
            const eng::Vec2 at   = transform->WorldPosition();
            const eng::Vec2 dash = m_body->Velocity() * step * 0.6f;
            eng::Gizmos::Line(at - dash, at + dash,
                              eng::Color{110, 160, 255, 255}, trailSeconds);
        }
    }

private:
    // Back to the start, then given the velocity. Both halves matter: setting
    // the velocity without resetting the position fires it from wherever it
    // had already fallen to, and resetting the position without clearing the
    // spin leaves it tumbling from the last run.
    void Fire() {
        if (transform == nullptr) {
            return;
        }
        transform->SetWorldPosition(m_home);
        transform->SetWorldRotation(m_homeRotation);

        if (m_body != nullptr) {
            m_body->SetVelocity(launch);
            m_body->SetAngularVelocity(0.0f);
        }
    }

    eng::Rigidbody2DComponent* m_body = nullptr;

    eng::Vec2 m_home{};
    float     m_homeRotation = 0.0f;
    float     m_timer        = -1.0f;
    int       m_sinceMark    = 0;
};


ENGINE_REGISTER_SCRIPT(PhysicsLauncher)
