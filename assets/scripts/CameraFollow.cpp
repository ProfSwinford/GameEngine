// =============================================================================
//  CameraFollow - keeps the game camera pointed at this entity.
//
//  Attach it to whatever the camera should follow - usually the player. Nothing
//  in the engine does this on its own, so without this script (or something
//  like it) the camera stays exactly where the scene file's "camera" block put
//  it and the player walks off the edge of the view.
//
//  -----------------------------------------------------------------------------
//  WHICH CAMERA THIS MOVES, AND WHICH IT DOES NOT
//
//  Engine::GetCamera() is the GAME camera: what the Game view shows, what the
//  player executable renders through, and what a scene file's "camera" block
//  sets. That is the one this moves.
//
//  The editor's Scene view has a SECOND camera of its own, which this never
//  touches - so you can pan and zoom around the level while the game runs and
//  watch the player chase the camera somewhere else entirely. See
//  editor/src/panels/ScenePanel.h for why those are deliberately separate.
//
//  Pressing Stop puts the game camera back where the scene file had it, so
//  following during a play session never changes what gets saved.
//
//  -----------------------------------------------------------------------------
//  ATTACH IT AFTER the script that does the moving.
//
//  Scripts all run in one stage - 200, Gameplay - in the order their components
//  are listed. This one reads a position that PlayerMovement has just written,
//  so if it is listed FIRST it reads last tick's position and the camera trails
//  the player by one step forever.
//
//  One step is only about four pixels at this speed, which is exactly the kind
//  of bug that is too small to see and too annoying to ignore. In the Inspector
//  the order is top to bottom; in the scene file it is the order of the
//  "components" array.
//
//  (SystemOrder.h reserves stage 700 for camera work for precisely this reason
//  - "follow whatever it follows, AFTER it has moved". Nothing uses that stage
//  yet; a script cannot reach it, so the component order is what we have.)
//
//  -----------------------------------------------------------------------------
//  WHY THE SMOOTHING LOOKS LIKE THAT
//
//  The obvious way to ease the camera in is to move it a fraction of the way
//  each step:
//
//      position += (target - position) * 0.1f;     // DON'T
//
//  That fraction is "per step", not "per second", so the camera becomes twice
//  as fast on a machine that simulates twice as often. This engine's whole
//  fixed-step design exists to stop that happening, and it is worth not
//  undoing it in the one file where it is easiest to do by accident.
//
//      1 - exp(-rate * dt)
//
//  is the same ease-in written as a RATE PER SECOND, so it behaves identically
//  however often it runs. Raise `followRate` to make the camera stiffer; a very
//  large number is effectively a hard lock to the player. It is a slider in the
//  Inspector, so the right value is something to find by dragging it while the
//  game runs rather than something to guess at.
// =============================================================================

#include <engine/Engine.h>

#include <cmath>

namespace {

class CameraFollow final : public eng::ScriptBehaviour {
public:
    // How quickly the camera closes the gap, per second. About 8 is a gentle
    // follow you can feel; 50 is nearly rigid; 1 is dreamy and probably too
    // slow to play with.
    //
    // HOW FAR BEHIND IT SETTLES, if you want to pick this number rather than
    // guess at it: a target moving in a straight line at a steady speed ends
    // up a constant distance ahead, and that distance is
    //
    //     speed / rate
    //
    // The player moves at 220 units a second (PlayerMovement), so at a rate of
    // 8 the camera trails by about 27 units - a couple of percent of the
    // screen, which reads as weight rather than as lag. Want it half as far
    // behind? Double the rate.
    // A SLIDER AND NOT A DRAG BOX, which is what the [1..50] says. This number
    // has ends that mean something: below 1 the camera stops keeping up at all
    // and above 50 it is already a hard lock, so there is nothing out there to
    // find. Leave the marker off for a distance or a speed, where there is no
    // such limit and a slider would invent one.
    //
    // The ends are the widget's, not the engine's - a scene file may hold
    // anything, and nothing clamps it on load.
    float followRate = 7.0f; // [1..50] how hard the camera pulls, per second

    void OnStart() {
        // Start ON the target rather than easing in from wherever the scene
        // file left the camera, which otherwise looks like a swoop at the
        // start of every single play session.
        if (const eng::Transform2D* transform = Transform()) {
            eng::Engine::Get().GetCamera().SetPosition(transform->WorldPosition());
        }
    }

    void OnFixedUpdate() {
        const eng::Transform2D* transform = Transform();
        if (transform == nullptr) {
            return;   // this entity is on its way out
        }

        eng::Camera&    camera  = eng::Engine::Get().GetCamera();
        const eng::Vec2 target  = transform->WorldPosition();
        const eng::Vec2 current = camera.Position();

        // Frame-rate independent ease-in. See the note above.
        const float t = 1.0f - std::exp(-followRate * eng::Time::FixedDeltaTime());
        camera.SetPosition(current + (target - current) * t);

        // WORTH TRYING NEXT, if you want it: clamp the result so the camera
        // never shows the outside of the level, or leave a dead zone in the
        // middle of the screen that the player can move around in without the
        // camera reacting at all. Both are a few lines here and neither needs
        // anything from the engine.
    }

};

} // namespace

// Registers the name "CameraFollow" so that a scene file and the editor can
// find it. WITHOUT THIS LINE the file compiles and the script can never be
// attached.
ENGINE_REGISTER_SCRIPT(CameraFollow)
