// =============================================================================
//  TilemapWalker - walking around on a tilemap, with a walk cycle.
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
//
//  ==========================================================================
//  AND IT PLAYS AN ANIMATION FOR THE DIRECTION IT IS GOING
//
//  `walkAnimations` is an ARRAY INDEXED BY DIRECTION, and the Inspector shows
//  it as four boxes:
//
//      walkAnimations[0]   going right
//      walkAnimations[1]   going left
//      walkAnimations[2]   going up
//      walkAnimations[3]   going down
//
//  Put the name of an animation in each. They are names of animations stored
//  in the SHEET the entity's Sprite Animation component is already playing
//  from - so if that sheet holds "walk_left", that is what goes in the box.
//  Leave one blank for a direction your art does not have.
//
//  WITH NO INPUT IT FREEZES ON THE FRAME IT IS SHOWING. Let go half way
//  through a stride and the character stands there mid-stride, which is what a
//  walk cycle's own art already looks like at that moment.
//
//  It does NOT go back to the Sprite's own picture. That was the first version
//  of this and it reads as a jolt: the sprite's image is a pose chosen for the
//  scene, with nothing to do with the stride the animation had reached, so
//  every time you stopped walking the character snapped into it.
//
//  So the Sprite's own image is what you see BEFORE the first step and nothing
//  after it. Give it a standing pose if you like, but the walk cycle's frames
//  are what the character rests on once it has moved.
//
//  Nothing is filled in by default, because a default would be a guess about
//  somebody else's spritesheet. Until the boxes are filled in, this behaves
//  exactly as it did before.
//
//  ==========================================================================
//  AND IT READS INPUT ONCE A FRAME, NOT ONCE A STEP
//
//  Every input call in this file is in OnUpdate. OnFixedUpdate acts on what
//  was stored and asks the input layer nothing - one place where input enters
//  the script, which is the shape assets/scripts/PlayerMovement.cpp uses too.
//
//  THE COST IS ONE FRAME, and it is better known than discovered. A frame
//  latches new input, then runs between ZERO and five fixed steps, then runs
//  OnUpdate. OnUpdate is LAST, so what it stores is what the NEXT frame's
//  steps act on - about 17ms later at 60fps.
//
//  FOR THE MOVEMENT AXIS THAT IS THE WHOLE OF THE DIFFERENCE. A held key reads
//  the same from either hook, because the input layer updates once a frame and
//  every step of that frame sees the same answer - so reading it from
//  OnFixedUpdate was never stale and never missed anything. This split buys
//  tidiness there, not correctness.
//
//  FOR A PRESS IT IS THE DIFFERENCE BETWEEN WORKING AND NOT, and that is the
//  reason to do it at all. See OnUpdate.
// =============================================================================

#include <engine/Engine.h>

// SheetPathOf and SubAssetNameOf: the two functions that know how a path
// names one animation inside a sheet. Engine.h does not reach them.
#include <engine/resource/ImportSettings.h>

#include <cmath>
#include <string>

namespace {

// ---------------------------------------------------------------------------
//  Which way the player is going, and therefore which slot of the array.
//
//  AT NAMESPACE SCOPE rather than inside the class, because the exposer reads
//  the class body line by line looking for variables to put in the Inspector,
//  and an enumerator list is not something it should have to understand.
//
//  kStill is deliberately NOT one of the four. Standing still is not a
//  direction with an animation behind it; it is the absence of one, and the
//  array has no slot for it.
// ---------------------------------------------------------------------------
enum Facing {
    kRight = 0,
    kLeft,
    kUp,
    kDown,
    kFacingCount,
};

constexpr int kStill = -1;

// Nothing has been applied yet, so the first step applies whatever is true -
// including stopping, which is what makes a clip set to play on start stop as
// soon as the game begins if nobody is pressing anything.
constexpr int kNothingApplied = -2;

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

