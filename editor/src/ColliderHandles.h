#pragma once

// ============================================================================
//  ColliderHandles.h - reshaping a collider by dragging it in the Scene view.
//
//  WHY THIS EXISTS. A polygon or an edge collider is a list of numbers, and
//  typing a list of numbers at a shape you are looking at is the slowest
//  possible way to make it match. The Inspector can show the points and will
//  keep doing so - it is the only place to add or remove one - but putting
//  them where they belong is a job for the mouse.
//
//  EVERY SHAPE GETS HANDLES, not just those two. A box fitted to a sprite by
//  dragging its sides is the same win in miniature, and a feature that worked
//  on two of five colliders would be a thing to remember rather than a thing
//  to use.
//
//  ==========================================================================
//  IT IS OFF UNTIL YOU ASK FOR IT, and that is deliberate rather than timid.
//
//  The obvious design turns handles on whenever a collider is selected. It is
//  also the design that makes the Scene view unpredictable: handles sit on
//  top of the move gizmo and on top of the entity itself, so every click near
//  a selected object becomes a question of which thing you meant. The Tile
//  Palette made exactly this argument about painting - see TilePaint.h - and
//  the answer is the same one: a mode you turn on is a mode you know you are
//  in.
//
//  The switch is a button on the collider in the Inspector, so it reads as a
//  property of that collider rather than as a global tool.
//
//  ==========================================================================
//  THE STATE IS THE EDITOR'S, NOT THE SCENE'S.
//
//  Which collider you are currently reshaping is not a fact about the game;
//  it is a fact about what you are doing this minute. Serializing it would
//  write editor state into a file a shipped game reads, and it would turn a
//  mode somebody left on into a permanent property of the level. Unity's
//  "Edit Collider" is editor-only for the same reason.
//
//  SO IT IS ADDRESSED BY ENTITY AND INDEX, not by pointer. A scene reload or
//  an undo rebuilds every component in the scene, and a stored
//  ColliderComponent* would survive both as a dangling pointer that still
//  looks valid.
// ============================================================================

#include <engine/scene/EntityId.h>

#include <imgui.h>

#include <cstddef>

namespace eng {
class Camera;
class ColliderComponent;
} // namespace eng

namespace editor {

class ColliderEdit {
public:
    static ColliderEdit& Get();

    // Is this the collider whose handles are showing?
    bool IsEditing(const eng::ColliderComponent& collider) const;

    // Turns handles on for this collider, or off if they were already on it.
    // Only ever one at a time: two sets of handles over one entity would be
    // the ambiguity this whole file is arranged to avoid.
    void Toggle(const eng::ColliderComponent& collider);

    void Stop();

    // The collider being reshaped, looked up in the live scene - so a reload,
    // an undo or a deletion simply gives back null rather than a stale
    // pointer. Null when nothing is being edited.
    eng::ColliderComponent* Target() const;

private:
    eng::EntityId m_entity{};
    std::size_t   m_index = 0;
    bool          m_active = false;
};

// ---------------------------------------------------------------------------
//  INPUT, which must run BEFORE picking.
//
//  Picking's guard reads "is a handle being held", and a guard against
//  something produced LATER in the same frame is not a guard at all - see the
//  note on frame order in ScenePanel.cpp, which records what that bug looked
//  like for the move gizmo.
//
//  Returns true when the handles have taken the mouse, in which case picking
//  and the move gizmo stand down. Without that, grabbing a handle would also
//  run a pick underneath it and change the selection out from under the drag.
// ---------------------------------------------------------------------------
bool UpdateColliderHandles(const eng::Camera& camera, const ImVec2& imageOrigin);

// Output. Runs after picking, like the move gizmo's drawing does.
void DrawColliderHandles(const eng::Camera& camera, const ImVec2& imageOrigin);

} // namespace editor
