#pragma once

// =============================================================================
//  🚪 THE WEEK 10 GATE - Spec A, "Collector".
//
//  "A player square moves with mapped input on a single screen. Ten
//   collectible squares are placed by a scene file. Touching one destroys it
//   and increments a counter shown on the debug HUD. Collecting all ten is a
//   win. A timer of 60 seconds running out is a loss."
//
//  ---------------------------------------------------------------------------
//  THE RULE, AND THE ASSESSMENT: implemented in the SANDBOX target only, using
//  ONLY the engine's public API, without opening anything under engine/.
//
//      git diff --stat HEAD -- engine/     ->     zero changes
//
//  That diff is the assessment, not the game.
//
//  What this exercise uses, and where each came from:
//    mapped input                Week 8  InputMap, actions by StringId
//    entities from data          Week 9  Scene::Load, ComponentFactory
//    transform hierarchy         Week 6  Transform2D
//    collision events            Week 10 MessageBus + CollisionSystem
//    deferred destroy            Week 10 DeferredOps
//    messaging                   Week 10 MessageBus
//    debug text HUD              Week 6  DebugDraw::Text, screen space
//    fixed timestep for the timer Week 10 GameClock
//
//  NOT ONE of those needed a change under engine/. The two places where the
//  public API was nearly not enough are written up in
//  docs/week10-milestone4.md section 7 - finding them is the point of the
//  exercise, and they are Week 11 work.
//
//  This is a System registered at SystemStage::kGameplay, so it updates inside
//  the fixed step in the declared order, like anything else. Registering a
//  gameplay system is public API; nothing here is special-cased by the engine.
// =============================================================================

#include <engine/Engine.h>

namespace game {

class CollectorGame final : public eng::System {
public:
    bool Init();
    void Shutdown();

    // AUTOPILOT - the answer to "is it actually playable?" without a human at
    // the keyboard.
    //
    // It does NOT bypass the input layer. It steers by calling
    // InputMap::InjectAction on the same four named actions a player's keys are
    // bound to, so the movement code, the collision, the messaging and the
    // scoring all run exactly as they do for a person. If the autopilot can
    // finish a round, a player can.
    //
    // This is reason 4 from the list at the top of InputMap.h - "anything that
    // can produce an action stream can drive the game" - being used for the
    // first time, and it is why that list was worth writing down.
    void SetAutopilot(bool on) { m_autopilot = on; }
    bool IsFinished() const { return m_phase != Phase::Playing; }
    eng::u32 Collected() const { return m_collected; }

    void        Update(eng::f32 deltaSeconds) override;
    const char* Name() const override { return "CollectorGame"; }
    eng::i32    Order() const override { return eng::SystemStage::kGameplay; }

private:
    enum class Phase { Playing, Won, Lost };

    void OnCollected(eng::EntityHandle pickup);
    void DrawHud();
    void DriveAutopilot();

    eng::EntityHandle m_player{};
    eng::u32          m_collected     = 0;
    eng::u32          m_totalPickups  = 0;
    eng::f32          m_secondsLeft   = 60.0f;
    Phase             m_phase         = Phase::Playing;
    eng::SubscriptionId m_subscription = 0;
    bool              m_autopilot     = false;

    // Actions, hashed once at compile time via the _sid literal. Nothing here
    // knows what a key is - which is the Week 8 grep check, from the other
    // side.
    eng::StringId m_moveLeft;
    eng::StringId m_moveRight;
    eng::StringId m_moveUp;
    eng::StringId m_moveDown;
    eng::StringId m_quit;
};

} // namespace game
