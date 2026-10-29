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
//  IT IS ALSO THE EXAMPLE OF A TUNABLE SCRIPT. `radius` and
//  `degreesPerSecond` below appear in the Inspector, can be dragged while the
//  game runs, and are saved per entity - so two Orbiters can circle at
//  different speeds from one script. OnExpose is what puts them there; see
//  engine/scene/ScriptFields.h.
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
    //  THE TWO NUMBERS WORTH TUNING, which the Inspector can turn.
    //
    //  ORDINARY MEMBERS WITH THEIR DEFAULTS WRITTEN HERE, and not the
    //  `static constexpr` constants these used to be. That is the whole
    //  difference between a number you can change and one you cannot: a
    //  constant is fixed when the script compiles, so the Inspector could at
    //  best show it greyed out.
    //
    //  CHANGING THIS LINE STILL CHANGES EVERY ENTITY that has not been given
    //  its own value, because the scene file stores only what somebody
    //  actually edited. See engine/scene/ScriptFields.h for why that is the
    //  way round it is.
    // ========================================================================
    float radius = 90.0f;

    // IN DEGREES, like every angle anybody sees in this editor; the engine
    // holds radians and the conversion happens where it is used, once. 68.75
    // is the 1.2 radians a second this did before it was tunable.
    float degreesPerSecond = 68.75f;

    // ------------------------------------------------------------------------
    //  Which of them the Inspector shows. Nothing finds these by itself: C++
    //  can ask whether a name exists but not what names exist, so a variable
    //  is on screen because this function named it.
    //
    //  THE NAMES ARE ALSO THE KEYS IN THE SCENE FILE, so renaming one orphans
    //  whatever was saved under the old spelling. The Inspector says so when
    //  it finds a value nothing claims.
    // ------------------------------------------------------------------------
    void OnExpose(eng::ScriptFields& fields) {
        fields.Var("radius", radius,
                   "how far from its starting point it circles, in pixels");
        fields.Var("degreesPerSecond", degreesPerSecond,
                   "how fast it goes round. 360 is one full turn a second; a "
                   "negative number turns the other way.");
    }

    void OnStart() {
        // The centre of the circle is remembered HERE rather than in a
        // constructor, because a constructor runs before the script is
        // connected to its entity - Transform() would be null. OnStart is the
        // first moment the entity is guaranteed to be complete.
        if (eng::Transform2D* transform = Transform(); transform != nullptr) {
            m_centre = transform->LocalPosition();
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
        transform->SetLocalPosition(
            m_centre + eng::Vec2{std::cos(m_angle) * radius,
                                 std::sin(m_angle) * radius});
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
    eng::Vec2 m_centre{};
    float     m_angle = 0.0f;
};

} // namespace

// Registers the name "Orbiter". Without this line the file compiles perfectly
// and the script can never be found.
ENGINE_REGISTER_SCRIPT(Orbiter)