    // ======================================================================
    //  THE ANIMATIONS, ONE PER DIRECTION.
    //
    //  PUBLIC AND AN ARRAY, which is what puts four boxes in the Inspector:
    //  the exposer turns a fixed-size array into one row per element, named
    //  walkAnimations[0] to walkAnimations[3]. The order is the Facing enum
    //  above - right, left, up, down - which is what makes indexing it by a
    //  direction legal rather than lucky.
    //
    //  ----------------------------------------------------------------------
    //  NAMES, NOT PATHS, AND THAT IS THE COMPONENT'S DESIGN RATHER THAN A
    //  SHORTCUT.
    //
    //  SpriteAnimationComponent::Play takes the NAME of an animation stored in
    //  the sheet it is already playing from, so "walk_left" is the whole of
    //  what identifies one. Writing "textures/hero.png#walk_left" here would
    //  repeat the sheet's path in all four boxes and break the day the sheet
    //  was renamed - which is exactly why Play does not accept one.
    //
    //  It is also the cheaper call by a wide margin. Play keeps the frames it
    //  has already cut and restarts the clip; setting a PATH re-reads the
    //  sheet and re-loads every frame, which is not something to do each time
    //  somebody changes direction.
    //
    //  See engine/include/engine/scene/SpriteAnimation.h.
    //  ----------------------------------------------------------------------
    //
    //  A BLANK BOX MEANS "nothing for that direction", which is left alone
    //  rather than treated as a mistake: a side-on character has no up or
    //  down, and walking up should then keep whatever was already playing.
    //
    //  ----------------------------------------------------------------------
    //  WHAT MAKES THEM PICKERS AND NOT TEXT BOXES IS THE `[animation]` AT THE
    //  END OF THE DECLARATION BELOW.
    //
    //  Worth saying here, because it is the one thing about these four boxes
    //  that the rest of this comment block does not mention - and it was read
    //  as hand-wiring, which is a fair conclusion to reach when the marker
    //  sits at the far right of a line thirty lines below where the
    //  explanation starts.
    //
    //  IT IS A GENERAL MARKER AND HAS NOTHING TO DO WITH THIS SCRIPT.
    //  `// [animation]` on any public std::string - or on any fixed-size
    //  array of them, in any script - gets the slot: the clip's name, a drop
    //  target and a picker. `[image]` does the same for a picture. Case does
    //  not matter, `[anim]` and `[clip]` also mean an animation, and the
    //  marker may sit on the comment line DIRECTLY ABOVE the declaration
    //  instead - which is where a variable with this much to explain wants
    //  it. See eng::AssetFieldKind in engine/scene/ScriptFields.h.
    //
    //  WITHOUT IT A STRING IS JUST A STRING and you get a box to type a path
    //  into. That is not a lesser version of this - a label's text is not a
    //  file, and a file picker on it would be wrong - but it is what a second
    //  array of animation names added without the marker comes out as.
    //  ----------------------------------------------------------------------
    // ======================================================================
    std::string walkAnimations[kFacingCount];   // [animation] right, left, up, down

    // ======================================================================
    //  AND THE SAME THING AGAIN, WHICH IS THE POINT OF IT BEING HERE.
    //
    //  THE MARKER IS ON THE LINE ABOVE THE DECLARATION this time rather than
    //  at the end of it. That is the other place it is allowed, and the
    //  reason that form exists: a variable with a comment block over it has
    //  nowhere good to put a trailing marker. Neither of these two arrays is
    //  known to the engine or to the editor by name.
    //
    //  NOTHING READS THESE YET. There is no attack in this script, so the
    //  four rows they add to the Inspector drive nothing - they are here
    //  because "can I add another one the same way?" was worth answering in
    //  the file that asked it. Delete them once it stops being useful.
    // ======================================================================
    std::string attackAnimations[4]; // [animation] one per direction, as above

    void OnStart() {
        m_body = Find<eng::Rigidbody2DComponent>();
        if (m_body == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kGame,
                             "TilemapWalker on '{}' needs a Rigidbody2DComponent "
                             "beside it, and there is none - it will not move",
                             name);
            return;
        }

