// =============================================================================
//  TilemapWalker - walking around on a tilemap.
//
//  A small platformer controller, written to show the three things a tilemap
//  needs from the rest of the engine working together:
//
//    * the TILEMAP COLLIDER being an ordinary collider, so nothing here knows
//      it is standing on a tilemap rather than on a box;
//    * a RAYCAST answering "is there ground under my feet?", which is how a
//      jump knows whether it is allowed;
//    * OnFixedUpdate and Time, so the same code behaves the same at any frame
//      rate.
//
//  ==========================================================================
//  IT MOVES BY SETTING VELOCITY, NOT BY MOVING THE TRANSFORM
//
//  This entity has a Rigidbody2D, and the physics step owns its position. A
//  script that wrote Transform()->Translate every step would be fighting the
//  solver: the solver pushes the body out of a wall, the script puts it back
//  in, and the result is a character that shudders against every surface and
//  occasionally goes through one.
//
//  Horizontal velocity is set outright because walking is not something you
//  accelerate into; vertical velocity is LEFT ALONE so gravity keeps working.
//  That one asymmetry is most of what makes a controller feel right.
// =============================================================================

#include <engine/Engine.h>

namespace {

class TilemapWalker final : public MyScriptBehaviour {
public:
    // ---- what a platformer is actually made of --------------------------
    //
    // These three were `static constexpr` constants, which meant that tuning
    // the feel of the movement was a rebuild each time. They are ordinary
    // members now, so the Inspector shows them and they can be dragged WHILE
    // THE GAME RUNS - which is the only honest way to find out what a jump
    // height should be.
    //
    // Each one is read where it is used rather than copied at OnStart, so a
    // change takes effect on the next step with nothing to notify.
    float walkSpeed   = 260.0f;   // pixels a second, flat out
    float jumpImpulse = 520.0f;   // one upward kick. Gravity decides the height.

    // HALF THE CHARACTER'S HEIGHT PLUS A LITTLE, which is why it belongs on
    // the entity and not in the code: a different sprite needs a different
    // number, and before this it needed a different script.
    //
    // Too short and a jump is refused while standing on the floor; too long
    // and it is allowed in mid-air.
    float groundProbe = 30.0f;   // [1..200] how far down to look for a floor

    // A POINTER, so it is not exposed and could not be - the Inspector edits
    // values, and there is nothing to put in a box for an address. The Console
    // says so by name when it builds this file, which is how a variable that
    // did not appear is told from one that was never meant to.
    Ridigbody2D m_body = nullptr;

    void OnStart() {
        m_body = Owner() != nullptr ? Owner()->Find<eng::Rigidbody2DComponent>()
                                    : nullptr;
        if (m_body == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kGame,
                             "TilemapWalker on '{}' needs a Rigidbody2DComponent "
                             "beside it, and there is none - it will not move",
                             Owner() != nullptr ? Owner()->Name() : "<none>");
            return;
        }

        // A character that tips over when it bumps a step is not a player, it
        // is a log. Set here rather than in the scene file so the script
        // carries its own requirements with it.
        m_body->SetFreezeRotation(true);
    }

    void OnFixedUpdate() {
        if (m_body == nullptr) {
            return;
        }

        const float horizontal =
            Input::GetAxis("MoveRight") - Input::GetAxis("MoveLeft");

        const Vector2 velocity = m_body->Velocity();
        m_body->SetVelocity(Vector2{horizontal * walkSpeed, velocity.y});

        m_grounded = IsOnGround();

        if (m_grounded && Input::IsDown("Jump")) {
            // An impulse and not a force: a jump is one kick, not a push
            // spread over the step. See ForceMode in physics/Rigidbody.h.
            m_body->AddForce(Vector2{0.0f, jumpImpulse}, eng::ForceMode::Impulse);
        }
    }

private:
    // A short ray straight down from just inside the feet.
    //
    // WHY A RAY AND NOT "did I collide with something below me": a contact
    // tells you where you ARE, and it arrives on the step after you land. A
    // ray tells you where the ground IS, which is the question a jump is
    // actually asking - and it keeps answering while standing still, when
    // there is no new contact at all.
    bool IsOnGround() const {
        // `::Transform` and not `Transform`. Inside a ScriptBehaviour, the
        // unqualified name finds the inherited FUNCTION Transform() - a member
        // hides anything at global scope - so the typedef in Engine.h is only
        // reachable from here with the `::` in front of it.
        const ::Transform transform = Transform();
        if (transform == nullptr) {
            return false;
        }

        const Vector2 from = transform->WorldPosition();

        eng::QueryFilter filter;
        filter.ignore      = OwnerId();   // or the ray finds this very collider
        filter.hitTriggers = false;       // a trigger is not a floor

        const eng::RaycastHit2D hit =
            eng::Physics2D::Raycast(from, eng::Vec2{0.0f, -1.0f}, groundProbe, filter);

        // Pointing UPWARD, so a wall brushed on the way past is not a floor.
        // Without this check a character can jump repeatedly off a vertical
        // surface and climb it.
        return hit && hit.normal.y > 0.5f;
    }

    bool                       m_grounded = false;
};

} // namespace

ENGINE_REGISTER_SCRIPT(TilemapWalker)
