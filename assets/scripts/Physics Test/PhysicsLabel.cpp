// ============================================================================
//  PhysicsLabel - a line of text pinned to a place in the world.
//
//  One of these sits on each station of the physics playground and says what
//  the station is for. The text is a VARIABLE, so a station is renamed in the
//  Inspector rather than in this file, and one script serves all of them.
//
//  ==========================================================================
//  IT ONLY APPEARS WHILE PLAYING, AND THAT IS NOT A BUG TO FIX HERE.
//
//  Scripts do not run while the clock is paused, which is the whole of the
//  time you spend editing - see the gate at the end of Engine::Simulate. So
//  while you are building a scene these labels are not there, and the way to
//  find your way around instead is the Hierarchy: every station is a parent
//  entity named after itself, and selecting one draws its name and bounds in
//  the Scene view.
//
//  WHAT YOU DO SEE WHILE EDITING IS THE COLLIDERS, which is the thing worth
//  seeing. Those are drawn by the Scene view itself and not by any script;
//  see CollisionSystem::DrawGizmos.
// ============================================================================

#include <engine/Engine.h>


class PhysicsLabel final : public eng::ScriptBehaviour {
public:
    std::string title;   // the big line, drawn at the entity's position
    std::string note;    // the smaller line under it - what to look for

    // IN WORLD UNITS, because the label belongs to a place in the level and
    // has to stay with it when the camera moves. A screen-space label would
    // sit still while the station it names scrolled away underneath it.
    eng::Vec2 offset{0.0f, 0.0f};   // where the text sits, relative to here

    eng::Color titleColour{245, 205, 66, 255};   // the heading
    eng::Color noteColour{150, 150, 160, 255};   // the line under it

    // How far under the title the note goes. Negative, because world Y is up.
    float noteGap = -26.0f;   // [-80..0] world units below the title

    void OnUpdate() {
        if (transform == nullptr) {
            return;
        }

        // ASKED EVERY FRAME, and drawn with the default lifetime of zero so
        // that it lasts exactly this frame. That is what makes dragging the
        // entity - or editing the text - show up immediately, with nothing to
        // tell that anything changed.
        const eng::Vec2 at = transform->WorldPosition() + offset;

        if (!title.empty()) {
            eng::Gizmos::Text(at, title, titleColour, 0.0f, eng::GizmoSpace::World);
        }
        if (!note.empty()) {
            eng::Gizmos::Text(at + eng::Vec2{0.0f, noteGap}, note, noteColour, 0.0f,
                              eng::GizmoSpace::World);
        }
    }
};


ENGINE_REGISTER_SCRIPT(PhysicsLabel)
