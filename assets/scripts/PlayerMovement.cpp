#include <engine/Engine.h>
using namespace eng;

class PlayerMovement final : public MyScriptBehaviour {
public:

    float movementSpeed = 1;
    float jumpHeight = 10;
    float msecondsAlive = 0.0f;
    TestScript testScript;

    void OnStart() {
        // CHECKED BEFORE IT IS USED, which it was not - and that took the whole
        // player down.
        //
        // `TestScript testScript;` above is a REFERENCE: a bare script name is
        // a slot you drag an entity onto, not the script itself. Two things
        // followed from treating it as the script:
        //
        //   1. It cannot be assigned from a lookup. Find<> hands back a
        //      pointer, and the script build prints a warning on that line
        //      saying exactly this. The warning was there all along.
        //   2. operator-> on an unresolved reference returns NULL, so
        //      `testScript->found = true` wrote through nothing. On
        //      scenes/tilemap.oscene the player carries a TestScript and it
        //      happened to resolve; on scenes/collector.oscene it does not,
        //      and the player died on load with an access violation.
        //
        // An entity without a TestScript on it is an ordinary thing, not a
        // reason to stop the game. The lookup is a plain local because that is
        // what a lookup gives you; the reference above stays as what it is, a
        // slot somebody can fill in the Inspector.
        // The reference is pointed at THIS entity - which is what the old
        // Find<>() was reaching for - and then checked. Reaching it through the
        // reference also keeps TestScript's definition out of this file, which a
        // raw TestScript* would need and cannot have: each script is compiled
        // into its own module and sees the others only by forward declaration.
        testScript.Set(*Owner());
        if (testScript) {
            testScript->found = true;
        }
    }

    void OnUpdate()
    {
        direction = eng::InputMap::GetAxis2D();
    }

    void OnFixedUpdate() {
        if (transform == nullptr) {
            return;
        }

        transform->Translate(direction * (movementSpeed * eng::Time::FixedDeltaTime()));
    }

    void OnDestroy() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement lived {:.2f} seconds",
                        msecondsAlive);
    }

    //void OnCollisionEnter(eng::EntityId other) {
    //    // The other entity is looked up fresh rather than remembered, because
    //    // it may already have been destroyed this step.
    //    eng::Scene* scene = GetScene();
    //    if (scene == nullptr) {
    //        return;
    //    }
    //    const eng::Entity* partner = scene->Get(other);
    //    ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement touched '{}'",
    //                    partner != nullptr ? partner->Name() : "<already gone>");
    //}

private:
    eng::Vec2 direction;
};


// Registers the name "PlayerMovement" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(PlayerMovement)
