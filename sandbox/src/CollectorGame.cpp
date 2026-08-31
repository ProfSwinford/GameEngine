// ============================================================================
//  CollectorGame.cpp - the sample game. See CollectorGame.h.
//
//  Everything here is written using only the engine's public interface. There
//  is nothing in this file that a game of your own could not also do.
// ============================================================================

#include "CollectorGame.h"

#include <cstdio>

namespace game {

bool CollectorGame::IsPickup(eng::Entity& entity) {
    const auto* collider = entity.Find<eng::AABBColliderComponent>();
    return collider != nullptr && collider->Layer() == "Pickup";
}

bool CollectorGame::Init() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    // The player is found BY NAME, from the scene file. There is no position,
    // no colour and no count written into this code - all of it comes from
    // assets/scenes/collector.json.
    m_player = scene.Find("Player");
    if (m_player.IsNull()) {
        ENGINE_LOG_ERROR(eng::Channels::kGame,
                         "this scene has no entity called 'Player' - is the right scene "
                         "loaded?");
        return false;
    }

    m_totalPickups = 0;
    scene.ForEach([this](eng::Entity& entity) {
        if (IsPickup(entity)) {
            ++m_totalPickups;
        }
    });

    m_secondsLeft = kTimeLimit;
    m_collected   = 0;
    m_phase       = Phase::Playing;

    // ONE broadcast subscription rather than one per pickup. Pickups are
    // destroyed as the round goes on, and a per-entity subscription would have
    // to be cancelled at exactly the right moment. Filtering here is three
    // lines and cannot go wrong.
    m_subscription = eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::kCollisionEnter, [this](const eng::Message& message) {
            if (m_phase != Phase::Playing) {
                return;
            }
            // The player is one side of the pair; the pickup is the other.
            if (message.target == m_player) {
                OnCollected(message.other);
            }
        });

    eng::SystemScheduler::Register(this);

    ENGINE_LOG_INFO(eng::Channels::kGame,
                    "Collector: {} pickup(s) placed by the scene file, {:.0f} seconds "
                    "on the clock", m_totalPickups, static_cast<double>(m_secondsLeft));
    return true;
}

void CollectorGame::Shutdown() {
    eng::SystemScheduler::Unregister(this);
    eng::MessageBus::Unsubscribe(m_subscription);
}

void CollectorGame::OnCollected(eng::EntityId pickup) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::Entity* entity = scene.Get(pickup);
    if (entity == nullptr) {
        return;   // already gone - two collisions in one tick, which is normal
    }

    // Only pickups count. The player also touches the walls, and a counter
    // that went up for those would be a memorable bug.
    if (!IsPickup(*entity)) {
        return;
    }

    // DESTROYED THROUGH THE QUEUE, not immediately.
    //
    // This function runs during message delivery, while the collision system's
    // list of touching pairs and the render system's list of sprites are both
    // being used. Destroying the entity right here is exactly the problem
    // DeferredOps exists to prevent - and it is GAME code doing it, which is
    // the case the whole mechanism was built for.
    eng::DeferredOps::QueueDestroy(pickup);
    ++m_collected;

    // A circle that stays for three seconds where the pickup was. This is the
    // gizmo lifetime feature earning its keep: the event happened once and was
    // over instantly, and the marker is what lets you go and look at it.
    const eng::Vec2 where = entity->Transform().WorldPosition();
    eng::Gizmos::Circle(where, 18.0f, eng::Color::Yellow(), 3.0f);

    ENGINE_LOG_INFO(eng::Channels::kGame, "collected '{}' ({}/{})", entity->Name(),
                    m_collected, m_totalPickups);

    if (m_collected >= m_totalPickups && m_totalPickups > 0) {
        m_phase = Phase::Won;
        ENGINE_LOG_INFO(eng::Channels::kGame, "WIN, with {:.1f} seconds to spare",
                        static_cast<double>(m_secondsLeft));
    }
}

