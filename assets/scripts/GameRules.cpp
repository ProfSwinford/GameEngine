// =============================================================================
//  GameRules - the score, the clock, and whether you have won.
//
//  Attach it to ONE entity in the scene. It does not matter which, and it does
//  not need a sprite or a collider - an empty entity called "GameRules" is the
//  clearest thing to put it on. A script that is about the whole round rather
//  than about one object still has to live on an entity, because an entity is
//  the only thing the engine knows how to attach a script to.
//
//  -----------------------------------------------------------------------------
//  IT COUNTS THE WORLD RATHER THAN KEEPING A SCORE.
//
//  There is no `++m_collected` anywhere in this file. Every step it asks the
//  scene how many pickups are left and works the score out from that:
//
//      collected = total at the start - still there now
//
//  The alternative is for this script to keep a counter and for Pickup to tell
//  it to go up, through a message. That is one more moving part, and it has a
//  failure mode this version simply does not have: if a pickup is ever
//  destroyed by something other than being collected - the Inspector's delete
//  button, a script of your own, a reloaded scene - a counter drifts out of
//  step with the world and nothing reports it. Counting cannot drift.
//
//  It costs a loop over the entities once per step. For ten pickups that is
//  nothing. If you had ten thousand, the counter would be worth its extra
//  moving part - but you would have measured first.
//
//  -----------------------------------------------------------------------------
//  WHY THE TIMER USES THE FIXED STEP AND NEVER READS A WALL CLOCK
//
//  Time::FixedDeltaTime() is the FIXED simulation step. A
//  function that asked the clock what time it was would make the countdown
//  depend on the frame rate, which is the whole thing this engine's fixed step
//  exists to avoid. Sixty seconds here is sixty seconds on every machine, and
//  the Toolbar's Step button advances it by exactly one tick.
//
//  -----------------------------------------------------------------------------
//  WHAT IS DELIBERATELY NOT HERE: quitting.
//
//  Reading a "Quit" action and shutting the program down belongs to the
//  PROGRAM, not to the game's rules - and this script runs inside the editor
//  too, where quitting would close the editor out from under somebody who was
//  only play-testing. The player executable handles that; see player/src/main.cpp.
// =============================================================================

#include <engine/Engine.h>

#include <cstdio>

namespace {

// Is this entity one of the collectibles? Answered from the entity's own DATA -
// a collider on the "Pickup" layer - rather than from its name. Somebody adding
// an eleventh pickup to the scene file needs no code change at all, which is
// the whole point of the component model.
bool IsPickup(eng::Entity& entity) {
    auto* collider = eng::FindCollider(entity);
    return collider != nullptr && collider->Layer() == "Pickup";
}

class GameRules final : public eng::ScriptBehaviour {
public:
    void OnStart() {
        m_totalPickups = CountPickups();
        m_secondsLeft  = kTimeLimit;
        m_phase        = Phase::Playing;

        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "GameRules: {} pickup(s) placed by the scene file, {:.0f} "
                        "seconds on the clock",
                        m_totalPickups, static_cast<double>(kTimeLimit));

        if (m_totalPickups == 0) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "no pickups in this scene - nothing has a collider on the "
                            "'Pickup' layer, so there is nothing to collect");
        }
    }

    void OnFixedUpdate() {
        // The score, derived rather than remembered. Anything already queued
        // for destruction counts as collected, so the number on screen changes
        // on the step you touched it rather than the step after.
        const int remaining = CountPickups();
        const int collected = m_totalPickups - remaining;

        if (m_phase == Phase::Playing) {
            m_secondsLeft -= eng::Time::FixedDeltaTime();

            if (collected >= m_totalPickups && m_totalPickups > 0) {
                m_phase        = Phase::Won;
                m_untilWinScene = kWinSceneDelay;
                ENGINE_LOG_INFO(eng::Channels::kGame,
                                "WIN, with {:.1f} seconds to spare",
                                static_cast<double>(m_secondsLeft));
            } else if (m_secondsLeft <= 0.0f) {
                m_secondsLeft = 0.0f;
                m_phase       = Phase::Lost;
                ENGINE_LOG_INFO(eng::Channels::kGame, "out of time, with {}/{} collected",
                                collected, m_totalPickups);
            }
        } else if (m_phase == Phase::Won) {
            // ---- CHANGING SCENE, which is the point of this part ----------
            //
            // A short pause first, so "YOU WIN" is readable and the last
            // pickup is visibly gone before the screen changes. A cut on the
            // same frame as the final collision reads as a glitch rather than
            // as a result.
            //
            // The countdown uses the fixed step for the same reason the round
            // timer does: it is the fixed step, so the pause is the same
            // length on every machine.
            m_untilWinScene -= eng::Time::FixedDeltaTime();
            if (m_untilWinScene <= 0.0f) {
                RequestWinScene();
            }
        }

        UpdateHud(collected);
    }

