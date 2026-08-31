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
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/math/Vec2.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <cmath>

namespace {

class Orbiter final : public eng::ScriptBehaviour {
public:
    void OnStart() override {
        // The centre of the circle is remembered HERE rather than in a
        // constructor, because a constructor runs before the script is
        // connected to its entity - Transform() would be null. OnStart is the
        // first moment the entity is guaranteed to be complete.
        if (eng::Transform2D* transform = Transform(); transform != nullptr) {
            m_centre = transform->LocalPosition();
        }
    }

    void OnUpdate(float deltaSeconds) override {
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        // Multiplied by deltaSeconds, always. Without that the speed would
        // depend on how often this happens to be called.
        m_angle += kRadiansPerSecond * deltaSeconds;

        // Wrapped rather than left to grow forever. An angle accumulating for
        // an hour at 60 steps a second reaches about 13,000 radians, at which
        // point a float has only about a thousandth of a radian of precision
        // left and the movement visibly stutters. This costs one comparison.
        if (m_angle > eng::kTwoPi) {
            m_angle -= eng::kTwoPi;
        }

        // cos and sin turn an angle into a position on a circle.
        transform->SetLocalPosition(
            m_centre + eng::Vec2{std::cos(m_angle) * kRadius,
                                 std::sin(m_angle) * kRadius});
    }

    void OnCollisionEnter(eng::EntityId other) override {
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
    static constexpr float kRadius           = 90.0f;
    static constexpr float kRadiansPerSecond = 1.2f;

    eng::Vec2 m_centre{};
    float     m_angle = 0.0f;
};

} // namespace

// Registers the name "Orbiter". Without this line the file compiles perfectly
// and the script can never be found.
ENGINE_REGISTER_SCRIPT(Orbiter)
