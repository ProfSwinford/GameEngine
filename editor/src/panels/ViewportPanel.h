#pragma once
// =============================================================================
//  WEEK 6 PANELS - viewport controls and debug-draw toggles. Two small panels;
//  together they are where the IDE starts feeling like Unity rather than like
//  a debug overlay.
//
//  VIEWPORT PANEL:
//    - camera position and zoom as DRAGGABLE fields, so they can be scrubbed
//      while watching the scene move
//    - a Reset View button
//    - a live readout of the mouse position in WORLD space
//
//  That last one is the useful one, and it is a direct test of ScreenToWorld:
//  move the mouse, and if the numbers do not match where the cursor visually
//  is, the inverse transform is wrong. Finding that here beats finding it in
//  Week 9 when picking depends on it.
//
//  DEBUG DRAW PANEL:
//    - a master on/off
//    - per-category toggles (grid, axes, bounds, colliders)
//    - the circle segment count as a slider, so tessellation can be watched
//      changing
//
//  ---------------------------------------------------------------------------
//  DEBUGDRAW IS NOT INSIDE THIS PANEL, AND IMGUI IS NOT INSIDE DEBUGDRAW.
//  They solve different problems:
//    DebugDraw - WORLD space, lifetimed, callable from anywhere in engine code
//    ImGui     - SCREEN space, this frame only, called from the editor
//  ImGui cannot put a three-second marker at a world position; DebugDraw
//  cannot do a dockable table with a text filter. Both are needed and they
//  never overlap.
// =============================================================================
#include "Panel.h"

namespace editor {

class ViewportPanel final : public Panel {
public:
    const char* Title() const override { return "Viewport"; }
    void        Draw() override;
};

class DebugDrawPanel final : public Panel {
public:
    const char* Title() const override { return "Debug Draw"; }
    void        Draw() override;
};

} // namespace editor
