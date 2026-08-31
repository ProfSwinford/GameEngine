#pragma once

// =============================================================================
//  WEEK 6 - a movable, zoomable 2D camera.
//
//  BUILT AS AN INVERSE TRANSFORM, not as an offset subtracted inside rendering
//  code. A camera at position p with zoom z is exactly the inverse of the
//  transform that would place an object at p scaled by z. Once you see it that
//  way, Mat3 does all the work, panning and zooming compose correctly for
//  free, and adding camera rotation later is one line instead of an afternoon.
//
//  Special-casing the camera inside the renderer is the reliable way to fail
//  the M1 check that panning and zooming leave a three-deep hierarchy
//  visually correct.
//
//  ---------------------------------------------------------------------------
//  THE ONE PLACE THE Y AXIS FLIPS.
//
//  World space is Y-UP, because that is what the maths in Ch. 5 assumes and
//  what makes a positive rotation counter-clockwise. The screen is Y-DOWN,
//  because that is what every 2D graphics API does. ViewMatrix is the only
//  place in the engine that reconciles them:
//
//      view = Translation(-position) * Scaling(zoom, -zoom) * Translation(half)
//
//  read left to right as: move the camera to the origin, scale (flipping y),
//  then move the origin to the middle of the window.
//
//  If y ever flips anywhere else as well, the two flips cancel and everything
//  is upside down in a way that looks almost right.
// =============================================================================

#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>

namespace eng {

class Camera {
public:
    Vec2 Position() const     { return m_position; }
    f32  Zoom() const         { return m_zoom; }
    Vec2 ViewportSize() const { return m_viewport; }

    void SetPosition(Vec2 position) { m_position = position; }
    void Move(Vec2 delta)           { m_position += delta; }

    // Zoom is clamped away from zero: a zoom of 0 makes the view matrix
    // singular, and Mat3::Inverse would assert. Clamping here means a slider
    // dragged to the bottom produces a very small picture rather than an
    // assert dialog.
    void SetZoom(f32 zoom);

    void SetViewportSize(Vec2 sizePixels) { m_viewport = sizePixels; }

    void Reset();

    // World -> screen.
    Mat3 ViewMatrix() const;
    // Screen -> world. The literal inverse; this is the payoff for building
    // the camera as a transform.
    Mat3 InverseViewMatrix() const;

    Vec2 WorldToScreen(Vec2 world) const;
    Vec2 ScreenToWorld(Vec2 screen) const;

    // Direction vectors, which do NOT pick up the translation. Used by the
    // sprite renderer to convert a world-space size into a screen-space one.
    Vec2 WorldToScreenVector(Vec2 world) const;

    // The world-space AABB the camera can see. Cheap now; in Phase 2 it
    // becomes the culling test.
    AABB VisibleBounds() const;

private:
    Vec2 m_position{0.0f, 0.0f};
    f32  m_zoom = 1.0f;
    Vec2 m_viewport{1280.0f, 720.0f};
};

} // namespace eng
