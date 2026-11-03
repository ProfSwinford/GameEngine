// =============================================================================
//  Bullet - hits something tagged "Enemy", tells the scorekeeper, and both go.
//
//  THE WORKED EXAMPLE FOR TAGS AND FOR DESTROYING THINGS. Attach it to the
//  prefab Gun.cpp fires; assets/prefabs/bullet.oprefab already has it.
//
//  -----------------------------------------------------------------------------
//  WHY A TAG AND NOT A COLLISION LAYER
//
//  Before tags existed, the only shared label available was the collider's
//  layer, so the engine's own scripts borrowed it - Pickup.cpp still asks
//  `FindCollider(*partner)->Layer() == "Player"`. That works until you want to
//  label something WITHOUT changing what it collides with, and it does not work
//  at all on something with no collider. A tag is just a label. See
//  engine/scene/Entity.h.
//
//  -----------------------------------------------------------------------------
//  THE PARTNER IS LOOKED UP FRESH, EVERY TIME
//
//  `other` is an EntityId, not a pointer, and the thing it refers to may
//  already have been destroyed this step - by another bullet that arrived
//  first. FindEntity answers null for that, which is why there is a check and
//  not an assumption.
//
//  -----------------------------------------------------------------------------
//  AND DESTROYING TWICE IS FINE
//
//  Two bullets hitting one enemy on the same tick is ordinary, so the queue
//  ignores the second request rather than treating it as a mistake. Nothing
//  here has to check first. See rule 3 in engine/scene/DeferredOps.h.
// =============================================================================

#include <engine/Engine.h>

namespace {

class Bullet final : public eng::ScriptBehaviour {
public:
    int         damage = 1;          // how much this takes off the target
    std::string hits   = "Enemy";    // the tag of the things it damages

    void OnTriggerEnter(eng::EntityId other) {
        eng::Entity* hit = FindEntity(other);
        if (hit == nullptr || !hit->HasTag(hits)) {
            return;   // a wall, or something already gone
        }

        // ------------------------------------------------------------------
        //  TOLD BY MESSAGE RATHER THAN BY CALLING INTO IT.
        //
        //  A message needs no headers at all: this file knows nothing about
        //  the ScoreKeeper class and does not have to. The alternative -
        //  FindScriptIn<ScoreKeeper> - needs that class to be nameable from
        //  here, which means moving it into a shared header. Worth doing when
        //  you need its own methods; not worth it to add a number.
        // ------------------------------------------------------------------
        if (eng::ScriptComponent* rules =
                FindScriptNamedIn(*GetScene(), "ScoreKeeper")) {
            eng::Message scored;
            scored.type   = "Scored";
            scored.target = rules->OwnerId();
            scored.sender = OwnerId();
            scored.i0     = damage;
            scored.other  = other;
            eng::MessageBus::Send(scored);
        }

        eng::Gizmos::Circle(hit->Transform().WorldPosition(), 20.0f,
                            eng::Color::Yellow(), 0.25f);

        Destroy(other);
        DestroySelf();
    }
};

} // namespace

ENGINE_REGISTER_SCRIPT(Bullet)
