// =============================================================================
//  Gun - fires a prefab when the "Fire" action is pressed.
//
//  THE WORKED EXAMPLE FOR SPAWNING. Together with Bullet.cpp and
//  ScoreKeeper.cpp it is the whole "shoot something and keep score" loop, which
//  is the first thing most people try.
//
//  -----------------------------------------------------------------------------
//  TO USE IT
//
//  1. Drag this file onto an entity in the Hierarchy or the Inspector.
//  2. Drag a prefab from the Assets panel onto the script's `bullet` slot.
//     assets/prefabs/bullet.oprefab is one that works out of the box.
//  3. Bind an action called "Fire" in config/engine.json. There is already one
//     there bound to Mouse.Left.
//
//  -----------------------------------------------------------------------------
//  WHY THE PREFAB IS A PrefabRef AND NOT A std::string
//
//  A std::string would also appear in the Inspector - as a plain text box. You
//  would have to remember the folder and the extension, nothing would check the
//  path, you could not drag the prefab onto it, and renaming the prefab would
//  break this silently. A PrefabRef is the same string with its meaning
//  attached. See engine/scene/PrefabRef.h.
//
//  -----------------------------------------------------------------------------
//  THE ORDER OF THE LAST THREE LINES IS THE INTERESTING PART
//
//  Instantiate hands back an EntityId, not a pointer, because an id is what
//  survives. FindEntity turns it into something you can reach the components
//  on, and the result is only good for this function - see the note in
//  ScriptComponent.h about asking again rather than remembering.
//
//  And DestroyAfter is what stops a bullet that hits nothing from existing
//  forever. While the fuse burns the bullet behaves completely normally: it
//  still collides, which is the whole reason DeferredOps keeps fuses in a list
//  of their own.
// =============================================================================

#include <engine/Engine.h>


class Gun final : public ScriptBehaviour {
public:
    PrefabRef bullet;              // what to fire - drag a prefab here
    float          speed    = 600.0f;   // how fast it leaves the barrel
    float          cooldown = 0.2f;     // [0..2] seconds between shots
    float          muzzle   = 24.0f;    // how far in front of the gun it appears

    void OnFixedUpdate() {
        // Counted down in FIXED time, so the rate of fire is the same on every
        // machine. A cooldown measured in frames is faster on a faster
        // computer, which is exactly the thing the fixed step exists to avoid.
        m_untilNext -= eng::Time::FixedDeltaTime();
        if (m_untilNext > 0.0f) {
            return;
        }
        if (!eng::InputMap::IsPressed("Fire")) {
            return;
        }
        m_untilNext = cooldown;

        eng::Transform2D* barrel = Transform();
        if (barrel == nullptr) {
            return;
        }

        // Right() is the direction this entity's own +X axis points in the
        // world, which for a sprite is "forwards". It is normalised, so a
        // half-size gun does not fire at half speed.
        const eng::Vec2 forward = barrel->Right();

        const eng::EntityId shot =
            Instantiate(bullet, eng::SpawnOptions{}
                                    .At(barrel->WorldPosition() + forward * muzzle)
                                    .Facing(barrel->WorldRotation()));
        if (shot.IsNull()) {
            return;   // Instantiate already said why, in the Console
        }

        if (eng::Entity* fired = FindEntity(shot)) {
            if (auto* body = fired->Find<eng::Rigidbody2DComponent>()) {
                body->SetVelocity(forward * speed);
            }
        }

        // If it hits nothing, it stops existing. Two seconds of game time -
        // so the fuse stops while the game is paused.
        DestroyAfter(shot, 2.0f);
    }

private:
    float m_untilNext = 0.0f;
};


ENGINE_REGISTER_SCRIPT(Gun)
