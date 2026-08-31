#pragma once

// ============================================================================
//  CollectorGame.h - the sample game.
//
//  A player square moves around with the arrow keys or WASD. Ten collectible
//  squares are placed by a scene file. Touching one makes it disappear and adds
//  to a counter shown on screen. Collect all ten to win; run out of time and
//  you lose.
//
//  ==========================================================================
//  READ THIS FILE AND CollectorGame.cpp BEFORE WRITING YOUR OWN GAME.
//
//  It is deliberately small, and every part of the engine it uses is used the
//  way the engine intends:
//
//    named actions instead of key codes    InputMap
//    entities loaded from a data file      Scene
//    collision events                      MessageBus + CollisionSystem
//    destroying things safely              DeferredOps
//    text on screen                        Gizmos::Text
//    a timer that is the same on every     GameClock's fixed step
//    machine
//
//  Crucially, NONE of it needed a change to the engine. Everything here is
//  written against the engine's public interface, in the sandbox program. If
//  a game can be built that way, the interface is complete.
//
//  This is a System registered at the Gameplay stage, so it updates inside the
//  fixed simulation step in the declared order, like everything else. Nothing
//  about it is special-cased by the engine.
// ============================================================================

#include <engine/Engine.h>

namespace game {

class CollectorGame final : public eng::System {
public:
    bool Init();
    void Shutdown();

    // AUTOPILOT - the answer to "is it actually playable?" without a person at
    // the keyboard.
    //
    // It does NOT skip the input layer. It steers by calling
    // InputMap::InjectAction on the same four named actions the player's keys
    // are bound to, so the movement, the collision, the messaging and the
    // scoring all run exactly as they do for a person. If the autopilot can
    // finish a round, so can a player.
    void SetAutopilot(bool on) { m_autopilot = on; }

    bool IsFinished() const { return m_phase != Phase::Playing; }
    int  Collected() const   { return m_collected; }

    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "CollectorGame"; }
    int         Order() const override { return eng::SystemStage::kGameplay; }

private:
    enum class Phase { Playing, Won, Lost };

    void OnCollected(eng::EntityId pickup);
    void DrawHud();
    void DriveAutopilot();

    // Is this entity one of the collectibles? Answered from the entity's own
    // DATA - it has a collider on the "Pickup" layer - rather than by looking
    // at its name. Somebody adding an eleventh pickup to the scene file needs
    // no code change at all, which is the whole point of the component model.
    static bool IsPickup(eng::Entity& entity);

    eng::EntityId m_player{};
    int           m_collected    = 0;
    int           m_totalPickups = 0;
    float         m_secondsLeft  = 60.0f;
    Phase         m_phase        = Phase::Playing;

    eng::SubscriptionId m_subscription = 0;
    bool                m_autopilot    = false;

    // How fast the player moves, in world units per second, and how long the
    // round lasts. Plain constants here; try changing them.
    static constexpr float kPlayerSpeed = 220.0f;
    static constexpr float kTimeLimit   = 60.0f;
};

} // namespace game