private:
    enum class Phase { Playing, Won, Lost };

    // -------------------------------------------------------------------------
    //  OPENING ANOTHER SCENE FROM A SCRIPT
    //
    //      eng::Engine::Get().RequestScene("scenes/you_win.oscene");
    //
    //  REQUEST, never Load. The difference is not a style preference:
    //
    //    * LoadScene happens at once. Loading unloads the current scene,
    //      which destroys every entity, which destroys every component -
    //      INCLUDING THIS SCRIPT, whose OnFixedUpdate is part way through it.
    //      The call would return into an object that has been freed, and the
    //      next line to read a member reads deleted memory. It usually seems
    //      to work, which is what makes it dangerous.
    //
    //    * RequestScene writes the name down. The engine carries it out at
    //      the end of the frame, once no system is walking a list of entities
    //      and no script is mid-update. It is the same arrangement as
    //      DeferredOps::QueueDestroy, for a sharper version of the reason.
    //
    //  Nothing calls "unload". Loading a scene unloads the old one first, and
    //  that releases every texture nothing else is using - the Console line
    //  about images still loaded is that happening.
    // -------------------------------------------------------------------------
    void RequestWinScene() {
        if (m_requested) {
            return;   // asked already; this keeps running until the swap
        }
        m_requested = true;

        ENGINE_LOG_INFO(eng::Channels::kGame, "GameRules: opening {}", kWinScene);
        eng::Engine::Get().RequestScene(kWinScene);
    }

    int CountPickups() const {
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return 0;
        }
        int count = 0;
        scene->ForEach([&count](eng::Entity& entity) {
            if (IsPickup(entity) && !eng::DeferredOps::IsPendingDestroy(entity.Id())) {
                ++count;
            }
        });
        return count;
    }

    // -------------------------------------------------------------------------
    //  THE HUD IS MADE OF UI OBJECTS, not of gizmos.
    //
    //  It used to be three Gizmos::Text calls, and it worked right up until
    //  the Game view started asking for the world WITHOUT gizmos - which it
    //  has to, because gizmos are the grid, the origin arrows and the
    //  collider outlines, and none of that ships with a game. The score went
    //  with them and the HUD vanished from the only view that shows what a
    //  player actually sees.
    //
    //  So the score, the timer and the banner are entities in the scene file
    //  now, each with a UIRect and a UIText. This script finds them by name
    //  and sets their text. The LAYOUT is data you can drag around in the
    //  Inspector; only the WORDS are code. See engine/include/engine/scene/UI.h.
    // -------------------------------------------------------------------------
    eng::UITextComponent* FindLabel(const char* entityName) const {
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return nullptr;
        }
        eng::Entity* entity = scene->Get(scene->Find(entityName));
        return entity != nullptr ? entity->Find<eng::UITextComponent>() : nullptr;
    }

    void UpdateHud(int collected) {
        char line[96];

        if (eng::UITextComponent* score = FindLabel("HudScore")) {
            std::snprintf(line, sizeof(line), "COLLECTED  %d / %d", collected,
                          m_totalPickups);
            score->text = line;
        }

        if (eng::UITextComponent* timer = FindLabel("HudTimer")) {
            std::snprintf(line, sizeof(line), "TIME       %5.1f",
                          static_cast<double>(m_secondsLeft));
            timer->text = line;

            // The last ten seconds in red. Setting a colour on the component
            // rather than passing one to a draw call is the difference
            // between an object you can inspect and a call you cannot.
            timer->color = (m_secondsLeft < 10.0f) ? eng::Color{235, 90, 80, 255}
                                                   : eng::Color{235, 238, 245, 255};
        }

        // The banner is HIDDEN in the scene file and switched on here, which
        // is why there is no "if won, draw" branch around a draw call.
        if (eng::UITextComponent* banner = FindLabel("HudBanner")) {
            eng::Entity* owner = banner->Owner();
            auto*        rect  = owner != nullptr ? owner->Find<eng::UIRectComponent>()
                                                  : nullptr;
            if (rect != nullptr) {
                rect->visible = (m_phase != Phase::Playing);
            }
            if (m_phase == Phase::Won) {
                banner->text  = "YOU WIN";
                banner->color = eng::Color{120, 230, 140, 255};
            } else if (m_phase == Phase::Lost) {
                banner->text  = "OUT OF TIME";
                banner->color = eng::Color{235, 90, 80, 255};
            }
        }
    }

    int   m_totalPickups   = 0;
    float m_secondsLeft    = 0.0f;
    Phase m_phase          = Phase::Playing;
    float m_untilWinScene  = 0.0f;
    bool  m_requested      = false;

    // How long the round lasts. Try changing it.
    static constexpr float kTimeLimit = 60.0f;

    // How long "YOU WIN" stays on this scene before the next one opens.
    static constexpr float kWinSceneDelay = 1.5f;

    // The scene to open. A name rather than a build-list number, because this
    // is a particular screen rather than "the next level" - see
    // RequestSceneByIndex in engine/include/engine/Engine.h for the other way.
    static constexpr const char* kWinScene = "scenes/you_win.oscene";
};

} // namespace

// Registers the name "GameRules" so that a scene file and the editor can find
// it. WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(GameRules)
