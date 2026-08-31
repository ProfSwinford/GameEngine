#pragma once
// =============================================================================
//  THE SCENE VIEW - the editing viewport.
//
//  Everything before this rendered the world straight to the window and drew
//  the IDE on top of it, which gives you exactly ONE view and no way to look at
//  the world from somewhere other than where the game camera is standing. This
//  panel renders the world into its OWN RenderTarget through its OWN camera, so
//  it can be framed independently, and displays the result with ImGui::Image.
//
//  ---------------------------------------------------------------------------
//  THE SCENE CAMERA IS NOT THE GAME CAMERA, and that separation is the point.
//
//  Engine::GetCamera() is the game's - it is what the Game view renders through
//  and what a scene file's `camera` block sets. This panel owns a second one
//  that the game never touches, so panning around to look at something does not
//  move the player's view, and pressing Play does not snap the editor camera
//  somewhere else.
//
//  ---------------------------------------------------------------------------
//  CONTROLS, chosen to match what the muscle memory already expects:
//
//    middle-drag  or  right-drag   pan
//    wheel                          zoom, TOWARD THE CURSOR
//    left-click                     select the entity under the cursor
//    left-drag a gizmo handle       move the selection
//    F                              frame the selection
//
//  Zoom-toward-cursor is worth the six lines: zooming to the centre means
//  every zoom is followed by a pan to put back what you were looking at.
//
//  ---------------------------------------------------------------------------
//  THE GIZMO IS DRAWN WITH IMGUI, NOT WITH DEBUGDRAW, and that is deliberate.
//  DebugDraw is world space and goes into the render target; a manipulator has
//  to be a fixed size on screen no matter the zoom, and has to hit-test in the
//  same coordinates the mouse arrives in. Drawing it into the panel's ImGui
//  draw list over the image gives both for free. It is the one place the
//  Week 6 rule about keeping the two apart argues FOR ImGui rather than
//  against it.
// =============================================================================
#include "Panel.h"

#include <engine/Engine.h>

#include <imgui.h>   // ImVec2 - this panel thinks in screen space, so it says so

namespace editor {

class ScenePanel final : public Panel {
public:
    const char* Title() const override { return "Scene"; }
    void        Draw() override;

    // Not visible, so the mouse is not over it and no gizmo handle is being
    // held - a drag left half-finished when the tab changed would otherwise
    // resume on whatever the cursor happened to be over when it came back.
    void OnHidden() override {
        m_hovered  = false;
        m_dragAxis = GizmoAxis::None;
    }

    // Called by EditorApp after every panel has been drawn, once the target is
    // sized. Rendering during Draw() would be rendering in the middle of ImGui
    // recording its command list.
    void RenderView();

    eng::Camera& Camera() { return m_camera; }

private:
    // Which handle the mouse grabbed, if any.
    enum class GizmoAxis { None, X, Y, Both };

    void  UpdateGizmo(const ImVec2& imageOrigin);   // input  - runs BEFORE picking
    void  DrawGizmo(const ImVec2& imageOrigin);     // output - runs after
    void  HandlePicking(const ImVec2& imageOrigin, const ImVec2& imageSize);
    void  FrameSelection();
    static eng::AABB EntityWorldBounds(eng::Entity& entity);

    eng::Camera       m_camera;
    eng::RenderTarget m_target;

    // Where the image was placed and how big it is, recorded during Draw and
    // used by RenderView and by the mouse maths. Screen coordinates inside the
    // panel are (mouse - m_imageOrigin), which is the space the camera thinks
    // in once its viewport is the target's size.
    ImVec2 m_imageOrigin{0.0f, 0.0f};
    ImVec2 m_imageSize{0.0f, 0.0f};
    bool   m_hovered = false;

    // Last drop result, shown over the image. A drop that quietly did nothing
    // is indistinguishable from a broken drag.
    char   m_status[256] = {};

    GizmoAxis m_dragAxis  = GizmoAxis::None;
    eng::Vec2 m_dragGrabOffset{};
};

} // namespace editor
