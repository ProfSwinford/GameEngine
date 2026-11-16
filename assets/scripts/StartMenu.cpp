// =============================================================================
//  StartMenu - the button that leaves the menu.
//
//  It knows two things: which scene to open, and which GameController opens
//  it. It does NOT call RequestScene itself, and that is the point of the
//  pair: every scene change goes through one object, so every scene change
//  gets the same fade without each caller remembering to ask for one.
//
//  -----------------------------------------------------------------------------
//  NO #include FOR GameController, AND THAT IS NORMAL. The build puts every
//  script in reach of every other, the same way DoorSwitch reaches Door.
//
//  THE BARE SCRIPT NAME IS A REFERENCE. `GameController controller;` reads
//  like Unity's `public GameController controller;` and behaves like one: the
//  Inspector offers only entities that actually carry a GameController, and
//  paints the name red if it stops resolving. It is not a GameController by
//  value - the bare name is an alias for a reference to one - so this costs an
//  entity name rather than a whole controller, and it survives a script
//  rebuild. See ScriptRef.h.
//
//  -----------------------------------------------------------------------------
//  WHY A REFERENCE AND NOT GameController::Instance()
//
//  Because every script is compiled into its OWN library, and a static read
//  through a call written here would read THIS library's copy of that static -
//  which is null, always. The first version of this pair did exactly that and
//  silently did nothing. GoToScene below is safe because it writes instance
//  members on the controller object; the controller's own OnUpdate, running in
//  its own library, is what turns that into a fade. The long note at the top
//  of GameController.cpp has the whole story.
// =============================================================================

#include <engine/Engine.h>


class StartMenu final : public eng::ScriptBehaviour {
public:
    // Drag the GameController entity onto this in the Inspector. The scene
    // file stores the entity's NAME, so it survives a save and a reload.
    GameController controller;

    // Which scene the button opens. A public field, so it can be pointed at a
    // different level without touching this file.
    std::string targetScene = "scenes/collector.oscene";

    void OnStart() {
        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "StartMenu: click Start to open {}", targetScene);
    }

    void OnUpdate() {
        // WasClicked is true for exactly one step - press AND release on the
        // same button, so sliding off is how you change your mind. Polled
        // rather than a callback, because a callback would outlive the script
        // when the scene reloads. See UI.h.
        auto* button = Find<eng::UIButtonComponent>();
        if (button == nullptr || !button->WasClicked()) {
            return;
        }

        // Null when nothing was dragged in, when the entity has gone, and when
        // it has no GameController on it - all handled the same way, which is
        // why they are not told apart.
        if (!controller) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "StartMenu: no GameController is assigned, so '{}' cannot "
                            "be opened with a fade",
                            targetScene);
            return;
        }

        controller->GoToScene(targetScene);
    }
};


ENGINE_REGISTER_SCRIPT(StartMenu)
