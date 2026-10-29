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
//  compiled, and only calls those. A script with no update is not asked to
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

using namespace eng;

namespace {

class Destroy final : public ScriptBehaviour {
public:
    void OnStart() {
        ENGINE_LOG_INFO(Channels::kGame, "Destroy started on '{}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }

    void OnFixedUpdate() {
        Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;
        }

        // Replace this with whatever your script should do. It is here so that
        // a brand new script does something visible the first time you press
        // Play - a template that compiles and then appears to do nothing is
        // indistinguishable from one that failed to attach.
        m_secondsAlive += Time::FixedDeltaTime();
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
