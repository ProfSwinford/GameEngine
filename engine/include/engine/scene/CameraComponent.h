#pragma once

// ============================================================================
//  CameraComponent.h - the game camera, as an entity like everything else.
//
//  ==========================================================================
//  WHY THE CAMERA IS AN ENTITY
//
//  It used to be a bare Camera object owned by the Engine, with its position
//  written into the scene file as a "camera" block. That worked, and it made
//  three ordinary things awkward:
//
//    * YOU COULD NOT PUT A COMPONENT ON IT. A camera that shakes, follows a
//      player, or snaps between rooms is a behaviour - and behaviours are
//      scripts, which attach to entities. Following had to be a script on
//      something ELSE that reached over and moved the engine's camera.
//
//    * IT WAS NOT IN THE HIERARCHY. You could not select it, see where it
//      was, or tell at a glance whether something had moved it. When the view
//      ended up somewhere unexpected there was nothing to click on.
//
//    * IT COULD NOT BE PARENTED. Attaching the camera to a vehicle is just
//      parenting, once the camera is an entity.
//
//  Now it is an entity with a CameraComponent, and all three fall out for
//  free. Its POSITION is its transform's - there is no separate position
//  field, because two places to say where the camera is means two places to
//  disagree.
//
//  ==========================================================================
//  THE ENGINE STILL OWNS A Camera, AND THAT IS NOT A CONTRADICTION.
//
//  The renderer needs one object describing the view. What changed is who
//  decides what goes in it: CameraSystem copies the active CameraComponent's
//  world position and zoom into Engine::GetCamera() once per frame. The
//  component is the AUTHOR; the engine's Camera is the result.
//
//  ==========================================================================
//  A SCENE WITH NO CAMERA ENTITY STILL WORKS, UNCHANGED.
//
//  When no CameraComponent exists, the system does nothing at all and the
//  engine's camera keeps whatever the scene file's "camera" block gave it -
//  exactly the old behaviour. Every scene written before this existed opens
//  and plays identically, which is the only acceptable way to change
//  something this central.
//
//  ==========================================================================
//  SEVERAL CAMERAS: THE HIGHEST PRIORITY WINS.
//
//  Two cameras in a scene is a normal thing to want while building one - a
//  wide shot and a close one, switched between. Rather than refusing it, the
//  one with the highest `priority` is used, and the others sit there inert
//  until their turn. Ties go to whichever was found first, and the Inspector
//  says which camera is actually live so there is never any doubt.
// ============================================================================

// Overlap.h is here for AABB, which WorldBounds returns.
//
// IT WAS MISSING, and this header only ever compiled because every file that
// included it happened to have pulled Overlap.h in first - through Engine.h or
// through the physics headers. The first translation unit to include this one
// on its own failed, naming a line 80 lines below the actual problem. A header
// that compiles only in the company of others is a trap with a delay on it.
#include <engine/math/Overlap.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

namespace eng {

class CameraComponent;

class CameraSystem final : public System {
public:
    // STAGE 700, which this engine already describes as "the camera, after
    // everything it might follow has moved". A camera updated before the
    // things it follows would lag one step behind them forever.
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "CameraSystem"; }
    int         Order() const override { return SystemStage::kCamera; }

    static void Register(CameraComponent& camera);
    static void Unregister(CameraComponent& camera);

    // Pushes the active camera's transform and zoom into the engine's Camera.
    //
    // Public and static because it is called from TWO places: this system,
    // during the fixed step, and once more outside the step loop so that
    // moving the camera in the EDITOR updates the Game view immediately. A
    // paused clock runs no steps, so without that second call the Game view
    // would not move until you pressed Play. The deferred queue is drained
    // the same way and for the same reason - see Engine::Simulate.
    static void Apply();

    // The camera that is actually driving the view, or null when there is
    // none. The Inspector asks so it can say which one is live.
    static CameraComponent* Active();

    static std::size_t Count();
    static void        Clear();
};

class CameraComponent final : public Component {
public:
    static constexpr const char* kTypeName = "CameraComponent";

    ~CameraComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields:
    //   "zoom":     1.0    bigger is closer in
    //   "priority": 0      the highest one wins when there are several
    //   "enabled":  true   a disabled camera is ignored entirely
    //
    // There is deliberately NO position field. The camera is where its
    // entity is - see the note at the top of this header.
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    // Two cameras on ONE entity would be two views from the same point
    // differing only in zoom, which is a confusing way to express something
    // better done with two entities. The Inspector already refuses to add a
    // second component of any type that an entity already has, so there is
    // nothing to declare here.

    float Zoom() const     { return m_zoom; }
    int   Priority() const { return m_priority; }
    bool  Enabled() const  { return m_enabled; }

    void SetZoom(float zoom);
    void SetPriority(int priority) { m_priority = priority; }
    void SetEnabled(bool enabled)  { m_enabled = enabled; }

    // Is this the one currently driving the view?
    bool IsActive() const { return CameraSystem::Active() == this; }

    // The rectangle this camera frames, in world units, for the viewport it
    // would be drawn into. Used by the editor to outline the shot in the
    // Scene view - which is the whole point of making the camera visible.
    AABB WorldBounds(Vec2 viewportPixels) const;

private:
    float m_zoom     = 1.0f;
    int   m_priority = 0;
    bool  m_enabled  = true;
};

} // namespace eng
