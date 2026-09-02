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
//    OnUpdate(dt)         Every FIXED simulation step. NOT once per drawn frame -
//                         this engine simulates at a steady rate and draws
//                         separately, so this runs a whole number of times per
//                         frame, sometimes twice and sometimes not at all. That
//                         is what makes the game behave the same on every
//                         machine, and it is why you multiply by dt instead of
//                         assuming a frame rate.
//
//    OnDestroy()          The entity is going away. It is still safe to touch
//                         here and not afterwards.
//
//    OnCollisionEnter     ENTER fires once when an overlap begins, STAY every
//    OnCollisionStay      step it continues, and EXIT once when it ends -
//    OnCollisionExit      including when the other entity is destroyed while
//                         still overlapping.
//
//                         `other` is an EntityId, not a pointer, and the thing
//                         it refers to may already be gone. Look it up through
//                         the scene every time; never keep a pointer to it.
//
//                         These need a collider on BOTH entities, and each one's
//                         "collides with" list has to include the other's layer.
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

//#include <engine/core/Log.h>
//#include <engine/math/Transform2D.h>
//#include <engine/scene/Entity.h>
//#include <engine/scene/Scene.h>
//#include <engine/scene/ScriptComponent.h>
//#include <engine/input/InputMap.h>
#include <engine/Engine.h>

namespace {

class PlayerMovement final : public eng::ScriptBehaviour {
public:
    void OnStart() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement started on '{}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }

    void OnUpdate(float deltaSeconds) {
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        // Replace this with whatever your script should do. It is here so that
        // a brand new script does something visible the first time you press
        // Play - a template that compiles and then appears to do nothing is
        // indistinguishable from one that failed to attach.
        //m_secondsAlive += deltaSeconds;

        const eng::Vec2 direction =
        eng::InputMap::GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp");

        // Multiply by deltaSeconds, always - that is what makes the speed a
        // speed rather than "however fast this machine happens to run".
        transform->Translate(direction * (kSpeed * deltaSeconds));

    }

    void OnDestroy() {
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement lived {:.2f} seconds",
                        m_secondsAlive);
    }

    void OnCollisionEnter(eng::EntityId other) {
        // The other entity is looked up fresh rather than remembered, because
        // it may already have been destroyed this step.
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return;
        }
        const eng::Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(eng::Channels::kGame, "PlayerMovement touched '{}'",
                        partner != nullptr ? partner->Name() : "<already gone>");
    }

private:
    float m_secondsAlive = 0.0f;
    static constexpr float kSpeed = 220.0f;
};

} // namespace

// Registers the name "PlayerMovement" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(PlayerMovement)