void CollectorGame::DriveAutopilot() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::Entity* player = scene.Get(m_player);
    if (player == nullptr || m_phase != Phase::Playing) {
        eng::InputMap::ClearInjectedActions();
        return;
    }

    // Head for the nearest pickup that is still there.
    const eng::Vec2 here = player->Transform().WorldPosition();
    eng::Vec2       target{};
    float           bestDistanceSq = 0.0f;
    bool            found          = false;

    scene.ForEach([&](eng::Entity& entity) {
        if (entity.Id() == m_player || !IsPickup(entity)) {
            return;
        }
        // Skip anything already on its way out, or the autopilot keeps
        // steering at a pickup that has been collected but not yet removed.
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
        eng::InputMap::ClearInjectedActions();
        return;
    }

    // Steered by pressing ACTIONS, not by writing a position. Everything
    // downstream - GetAxis2D, the speed, the collision, the scoring - runs
    // exactly as it does for a person at the keyboard.
    const eng::Vec2 delta{target.x - here.x, target.y - here.y};
    constexpr float kSlack = 2.0f;   // stop nudging once basically lined up

    eng::InputMap::InjectAction("MoveRight", delta.x > kSlack);
    eng::InputMap::InjectAction("MoveLeft",  delta.x < -kSlack);
    eng::InputMap::InjectAction("MoveUp",    delta.y > kSlack);
    eng::InputMap::InjectAction("MoveDown",  delta.y < -kSlack);
}

void CollectorGame::Update(float deltaSeconds) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    if (m_autopilot) {
        DriveAutopilot();
    }

    if (eng::InputMap::IsPressed("Quit")) {
        eng::Engine::Get().RequestQuit();
    }

    if (m_phase == Phase::Playing) {
        // deltaSeconds is the FIXED step handed in by the scheduler - never a
        // clock read from inside this function. That is what makes the timer
        // behave identically at 30 frames a second and at 144.
        m_secondsLeft -= deltaSeconds;
        if (m_secondsLeft <= 0.0f) {
            m_secondsLeft = 0.0f;
            m_phase       = Phase::Lost;
            ENGINE_LOG_INFO(eng::Channels::kGame,
                            "out of time, with {}/{} collected", m_collected,
                            m_totalPickups);
        }
    }

    eng::Entity* player = scene.Get(m_player);
    if (player == nullptr) {
        return;   // the player was destroyed; the id caught it
    }

    if (m_phase == Phase::Playing) {
        // ACTIONS, NOT KEYS. Search this file for a key code and there is
        // nothing to find - which is why changing config/engine.json changes
        // the controls with no rebuild.
        const eng::Vec2 direction =
            eng::InputMap::GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp");
        player->Transform().Translate(direction * (kPlayerSpeed * deltaSeconds));
    }

    DrawHud();
}

void CollectorGame::DrawHud() {
    // SCREEN space, so the score and the timer stay put when the camera moves.
    char line[96];

    std::snprintf(line, sizeof(line), "COLLECTED  %d / %d", m_collected, m_totalPickups);
    eng::Gizmos::Text(eng::Vec2{16.0f, 16.0f}, line, eng::Color::White(), 0.0f,
                      eng::GizmoSpace::Screen);

    std::snprintf(line, sizeof(line), "TIME       %5.1f",
                  static_cast<double>(m_secondsLeft));
    eng::Gizmos::Text(eng::Vec2{16.0f, 34.0f}, line,
                      m_secondsLeft < 10.0f ? eng::Color::Red() : eng::Color::White(),
                      0.0f, eng::GizmoSpace::Screen);

    if (m_phase == Phase::Won) {
        eng::Gizmos::Text(eng::Vec2{16.0f, 64.0f}, "YOU WIN", eng::Color::Green(), 0.0f,
                          eng::GizmoSpace::Screen);
    } else if (m_phase == Phase::Lost) {
        eng::Gizmos::Text(eng::Vec2{16.0f, 64.0f}, "OUT OF TIME", eng::Color::Red(), 0.0f,
                          eng::GizmoSpace::Screen);
    }
}

} // namespace game