        // A character that tips over when it bumps a step is not a player, it
        // is a log. Set here rather than in the scene file so the script
        // carries its own requirements with it.
        m_body->SetFreezeRotation(true);
    }

    // ======================================================================
    //  WHERE INPUT IS READ, and the only place in this file that asks the
    //  input layer anything.
    //
    //  PUBLIC, AND THAT IS NOT INCIDENTAL. A hook is found by asking whether
    //  the class has one that can be CALLED, so a PRIVATE OnUpdate is not
    //  found and the script simply never updates. Nothing warns: the check
    //  that catches a misspelled hook is already satisfied by the public
    //  OnStart and OnFixedUpdate above, so it has nothing to complain about.
    // ======================================================================
    void OnUpdate() {
        // ALREADY UNIT LENGTH OR ZERO, because GetAxis2D normalises - so a
        // diagonal is the same speed as a cardinal and there is nothing here
        // that should be rescaling it.
        m_movement = Input::GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp");

        // ==================================================================
        //  THE JUMP, FOR WHEN IT COMES BACK - AND WHY IT IS LATCHED HERE.
        //
        //  A PRESS EXISTS FOR ONE FRAME. IsPressed is true on the frame the
        //  key went down and false ever after, and a frame runs between ZERO
        //  and five fixed steps: zero at any frame rate above 60, five when
        //  the frame rate collapses. Asked from OnFixedUpdate a press is
        //  therefore DROPPED on a fast frame and handled FIVE TIMES on a slow
        //  one. This hook runs on every frame, so latching it here is the only
        //  way to see each press exactly once.
        //
        //  `||` AND NOT `=`, because this hook also runs on frames that no
        //  step follows. Assigning would clear a press nothing had acted on
        //  yet; or-ing keeps it until a step consumes it.
        //
        //  NOT IsDown, which is what this line used to say. IsDown is
        //  "pressed OR held" - true for as long as the key is down - so it
        //  applied an impulse on every step while the key was held, which is a
        //  rocket rather than a jump. A latch fed from IsDown would not have
        //  fixed that: the query itself was the bug.
        // ==================================================================
        //m_jumpQueued = m_jumpQueued || Input::IsPressed("Jump");
    }

    void OnFixedUpdate() {
        if (m_body == nullptr) {
            return;
        }

        // WHAT OnUpdate SAW, and nothing is asked of the input layer here.
        // See the header for the one frame that costs.
        //
        // THE PLATFORMER VERSION IS THE COMMENTED PAIR BELOW, and it is what
        // the top of this file still describes: horizontal set outright,
        // vertical LEFT ALONE so gravity keeps working. The live line sets
        // both, which is what a top-down character wants and is what this is
        // currently doing.
        //const Vector2 velocity = m_body->Velocity();
        //m_body->SetVelocity(Vector2{m_movement.x * walkSpeed, velocity.y});

        m_body->SetVelocity(m_movement * walkSpeed);

        //m_grounded = IsOnGround();
        //
        //if (m_jumpQueued) {
        //    // CLEARED BY THE FIRST STEP THAT SEES IT, which is what makes
        //    // one press worth one jump however many steps this frame runs.
        //    //
        //    // Cleared HERE rather than inside the grounded test, so a press
        //    // made in mid-air is SPENT rather than saved. Moving this line
        //    // inside the test would buffer it until landing instead, which
        //    // is a question about feel and not about correctness.
        //    m_jumpQueued = false;
        //
        //    if (m_grounded) {
        //        // An impulse and not a force: a jump is one kick, not a push
        //        // spread over the step. See ForceMode in physics/Rigidbody.h.
        //        m_body->AddForce(Vector2{0.0f, jumpImpulse}, eng::ForceMode::Impulse);
        //    }
        //}
    }

