// =============================================================================
//  ScoreKeeper - counts what Bullet.cpp reports and puts it on screen.
//
//  THE WORKED EXAMPLE FOR ENTITY REFERENCES AND FOR MESSAGES. It is also the
//  object that answers "where is the thing keeping score?", which is the
//  question FindScriptNamedIn exists for.
//
//  -----------------------------------------------------------------------------
//  TO USE IT
//
//  1. Drag this file onto one entity - the one that represents the game's
//     rules. One per scene.
//  2. Drag the UI text entity that should show the score onto the `label` slot.
//
//  -----------------------------------------------------------------------------
//  WHY `label` IS AN EntityRef AND NOT A NAME IN THE CODE
//
//  An EntityRef is drawn in the Inspector as something you drag an entity onto,
//  it survives a save and a script reload, and Scene::RenameEntity follows it -
//  so renaming the label in the Hierarchy does not break this. A hardcoded
//  Find("HudScore") breaks silently the first time somebody renames anything.
//  See engine/scene/EntityRef.h.
//
//  -----------------------------------------------------------------------------
//  THE Unsubscribe IN OnDestroy IS NOT TIDINESS. IT IS REQUIRED.
//
//  The lambda handed to the bus is CODE INSIDE THIS COMPILED SCRIPT. The bus
//  keeps it and knows nothing about where it came from, and the editor unloads
//  and reloads this library every time you save a file. A subscription left
//  behind is a call into memory that has been unmapped, on the next message
//  after a rebuild - and it would work for a while first, which is the worst
//  way for a bug like this to behave.
//
//  Anything that hands a callable to something longer-lived than a script needs
//  the matching line in OnDestroy. It is the one piece of manual lifetime
//  management scripting in this engine asks for.
// =============================================================================

#include <engine/Engine.h>

#include <string>


class ScoreKeeper final : public eng::ScriptBehaviour {
public:
    eng::EntityRef label;          // [UITextComponent] where the score is shown
    std::string    prefix = "Score: ";   // what goes in front of the number

    void OnStart() {
        m_subscription = eng::MessageBus::SubscribeBroadcast(
            "Scored", [this](const eng::Message& message) {
                // ONE SUBSCRIPTION PER TYPE, SO IT HEARS EVERYBODY'S. The
                // target is compared here rather than by the bus, which is the
                // same thing the script system does for collisions - see
                // Messaging.h on why there is only one kind of subscription.
                if (message.target == OwnerId()) {
                    m_score += message.i0;
                }
            });

        Show();
        ENGINE_LOG_INFO(eng::Channels::kGame, "ScoreKeeper ready on '{}'", Name());
    }

    void OnFixedUpdate() {
        // Only touched when it actually changed. Rebuilding the string every
        // step would allocate sixty times a second to write the same text.
        if (m_score != m_shown) {
            Show();
        }
    }

    void OnDestroy() {
        // See the note at the top of this file. This line is load-bearing.
        eng::MessageBus::Unsubscribe(m_subscription);
    }

private:
    void Show() {
        m_shown = m_score;
        if (auto* text = label.Find<eng::UITextComponent>()) {
            text->text = prefix + std::to_string(m_score);
        } else if (!m_warnedAboutLabel) {
            // ONCE, not every step. A missing label is worth saying and not
            // worth saying four thousand times.
            m_warnedAboutLabel = true;
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "ScoreKeeper on '{}' has no UITextComponent to write to - "
                            "drag a UI text entity onto its 'label' slot",
                            Name());
        }
    }

    eng::SubscriptionId m_subscription = 0;
    int                 m_score = 0;
    int                 m_shown = -1;   // never equal to m_score, so Show runs once
    bool                m_warnedAboutLabel = false;
};


ENGINE_REGISTER_SCRIPT(ScoreKeeper)
