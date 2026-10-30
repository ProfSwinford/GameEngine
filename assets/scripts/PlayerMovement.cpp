// =============================================================================
//  PlayerMovement - a script.
//
//  Attach it by dragging this file from the Assets panel onto an entity in the
//  Hierarchy or the Inspector.
//
//  -----------------------------------------------------------------------------
//  THIS IS COMPILED C++, NOT AN INTERPRETED SCRIPT.
//
//  Saving this file changes nothing in a running editor. Build the project and
//  start the editor again, and it connects itself - the scene already refers to
//  "PlayerMovement" by name, so nothing needs reattaching.
//
//  Until then the Inspector shows this script as NOT FOUND, which is the editor
//  telling you the truth rather than pretending.
//
//  -----------------------------------------------------------------------------
//  THE LIFECYCLE. Every one of these is optional; delete the ones you do not
//  need.
//
//    OnStart()            Once, on the first simulation step after this script
//                         is attached and its entity is fully built. NOT at
//                         attach time: while a scene loads, components are
//                         attached one at a time, so another component you look
//                         for at attach time may not exist yet.
//
//    OnFixedUpdate()      Every FIXED simulation step. NOT once per drawn
//                         frame - this engine simulates at a steady rate and
//                         draws separately, so this runs a whole number of
//                         times per frame, sometimes twice and sometimes not
//                         at all. That is what makes the game behave the same
//                         on every machine, and it is where movement, physics
//                         and anything timed belongs.
//
//                         It takes no argument: ask Time::FixedDeltaTime().
//
//    OnUpdate()           Once per DRAWN frame, so however often that happens
//                         to be on this machine. For things tied to the
//                         screen rather than to the world - a fade, a menu, a
//                         camera that should move as smoothly as the display
//                         allows. NOT for movement or physics: it runs at a
//                         different rate on every machine, which is the whole
//                         thing the fixed step exists to avoid.
//
//                         Ask Time::DeltaTime() here, or
//                         Time::UnscaledDeltaTime() for something that must
//                         keep moving while the game is paused.
//
//    OnDestroy()          The entity is going away. It is still safe to touch
//                         here and not afterwards.
//
//    OnCollisionEnter     A SOLID contact: neither side is a trigger. ENTER
//    OnCollisionStay      fires once when the overlap begins, STAY every step
//    OnCollisionExit      it continues, and EXIT once when it ends - including
//                         when the other entity is destroyed while still
//                         overlapping.
//
//                         Takes EITHER the other entity or the whole contact:
//
//                             void OnCollisionEnter(eng::EntityId other)
//                             void OnCollisionEnter(const eng::Collision2D& hit)
//
//                         The second form carries hit.normal, hit.point and
//                         hit.relativeVelocity - which is how "I landed on
//                         something" is told from "I walked into a wall".
//
//    OnTriggerEnter       The same three events for a TRIGGER contact, where
//    OnTriggerStay        either side has "trigger" ticked. A trigger reports
//    OnTriggerExit        overlaps and nothing is pushed apart.
//
//                         A PAIR SENDS ONE SET OR THE OTHER, NEVER BOTH. A
//                         script with OnCollisionEnter on a trigger collider
//                         is never called - so the engine reports that in the
//                         Console rather than leaving it silent.
//
//                         `other` is an EntityId, not a pointer, and the thing
//                         it refers to may already be gone. Look it up through
//                         the scene every time; never keep a pointer to it.
//
//                         These need a collider on BOTH entities, and each
//                         one's "collides with" list has to include the
//                         other's layer.
//
//  -----------------------------------------------------------------------------
//  WHAT YOU CAN REACH from inside any of them:
//
//    Owner()       Entity*        this script's entity
//    Transform()   Transform2D*   its position, rotation and scale
//    GetScene()    Scene*         to find or create other entities
//    OwnerId()     EntityId       this entity's id, for sending messages
//
//  All four work from OnStart onwards. Any of them can return null if the
//  entity has been destroyed, so check before using one.
// =============================================================================

#include <engine/Engine.h>

namespace {
using namespace eng;

class PlayerMovement final : public MyScriptBehaviour {
public:

    float movementSpeed = 1;
    float jumpHeight = 10;
    float msecondsAlive = 0.0f;

    void OnStart() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement started on '{}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }

    void OnUpdate()
    {
        direction =
            eng::InputMap::GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp");
    }

    void OnFixedUpdate() {
        ::Transform transform = Transform();
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

} // namespace

// Registers the name "PlayerMovement" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(PlayerMovement)
