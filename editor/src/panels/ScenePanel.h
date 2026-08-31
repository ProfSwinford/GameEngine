#pragma once

// ============================================================================
//  ScenePanel.h - the Scene view: the window you edit in.
//
//  It draws the world into its OWN off-screen picture through its OWN camera,
//  then displays that picture inside the panel. Having a separate picture is
//  what makes it possible to have a Scene view and a Game view on screen at
//  the same time - drawing straight to the window can only ever produce one.
//
//  ==========================================================================
//  THE SCENE CAMERA IS NOT THE GAME CAMERA, and that separation is the point.
//
//  Engine::GetCamera() is the game's camera - what the Game view shows and
//  what a scene file's "camera" block sets. This panel owns a second one that
//  the game never touches, so panning around to look at something does not
//  move the player's view, and pressing Play does not yank the editor camera
//  somewhere else.
//
//  ==========================================================================
//  CONTROLS, chosen to match what your hands already expect:
//
//    middle-drag or right-drag   pan
//    mouse wheel                 zoom, TOWARDS THE CURSOR
//    left-click                  select whatever is under the cursor
//    drag a coloured handle      move the selection
//    F                           frame the selection
//
//  Zooming towards the cursor is worth the six lines it takes. Zooming towards
//  the middle means every zoom has to be followed by a pan to put back what
//  you were looking at.
//
//  ==========================================================================
//  THE MOVE HANDLES ARE DRAWN WITH IMGUI, NOT AS GIZMOS, and that is
//  deliberate. Gizmos live in the world, so they would grow and shrink with
//  the zoom - and a handle that becomes two pixels wide when you zoom out is
//  unusable exactly when you need it. Drawing them in screen space over the
//  image keeps them a fixed size and lets them be hit-tested in the same
//  coordinates the mouse arrives in.
// ============================================================================

#include "Panel.h"

#include <engine/Engine.h>

#include <imgui.h>   // ImVec2 - this panel thinks in screen pixels, so it says so

namespace editor {

class ScenePanel final : public Panel {
public:
    const char* Title() const override { return "Scene"; }
    void        Draw() override;

    // Not visible, so the mouse is not over it and no handle is being held. A
    // drag left half-finished when the tab changed would otherwise carry on
    // when the panel came back.
    void OnHidden() override {
        m_hovered  = false;
        m_dragAxis = GizmoAxis::None;
    }

    // Called by EditorApp after every panel has drawn. Drawing the world
    // inside Draw() would mean drawing in the middle of ImGui building its
    // list of things to draw.
    void RenderView();

    eng::Camera& Camera() { return m_camera; }

private:
    // Which move handle the mouse grabbed, if any.
    enum class GizmoAxis { None, X, Y, Both };

    void UpdateGizmo(const ImVec2& imageOrigin);   // input  - runs BEFORE picking
    void DrawGizmo(const ImVec2& imageOrigin);     // output - runs after
    void HandlePicking(const ImVec2& imageOrigin);
    void DrawOverlay();
    void FrameSelection();

    // The box used for clicking on an entity and for framing it.
    static eng::AABB EntityWorldBounds(eng::Entity& entity);

    eng::Camera       m_camera;
    eng::RenderTarget m_target;

    // Where the picture was placed on screen and how big it is, recorded while
    // drawing and used by all the mouse arithmetic. A position inside the view
    // is (mouse - m_imageOrigin), which is the space the camera works in once
    // its viewport is the picture's size.
    ImVec2 m_imageOrigin{0.0f, 0.0f};
    ImVec2 m_imageSize{0.0f, 0.0f};
    bool   m_hovered = false;

    // What the last drag-and-drop did, shown over the image. A drop that
    // quietly did nothing is indistinguishable from a broken drag.
    char m_status[256] = {};

    GizmoAxis m_dragAxis = GizmoAxis::None;
    eng::Vec2 m_dragGrabOffset{};
};

} // namespace editor
