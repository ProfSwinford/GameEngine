// =============================================================================
//  Autopilot - plays the game by itself, by pressing the same controls a person
//  would.
//
//  NOT ATTACHED TO ANYTHING BY DEFAULT. Attach it to the Player in the
//  Inspector and press Play; the player drives itself around collecting things.
//  Detach it to take the controls back. Leaving it attached while you try to
//  play means you and it are both pressing the movement actions, which mostly
//  cancels out - that is why it is off by default rather than always on.
//
//  -----------------------------------------------------------------------------
//  THE POINT: IT DOES NOT SKIP THE INPUT LAYER.
//
//  It would be much easier to write the player's position directly. It
//  deliberately does not. It calls InputMap::InjectAction on the same four
//  NAMED ACTIONS the keyboard is bound to, so everything downstream -
//  GetAxis2D, the speed in PlayerMovement, the collision test, the pickup's
//  destroy, the score in GameRules - runs exactly as it does for a person at
//  the keyboard.
//
//  That is what makes it worth having. A test that bypasses the input layer
//  only proves the test works. If this can finish a round, a player can.
//
//  It also means this script steers ANY entity whose movement reads those four
//  actions. It knows nothing about PlayerMovement.
//
//  -----------------------------------------------------------------------------
//  WHY IT RE-FINDS ITS TARGET EVERY STEP
//
//  The nearest pickup changes as pickups are collected, so there is nothing
//  worth remembering between steps - and a remembered EntityId would need
//  checking against the scene anyway. Searching ten entities once per step is
//  cheaper than the bookkeeping would be.
// =============================================================================

#include <engine/Engine.h>

namespace {

// The same data-driven test GameRules uses: a collider on the "Pickup" layer.
// Deliberately duplicated rather than shared, because scripts are compiled as
// separate files with no header between them - and four lines repeated is a
// smaller problem than a header that has to be kept in step.
bool IsPickup(eng::Entity& entity) {
    auto* collider = eng::FindCollider(entity);
    return collider != nullptr && collider->Layer() == "Pickup";
}

class Autopilot final : public eng::ScriptBehaviour {
public:
    void OnStart() {
        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "Autopilot is driving '{}' - detach this script to take the "
                        "controls back",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }

    void OnFixedUpdate() {
        // Steering is a direction, not a distance, so this one needs no delta.

        eng::Scene*       scene     = GetScene();
        eng::Transform2D* transform = Transform();
        if (scene == nullptr || transform == nullptr) {
            Stop();
            return;
        }

        const eng::Vec2 here = transform->WorldPosition();

        eng::Vec2 target{};
        float     bestDistanceSq = 0.0f;
        bool      found          = false;

        scene->ForEach([&](eng::Entity& entity) {
            if (!IsPickup(entity)) {
                return;
            }
            // Skip anything already on its way out, or the autopilot keeps
            // steering at a pickup that has been collected but not yet removed
            // and never moves on to the next one.
            if (eng::DeferredOps::IsPendingDestroy(entity.Id())) {
                return;
            }
            const eng::Vec2 to         = entity.Transform().WorldPosition();
            const float     distanceSq = eng::DistanceSquared(here, to);
            if (!found || distanceSq < bestDistanceSq) {
                found          = true;
                bestDistanceSq = distanceSq;
                target         = to;
            }
        });

        if (!found) {
            Stop();   // nothing left to collect
            return;
        }

        // Pressing ACTIONS, not writing a position.
        const eng::Vec2 delta{target.x - here.x, target.y - here.y};
        constexpr float kSlack = 2.0f;   // stop nudging once basically lined up

        eng::InputMap::InjectAction("MoveRight", delta.x > kSlack);
        eng::InputMap::InjectAction("MoveLeft", delta.x < -kSlack);
        eng::InputMap::InjectAction("MoveUp", delta.y > kSlack);
        eng::InputMap::InjectAction("MoveDown", delta.y < -kSlack);
    }

    void OnDestroy() { Stop(); }

private:
    // Let go of every action. Without this the last direction stays pressed
    // forever and the player walks into a wall for the rest of the round.
    static void Stop() { eng::InputMap::ClearInjectedActions(); }
};

} // namespace

// Registers the name "Autopilot" so that a scene file and the editor can find
// it. WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(Autopilot)
