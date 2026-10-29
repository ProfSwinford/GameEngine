// =============================================================================
//  Pickup - one collectible. Touch it with the player and it disappears.
//
//  ITS COLLIDER MUST BE A TRIGGER, which is what a collectible is: the player
//  walks through it rather than bumping into it. That is why the hook here is
//  OnTriggerEnter and not OnCollisionEnter - a trigger pair sends one and not
//  the other. See physics/Collider.h.
//
//  Attach it by dragging this file from the Assets panel onto an entity in the
//  Hierarchy or the Inspector.
//
//  -----------------------------------------------------------------------------
//  THE PICKUP DESTROYS ITSELF. Nothing else in the project knows how a pickup
//  is collected.
//
//  The obvious alternative is for the player's script to destroy whatever it
//  touches. That works until the second kind of collectible, and then the
//  player's script has to know about every one of them - a file that grows
//  every time you add anything to the game. Here, a new kind of collectible is
//  a new script on a new entity, and nothing existing is edited.
//
//  This also means the pickup is the thing that reports itself in the Console,
//  by name, which is what you want when you are wondering why one particular
//  one did not vanish.
//
//  -----------------------------------------------------------------------------
//  WHY QueueDestroy AND NOT A PLAIN DESTROY
//
//  OnTriggerEnter runs during message delivery, while the collision system is
//  part way through its list of touching pairs and the render system is part
//  way through its list of sprites. Removing an entity at that moment pulls the
//  ground out from under both of them.
//
//  So the request is QUEUED and carried out at stage 600, between collision and
//  drawing, where nothing is mid-walk. See scene/DeferredOps.h. Every other
//  part of the engine that destroys something goes through the same queue,
//  including the Inspector's delete button.
// =============================================================================

#include <engine/Engine.h>

namespace {

class Pickup final : public eng::ScriptBehaviour {
public:
    void OnTriggerEnter(eng::EntityId other) {
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {
            return;
        }

        // Looked up fresh rather than remembered. `other` is an id, and the
        // entity it names may already have been destroyed this step - which is
        // exactly why collisions report ids instead of pointers.
        eng::Entity* partner = scene->Get(other);
        if (partner == nullptr) {
            return;
        }

        // ONLY THE PLAYER COLLECTS. Asked of the other entity's DATA - the
        // layer on its collider - rather than its name, so a second player or
        // a renamed one still works and nothing here has a list of names in
        // it.
        const auto* collider = eng::FindCollider(*partner);
        if (collider == nullptr || collider->Layer() != "Player") {
            return;
        }

        // ALREADY ON ITS WAY OUT. Enter can fire twice in one step when two
        // colliders overlap this pickup, and queueing the same destroy twice
        // would count it twice.
        if (eng::DeferredOps::IsPendingDestroy(OwnerId())) {
            return;
        }

        // A circle that stays for three seconds where the pickup was. The
        // event happened once and was over instantly; the marker is what lets
        // you go and look at where it happened.
        eng::Entity* self = Owner();
        if (self != nullptr) {
            eng::Gizmos::Circle(self->Transform().WorldPosition(), 18.0f,
                                eng::Color::Yellow(), 3.0f);
            ENGINE_LOG_INFO(eng::Channels::kGame, "collected '{}'", self->Name());
        }

        eng::DeferredOps::QueueDestroy(OwnerId());
    }
};

} // namespace

// Registers the name "Pickup" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(Pickup)
