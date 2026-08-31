#pragma once

// ============================================================================
//  Camera.h - decides which part of the world ends up on screen.
//
//  A camera has a position (what it is looking at) and a zoom (how close it
//  is). Moving it moves the view; increasing the zoom makes everything bigger.
//
//  HOW IT WORKS: IT IS A TRANSFORM, BACKWARDS
//  The tempting way to write a camera is to subtract its position inside the
//  drawing code. That works until you add zoom, then rotation, then a parent,
//  and each one needs another special case somewhere else.
//
//  Instead, a camera at position p with zoom z is described by the matrix that
//  UNDOES placing an object at p and scaling it by z. Once it is a matrix,
//  Mat3 does all of the work: panning and zooming combine correctly for free,
//  and turning a mouse click back into a world position is just the inverse
//  matrix.
//
//  ==========================================================================
//  THE ONE PLACE THE Y AXIS FLIPS.
//
//  The world is Y-UP: bigger y means further up, which is what makes the maths
//  behave the way it does in a maths lesson and a positive rotation turn
//  anticlockwise. The screen is Y-DOWN, because every 2D graphics API in
//  existence puts pixel (0,0) in the top-left corner.
//
//  ViewMatrix is the only place in the whole engine that reconciles those two.
//  If y ever gets flipped somewhere else as well, the two flips cancel out and
//  everything ends up upside down in a way that looks almost right.
//  ==========================================================================
// ============================================================================

#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>

namespace eng {

class Camera {
public:
    Vec2  Position()     const { return m_position; }
    float Zoom()         const { return m_zoom; }
    Vec2  ViewportSize() const { return m_viewport; }

    void SetPosition(Vec2 position) { m_position = position; }
    void Move(Vec2 delta)           { m_position += delta; }

    // Zoom is kept away from zero. A zoom of exactly 0 would squash the whole
    // world onto a single point, and the matrix describing that cannot be
    // undone - so dragging a zoom slider to the bottom would break the ability
    // to click on anything. Clamping means it just gets very small.
    void SetZoom(float zoom);

    // How large the picture being drawn is, in pixels. The editor sets this
    // from the size of the panel; the standalone game sets it from the window.
    void SetViewportSize(Vec2 sizePixels) { m_viewport = sizePixels; }

    void Reset();

    // World coordinates to screen pixels, and back again.
    Mat3 ViewMatrix() const;
    Mat3 InverseViewMatrix() const;

    Vec2 WorldToScreen(Vec2 world) const;

    // The one that makes clicking work: given a pixel the mouse is over, which
    // point in the world is under it?
    Vec2 ScreenToWorld(Vec2 screen) const;

    // For a DIRECTION rather than a position - the camera's position does not
    // apply, only its zoom and the y flip. Used to turn a size in world units
    // into a size in pixels.
    Vec2 WorldToScreenVector(Vec2 world) const;

    // The rectangle of world the camera can currently see.
    AABB VisibleBounds() const;

private:
    Vec2  m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
    Vec2  m_viewport{1280.0f, 720.0f};
};

} // namespace eng
