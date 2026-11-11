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

class Bullet final : public eng::ScriptBehaviour {
public:
    ImageRef sprite;                 // what the bullet looks like - drag a sprite here
    SpriteComponent* m_renderer = nullptr; // the SpriteComponent beside this script

    int         damage = 1;          // how much this takes off the target
    std::string hits   = "Enemy";    // the tag of the things it damages

    void OnStart() {
        // ASKED FOR HERE RATHER THAN KEPT IN A MEMBER. A component dies with
        // its entity, and the whole scene is rebuilt by Play, by Stop and by
        // every undo step - so the rule in docs/scripting-reference.md is to
        // ask with Find each time. This needs it once, on the way in, so a
        // local is the whole of it.
        m_renderer = Find<eng::SpriteComponent>();
        if (m_renderer == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kGame,
                             "Bullet on '{}' needs a SpriteComponent "
                             "beside it, and there is none - it will not draw",
                             name);
            return;
        }
        // ONLY WHEN A PICTURE WAS ACTUALLY DRAGGED IN.
        //
        // An unset ImageRef holds an empty path, and SetTexture treats an
        // empty path as "no picture" and clears the slot - deliberately, so
        // that emptying the field in the Inspector works. bullet.oprefab
        // already authors `"texture": "textures/checker_red.bmp"` on the
        // SpriteComponent, so calling this unguarded on a prefab whose
        // script slot was never filled in blanks that texture and every
        // bullet fires invisibly.
        //
        // So the field is an OVERRIDE, not a requirement: leave it empty and
        // the sprite keeps whatever the prefab gave it.
        if (sprite.IsSet()) {
            m_renderer->SetTexture(sprite);
        }
    }

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

ENGINE_REGISTER_SCRIPT(Bullet)
