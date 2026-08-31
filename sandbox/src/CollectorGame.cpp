// 🚪 THE WEEK 10 GATE - Spec A, "Collector". See CollectorGame.h.
//
// Sandbox target, engine public API only, zero changes under engine/.

#include "CollectorGame.h"

#include <cstdio>

namespace game {
namespace {

// Tunables as CVars rather than constants, because that is what Week 8 was
// for: the speed below was found by dragging a slider in the editor, not by
// rebuilding eleven times.
eng::CVar* g_playerSpeed = nullptr;
eng::CVar* g_timeLimit   = nullptr;

} // namespace

bool CollectorGame::Init() {
    m_moveLeft  = eng::Intern("MoveLeft");
    m_moveRight = eng::Intern("MoveRight");
    m_moveUp    = eng::Intern("MoveUp");
    m_moveDown  = eng::Intern("MoveDown");
    m_quit      = eng::Intern("Quit");

    g_playerSpeed = eng::CVarRegistry::RegisterFloat(
        "game.playerSpeed", 220.0f, "Collector: player movement speed, world units/sec.");
    g_timeLimit = eng::CVarRegistry::RegisterFloat(
        "game.timeLimit", 60.0f, "Collector: seconds before the round is lost.");

    eng::Scene& scene = eng::Engine::Get().GetScene();

    // The player and the pickups are found BY NAME, from the scene file. There
    // is no position, no colour and no count compiled in here - which is the
    // Week 9 bar arriving in gameplay code.
    m_player = scene.Find("Player");
    if (m_player.IsNull()) {
        ENGINE_LOG_ERROR(eng::Channels::kGame,
                         "the scene has no entity named 'Player'; is the right scene "
                         "loaded?");
        return false;
    }

    m_totalPickups = 0;
    scene.ForEach([this](eng::Entity& entity) {
        // "Is this a pickup" is answered by the entity's own data - it has a
        // collider on the Pickup layer - not by parsing its name. A designer
        // adding an eleventh pickup in the scene file needs no code change,
        // which is the property the whole model exists for.
        if (const eng::Component* collider =
                entity.FindComponent(eng::AABBColliderComponent::TypeIdStatic());
            collider != nullptr) {
            const auto* box = static_cast<const eng::AABBColliderComponent*>(collider);
            if ((box->Layer() & eng::CollisionLayers::kPickup) != 0) {
                ++m_totalPickups;
            }
        }
    });

    m_secondsLeft = g_timeLimit->GetFloat();
    m_collected   = 0;
    m_phase       = Phase::Playing;

    // ONE broadcast subscription rather than one per pickup: pickups are
    // destroyed as the round goes on, and a per-entity subscription would have
    // to be unsubscribed at exactly the right moment. Filtering here is three
    // lines and cannot leak.
    m_subscription = eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::CollisionEnter(), [this](const eng::Message& message) {
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
                    "Collector: {} pickup(s) placed by the scene file, {:.0f} second "
                    "limit", m_totalPickups, static_cast<double>(m_secondsLeft));
    return true;
}

void CollectorGame::Shutdown() {
    eng::SystemScheduler::Unregister(this);
    eng::MessageBus::Unsubscribe(m_subscription);
}

void CollectorGame::OnCollected(eng::EntityHandle pickup) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::Entity* entity = scene.Get(pickup);
    if (entity == nullptr) {
        return;   // already gone: two collisions in one tick, which is normal
    }

    // Only pickups count. The player also collides with the walls, and a
    // counter that incremented on those would be a memorable bug.
    const auto* collider = static_cast<const eng::AABBColliderComponent*>(
        entity->FindComponent(eng::AABBColliderComponent::TypeIdStatic()));
    if (collider == nullptr || (collider->Layer() & eng::CollisionLayers::kPickup) == 0) {
        return;
    }

    // DEFERRED. This handler runs during message dispatch, at stage 500, while
    // the collision system's pair set and the sprite render system's array are
    // both live. Destroying immediately here is exactly the
    // iterator-invalidation bug DeferredOps exists to prevent - and it is
    // GAMEPLAY code doing it, which is the case the whole mechanism was built
    // for.
    eng::DeferredOps::QueueDestroy(pickup);
    ++m_collected;

    // A three-second marker where the pickup was. This is the debug-draw
    // lifetime feature earning its keep: the event happened once and is over,
    // and the marker is what lets you go and look at it.
    const eng::Vec2 where = entity->Transform().WorldPosition();
    eng::DebugDraw::Circle(where, 18.0f, eng::Color::Yellow(), 3.0f);

    ENGINE_LOG_INFO(eng::Channels::kGame, "collected '{}' ({}/{})", entity->Name(),
                    m_collected, m_totalPickups);

