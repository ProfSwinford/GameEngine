// =============================================================================
//  Destroy - a script.
//
//  Attach it by dragging this file from the Assets panel onto an entity in the
//  Hierarchy or the Inspector.
//
//  -----------------------------------------------------------------------------
//  THIS IS COMPILED C++, NOT AN INTERPRETED SCRIPT - but you never rebuild the
//  editor. Save this file, switch back to the editor window, and it compiles
//  and reloads by itself. There is a Build Scripts button in the Assets panel
//  for when you would rather not alt-tab.
//
//  If it does not compile, the errors appear in the Console with the file and
//  line, and the menu bar says so.
//
//  -----------------------------------------------------------------------------
//  DELETE THE HOOKS YOU DO NOT NEED. All of them are optional, and that is
//  literal: there is no `override` and nothing to declare. The engine works out
//  which of these functions you actually wrote, while your script is being
//  compiled, and only calls those. A script with no OnUpdate is not asked to
//  update - it costs nothing at all per frame.
//
//  The catch is the same one every engine that works this way has: a MISSPELLED
//  hook is not an error, it is just a function nobody calls. If your script
//  seems to do nothing, check the Console - it lists every script it loaded
//  together with the hooks it found in it.
//
//  -----------------------------------------------------------------------------
//  THE LIFECYCLE.
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

#include <engine/Engine.h>

using namespace eng;

namespace {

class Destroy final : public ScriptBehaviour {
public:
    void OnStart() {
        ENGINE_LOG_INFO(Channels::kGame, "Destroy started on '{}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }

    void OnUpdate(float deltaSeconds) {
        Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        // Replace this with whatever your script should do. It is here so that
        // a brand new script does something visible the first time you press
        // Play - a template that compiles and then appears to do nothing is
        // indistinguishable from one that failed to attach.
        m_secondsAlive += deltaSeconds;
    }

    void OnDestroy() {
        ENGINE_LOG_INFO(Channels::kGame, "Destroy lived {:.2f} seconds",
                        m_secondsAlive);
    }

    void OnCollisionEnter(EntityId other) {
        // The other entity is looked up fresh rather than remembered, because
        // it may already have been destroyed this step.
        Scene* scene = GetScene();
        if (scene == nullptr) {
            return;
        }
        const Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(Channels::kGame, "Destroy touched '{}'",
                        partner != nullptr ? partner->Name() : "<already gone>");
    }

private:
    float m_secondsAlive = 0.0f;
};

} // namespace

// Registers the name "Destroy" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(Destroy)
