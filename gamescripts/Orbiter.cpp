// =============================================================================
//  Orbiter - the worked example, and the file to read before writing your own.
//
//  Moves its entity around its own starting position in a circle. Deliberately
//  something SpinComponent cannot do: spin rotates a transform, and a parent's
//  rotation sweeps its children, but nothing in the engine moves a lone entity
//  along a path. That is what a script is for - behaviour that is specific to
//  one game and has no business being a built-in component.
//
//  Attach it by dragging Orbiter.cpp from the Asset Browser onto an entity in
//  the Hierarchy or the Inspector.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/math/Vec2.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <cmath>

namespace {

class Orbiter final : public eng::ScriptBehaviour {
public:
    void OnStart() override {
        // The CENTRE is captured here rather than in the constructor, because
        // the constructor runs before the behaviour is bound to its entity -
        // Transform() would be null. OnStart is the first moment the entity is
        // guaranteed to be whole. See the note on OnStart in
        // ScriptComponent.h.
        if (eng::Transform2D* transform = Transform(); transform != nullptr) {
            m_centre = transform->LocalPosition();
        }
    }

    void OnUpdate(eng::f32 deltaSeconds) override {
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        m_angle += kRadiansPerSecond * deltaSeconds;

        // Wrapped rather than left to grow. An angle accumulating for an hour
        // at 60 Hz reaches ~13000 radians, where a float has about a
        // thousandth of a radian of precision left and the motion visibly
        // stutters. Costs one comparison a step.
        if (m_angle > eng::kTwoPi) {
            m_angle -= eng::kTwoPi;
        }

        transform->SetLocalPosition(
            m_centre + eng::Vec2{std::cos(m_angle) * kRadius, std::sin(m_angle) * kRadius});
    }

    void OnCollisionEnter(eng::EntityHandle other) override {
        // The partner is a HANDLE and is resolved through the scene, never
        // cached as a pointer - it may already be dead. That is the rule the
        // whole engine runs on, and a script is not exempt from it.
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return;
        }
        const eng::Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(eng::Channels::kGame, "Orbiter touched '{}'",
                        partner != nullptr ? partner->Name() : "<destroyed>");
    }

private:
    static constexpr eng::f32 kRadius           = 90.0f;
    static constexpr eng::f32 kRadiansPerSecond = 1.2f;

    eng::Vec2 m_centre{};
    eng::f32  m_angle = 0.0f;
};

} // namespace

// Registers the name "Orbiter". Without this line the file compiles and the
// script can never be found.
ENGINE_REGISTER_SCRIPT(Orbiter)