    if (m_collected >= m_totalPickups && m_totalPickups > 0) {
        m_phase = Phase::Won;
        ENGINE_LOG_INFO(eng::Channels::kGame, "WIN with {:.1f} seconds to spare",
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

    // Steer toward the nearest surviving pickup. Found the same way Init counts
    // them - by collision layer, from the entity's own data - so an eleventh
    // pickup added to the scene file is steered toward with no code change.
    const eng::Vec2 here = player->Transform().WorldPosition();
    eng::Vec2       target{};
    eng::f32        bestDistanceSq = 0.0f;
    bool            found = false;

    scene.ForEach([&](eng::Entity& entity) {
        if (entity.Handle() == m_player) {
            return;
        }
        const auto* collider = static_cast<const eng::AABBColliderComponent*>(
            entity.FindComponent(eng::AABBColliderComponent::TypeIdStatic()));
        if (collider == nullptr ||
            (collider->Layer() & eng::CollisionLayers::kPickup) == 0) {
            return;
        }
        // Skip anything already on its way out, or the autopilot will keep
        // steering at a pickup that has been collected but not yet drained
        // from the deferred queue.
        if (eng::DeferredOps::IsPendingDestroy(entity.Handle())) {
            return;
        }
        const eng::Vec2 to = entity.Transform().WorldPosition();
        const eng::f32  distanceSq = eng::DistanceSquared(here, to);
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

    // INJECTED AS ACTIONS, not as a position write. Everything downstream -
    // GetAxis2D, the dead zone, the speed CVar, the collision - runs exactly
    // as it does for a person at the keyboard.
    const eng::Vec2 delta{target.x - here.x, target.y - here.y};
    constexpr eng::f32 kSlack = 2.0f;   // stop nudging once basically aligned

    eng::InputMap::InjectAction(m_moveRight, delta.x >  kSlack);
    eng::InputMap::InjectAction(m_moveLeft,  delta.x < -kSlack);
    eng::InputMap::InjectAction(m_moveUp,    delta.y >  kSlack);
    eng::InputMap::InjectAction(m_moveDown,  delta.y < -kSlack);
}

void CollectorGame::Update(eng::f32 deltaSeconds) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    if (m_autopilot) {
        DriveAutopilot();
    }

    if (eng::InputMap::IsPressed(m_quit)) {
        eng::Engine::Get().RequestQuit();
    }

    if (m_phase == Phase::Playing) {
        // GAME time, through the fixed step handed to this system - never a
        // clock read inside an update. That is what makes the timer identical
        // at 30 and at 144 FPS, which is Week 10 verification item 1.
        m_secondsLeft -= deltaSeconds;
        if (m_secondsLeft <= 0.0f) {
            m_secondsLeft = 0.0f;
            m_phase       = Phase::Lost;
            ENGINE_LOG_INFO(eng::Channels::kGame, "LOSS: time ran out with {}/{} collected",
                            m_collected, m_totalPickups);
        }
    }

    eng::Entity* player = scene.Get(m_player);
    if (player == nullptr) {
        return;   // stale handle, detected and reported by the engine
    }

    if (m_phase == Phase::Playing) {
        // ACTIONS, NOT KEYS. Grep this file for SDL_SCANCODE and there is
        // nothing to find - which is the Week 8 check, and the reason a
        // rebind in config/engine.json changes the controls with no rebuild.
        const eng::Vec2 direction = eng::InputMap::GetAxis2D(m_moveLeft, m_moveRight,
                                                             m_moveDown, m_moveUp);
        const eng::f32  speed     = g_playerSpeed->GetFloat();
        player->Transform().Translate(direction * (speed * deltaSeconds));
    }

    DrawHud();
}

void CollectorGame::DrawHud() {
    // SCREEN space, so it does not move when the camera pans - the Week 6
    // requirement that debug draw be per-call space-aware, used exactly as
    // intended.
    char line[96];

    std::snprintf(line, sizeof(line), "COLLECTED  %u / %u", m_collected, m_totalPickups);
    eng::DebugDraw::Text(eng::Vec2{16.0f, 16.0f}, line, eng::Color::White(), 0.0f,
                         eng::DebugSpace::Screen);

    std::snprintf(line, sizeof(line), "TIME       %5.1f", static_cast<double>(m_secondsLeft));
    eng::DebugDraw::Text(eng::Vec2{16.0f, 34.0f}, line,
                         m_secondsLeft < 10.0f ? eng::Color::Red() : eng::Color::White(),
                         0.0f, eng::DebugSpace::Screen);

    if (m_phase == Phase::Won) {
        eng::DebugDraw::Text(eng::Vec2{16.0f, 64.0f}, "YOU WIN", eng::Color::Green(), 0.0f,
                             eng::DebugSpace::Screen);
    } else if (m_phase == Phase::Lost) {
        eng::DebugDraw::Text(eng::Vec2{16.0f, 64.0f}, "OUT OF TIME", eng::Color::Red(),
                             0.0f, eng::DebugSpace::Screen);
    }
}

} // namespace game