private:
    // Which slot of the array a pair of axes is asking for, or kStill.
    //
    // A DEAD ZONE RATHER THAN != 0. A stick at rest reads a little off centre,
    // and without one a controller left alone on the desk would flicker
    // between walking and standing several times a second.
    //
    // THE BIGGER AXIS WINS, so a diagonal picks one animation rather than
    // alternating between two. Horizontal wins a tie, which is the right
    // answer for anything side-on.
    static int FacingFor(float horizontal, float vertical) {
        constexpr float kDeadZone = 0.2f;

        const float across = std::fabs(horizontal);
        const float along  = std::fabs(vertical);

        if (across < kDeadZone && along < kDeadZone) {
            return kStill;
        }
        if (across >= along) {
            return horizontal > 0.0f ? kRight : kLeft;
        }
        return vertical > 0.0f ? kUp : kDown;
    }

    // Puts the right animation on, or takes it off.
    //
    // ======================================================================
    //  ONLY WHEN IT CHANGES, AND THAT IS NOT AN OPTIMISATION.
    //
    //  Play() restarts a clip from its first frame. Calling it every step
    //  would therefore pin the animation to frame 0 for ever - a walk cycle
    //  that never walks, which looks like a broken animation rather than like
    //  a script asking for it sixty times a second.
    //
    //  So the last thing asked for is remembered, and asking again for the
    //  same thing does nothing at all.
    // ======================================================================
    void Show(int facing) {
        if (facing == m_showing) {
            return;
        }
        m_showing = facing;

        // ASKED FOR EACH TIME, NEVER CACHED. A component dies with its entity,
        // and the whole scene is rebuilt by Play, by Stop and by every undo
        // step - so a pointer kept from OnStart is the one thing here that
        // could dangle. This runs only when the direction changes, so it is a
        // string compare over the two or three components an entity has, a few
        // times a second at most. See docs/scripting-reference.md.
        auto* animation = Find<eng::SpriteAnimationComponent>();
        if (animation == nullptr) {
            return;   // no animation on this entity; the movement still works
        }

        if (facing == kStill) {
            // ==============================================================
            //  PAUSED, NOT STOPPED, and the difference is the whole of what
            //  this does when you let go of the keys.
            //
            //  Pause leaves the frame that is on screen exactly where it is.
            //  Stop ALSO rewinds and puts the sprite's own picture back, and
            //  that second half is the jarring one: the sprite's image is a
            //  pose chosen for the scene, so stopping mid-stride snapped the
            //  character into something unrelated to the frame it was on.
            //
            //  Mid-stride is a pose the art already contains. Standing on it
            //  is seamless by construction; cutting to a different picture is
            //  not.
            //
            //  NOTHING IS REWOUND EITHER, so the clip keeps its place - and
            //  because Show only acts on a CHANGE, walking off again in the
            //  same direction asks for that direction afresh and restarts the
            //  cycle from its first frame. For a loop that is the join it
            //  makes on every lap anyway. If you would rather it carried on
            //  from where it froze, Play() with no argument resumes instead.
            // ==============================================================
            animation->Pause();
            return;
        }

        // THE ARRAY, INDEXED BY THE DIRECTION. The bounds are the enum's own,
        // so there is nothing to check: FacingFor returns one of the four or
        // kStill, and kStill was handled above.
        const std::string& wanted = walkAnimations[facing];
        if (wanted.empty()) {
            // Nothing set for this direction. The clip that was playing is
            // LEFT ALONE rather than stopped, so a side-on character walking
            // up keeps its sideways walk instead of freezing.
            return;
        }

        // ==================================================================
        //  A NAME *OR* A PATH, because both are things that end up in the box.
        //
        //  Dragging an animation out of the Assets panel gives a full path -
        //  "textures/hero.png#walk_left" - and typing gives a bare name.
        //  Accepting only one of them would make the other silently do
        //  nothing, which is exactly how this was reported.
        //
        //  A PATH NAMING THE SHEET ALREADY PLAYING IS REDUCED TO ITS NAME,
        //  because Play is far cheaper: it keeps the frames it has already cut
        //  and restarts the clip, where setting a path re-reads the sheet and
        //  re-loads every frame. Only a DIFFERENT sheet needs the whole path.
        // ==================================================================
        const std::string inside = eng::SubAssetNameOf(wanted);

        if (inside.empty()) {
            // Not a path at all, so it is a name. Play reports a name no sheet
            // has - once, in the Console, with the Sprite Animation row in the
            // Inspector saying the same thing.
            animation->Play(wanted);
            return;
        }

        if (eng::SheetPathOf(wanted) == animation->SheetPath()) {
            animation->Play(inside);
            return;
        }

        // ANOTHER SHEET. SetAnimationPath honours the component's "play when
        // the scene starts" tick box, which is the authored intent for the
        // FIRST clip and not for this one - so it is asked to play straight
        // after, or an entity set not to play on start would change direction
        // and stand still.
        if (animation->SetAnimationPath(wanted)) {
            animation->Play();
        }
    }

    // A short ray straight down from just inside the feet.
    //
    // WHY A RAY AND NOT "did I collide with something below me": a contact
    // tells you where you ARE, and it arrives on the step after you land. A
    // ray tells you where the ground IS, which is the question a jump is
    // actually asking - and it keeps answering while standing still, when
    // there is no new contact at all.
    bool IsOnGround() const {
        // `transform` is the inherited handle, not a local - see the note above
        // ScriptHandle in engine/scene/ScriptComponent.h. It asks the entity
        // afresh on every use, so there is nothing here to go stale.
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

    // A POINTER, which the Inspector could not edit anyway - there is nothing
    // to put in a box for an address. PRIVATE rather than public-with-an-m_
    // prefix: `private:` is the language's own way of saying "not part of the
    // interface", and the exposer no longer reads anything into a name.
    // SPELLED Rigidbody2D, which it was not until recently: this line read
    // `Ridigbody2D` - the misspelled compatibility alias - and that alias has
    // since been removed from Engine.h, which left this file unable to compile
    // at all. A project's scripts are built by the EDITOR and not by the
    // engine's own build, so nothing reported it.
    Rigidbody2D                m_body = nullptr;

    bool                       m_grounded = false;

    // WHAT OnUpdate SAW AND OnFixedUpdate ACTS ON.
    //
    // Written once a frame in OnUpdate, read by every fixed step of the
    // FOLLOWING frame - OnUpdate runs after the step loop, so there is one
    // frame between the two. Zero until the first OnUpdate has run, which is
    // the right answer for a character nobody has told to move yet.
    //
    // Unit length or zero and never longer, because GetAxis2D normalises - so
    // `m_movement * walkSpeed` is the same speed in all eight directions.
    Vector2                    m_movement{};

    // THE JUMP'S LATCH, for when the jump in OnUpdate comes back. A bool and
    // not a count: two presses inside one frame are one jump, which is what
    // somebody who mashed the key meant by it.
    //bool                     m_jumpQueued = false;

    // What Show() last asked for, so it can tell a change from a repeat.
    int                        m_showing = kNothingApplied;
};

} // namespace

ENGINE_REGISTER_SCRIPT(TilemapWalker)
