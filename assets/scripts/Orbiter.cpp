// ============================================================================
//  Orbiter - a worked example script, and the file to read before writing your
//  own.
//
//  It moves its entity around its own starting position in a circle.
//
//  That is deliberately something SpinComponent cannot do. Spin turns a
//  transform on the spot, and turning a PARENT sweeps its children round with
//  it - but nothing built into the engine moves a lone entity along a path.
//  Behaviour that is specific to one game, and has no business being a
//  built-in component, is exactly what a script is for.
//
//  Attach it by dragging Orbiter.cpp from the Assets panel onto an entity in
//  the Hierarchy or the Inspector.
//
//  IT IS ALSO THE EXAMPLE OF A TUNABLE SCRIPT. The three public variables
//  below appear in the Inspector, can be dragged while the game runs, and are
//  saved per entity - so two Orbiters can circle at different speeds from one
//  script.
//
//  NOTHING HERE ASKS FOR THAT. They are on screen because they are public and
//  of a type the Inspector can draw, and because the editor reads this file
//  when it builds it. The trailing comment on each one becomes its tooltip,
//  and a [low..high] at the start of that comment makes it a slider. See
//  engine/tools/ScriptVars.h.
// ============================================================================

//#include <engine/core/Log.h>
//#include <engine/math/Transform2D.h>
//#include <engine/math/Vec2.h>
//#include <engine/scene/Entity.h>
//#include <engine/scene/Scene.h>
//#include <engine/scene/ScriptComponent.h>
#include <engine/Engine.h>
#include <cmath>

namespace {

class Orbiter final : public eng::ScriptBehaviour {
public:
    // ========================================================================
    //  THE NUMBERS WORTH TUNING, which the Inspector can turn.
    //
    //  ORDINARY PUBLIC MEMBERS WITH THEIR DEFAULTS WRITTEN HERE, and not the
    //  `static constexpr` constants these used to be. That is the whole
    //  difference between a number you can change and one you cannot - and a
    //  constant is not merely greyed out, it is left out, with the Console
    //  saying which one and why.
    //
    //  PUBLIC IS WHAT EXPOSES THEM. Move one below `private:` and it is gone
    //  from the Inspector, which is the way to keep bookkeeping off screen.
    //
    //  CHANGING THIS LINE STILL CHANGES EVERY ENTITY that has not been given
    //  its own value, because the scene file stores only what somebody
    //  actually edited. See engine/scene/ScriptFields.h for why that is the
    //  way round it is.
    // ========================================================================
    float radius = 90.0f;   // how far out it circles, in pixels

    // IN DEGREES, like every angle anybody sees in this editor; the engine
    // holds radians and the conversion happens where it is used, once. 68.75
    // is the 1.2 radians a second this did before it was tunable.
    float degreesPerSecond = 68.75f;   // [-360..360] one full turn a second at 360

    // WHAT IT CIRCLES, and the example of a reference to another entity.
    //
    // Left empty it orbits the spot it started at, which is what this did
    // before. Drag an entity onto it in the Inspector and it orbits THAT -
    // following it as it moves, so a moon goes round a planet that is itself
    // going somewhere.
    //
    // NO REQUIRED TYPE in the marker below, because there is nothing to
    // insist on: every entity has a transform, so every entity is something
    // that can be circled. GameRules.cpp shows the other case.
    eng::EntityRef centre;   // what to go round. Empty circles where it started.

    void OnStart() {
        // The centre of the circle is remembered HERE rather than in a
        // constructor, because a constructor runs before the script is
        // connected to its entity - Transform() would be null. OnStart is the
        // first moment the entity is guaranteed to be complete.
        if (eng::Transform2D* transform = Transform(); transform != nullptr) {
            m_start = transform->LocalPosition();
        }
    }

    void OnFixedUpdate() {
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        // Multiplied by the step length, always. Without that the speed would
        // depend on how often this happens to be called.
        //
        // The variable is READ HERE, every step, rather than copied into
        // something at OnStart. That is what makes turning the knob in the
        // Inspector take effect while the game is running - nothing has to be
        // told that it changed, because nothing cached it.
        m_angle += degreesPerSecond * eng::kDegToRad * eng::Time::FixedDeltaTime();

        // Wrapped rather than left to grow forever. An angle accumulating for
        // an hour at 60 steps a second reaches about 13,000 radians, at which
        // point a float has only about a thousandth of a radian of precision
        // left and the movement visibly stutters. This costs one comparison.
        if (m_angle > eng::kTwoPi) {
            m_angle -= eng::kTwoPi;
        }

        // cos and sin turn an angle into a position on a circle.
        const eng::Vec2 offset{std::cos(m_angle) * radius,
                               std::sin(m_angle) * radius};

        // ====================================================================
        //  TWO PATHS, AND THEY ARE IN DIFFERENT COORDINATE SPACES. Collapsing
        //  them into one would be wrong for a parented entity, which is
        //  exactly the case this engine is built to make easy.
        //
        //  NO TARGET: the circle is drawn in the PARENT'S space, around the
        //  spot this entity started at. So an orbiter parented to something
        //  that moves keeps orbiting relative to it, and turning the parent
        //  sweeps the whole orbit round - which is the behaviour described at
        //  the top of math/Transform2D.h and what this script did before it
        //  could be given a target.
        //
        //  A TARGET: the other entity's position is a WORLD position, so the
        //  answer has to be set as one. Adding a world position to a local
        //  one compiles perfectly and puts the entity somewhere neither of
        //  them is.
        //
        //  ASKED EVERY STEP in both cases. The target is allowed to move -
        //  that is most of the reason to point at one - and allowed to be
        //  destroyed, in which case Target() returns null and this falls back
        //  to the remembered spot rather than stopping dead.
        // ====================================================================
        if (const eng::Entity* around = centre.Target()) {
            transform->SetWorldPosition(around->Transform().WorldPosition() + offset);
        } else {
            transform->SetLocalPosition(m_start + offset);
        }
    }

    void OnCollisionEnter(eng::EntityId other) {
        // The other entity is looked up through the scene rather than
        // remembered as a pointer, because it may already have been destroyed.
        // That is the rule the whole engine runs on, and a script is not
        // exempt from it.
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return;
        }
        const eng::Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(eng::Channels::kGame, "Orbiter touched '{}'",
                        partner != nullptr ? partner->Name() : "<already gone>");
    }

private:
    // Where it was when it started, in its PARENT'S space - which is what
    // LocalPosition means. Only used when no target is set; see OnFixedUpdate.
    eng::Vec2 m_start{};
    float     m_angle = 0.0f;
};

} // namespace

// Registers the name "Orbiter". Without this line the file compiles perfectly
// and the script can never be found.
ENGINE_REGISTER_SCRIPT(Orbiter)
