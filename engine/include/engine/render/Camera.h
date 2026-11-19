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
    // ======================================================================
    //  TWO WAYS TO SAY HOW MUCH WORLD IS IN SHOT, and the second one exists
    //  because the first does not survive a change of resolution.
    //
    //  BY ZOOM - the original, and still the default. The zoom is how many
    //    screen pixels one world unit covers, and the viewport decides how
    //    much world fits. So a bigger window shows MORE WORLD at the same
    //    zoom. That is right for the Scene view, where the panel is a window
    //    onto a level and dragging it wider should reveal more of it.
    //
    //    It is wrong for a GAME. A player at 2560x1440 saw further than one
    //    at 1280x720 - further up, down and sideways - so every judgement
    //    about what is just off-screen was being made against one developer's
    //    monitor. Pinning the aspect ratio fixed the SHAPE of that mistake
    //    without fixing the mistake.
    //
    //  BY FRAME HEIGHT - a number of world units visible from the top of the
    //    picture to the bottom, whatever the picture's pixel size. The zoom
    //    is then DERIVED from the viewport: a taller viewport draws the same
    //    world bigger rather than showing more of it. This is what makes
    //    "what I framed in the Game view is what ships" true.
    //
    //    The width still follows the aspect ratio, exactly as Unity's
    //    orthographic camera does. A 16:9 and a 21:9 player see the same
    //    vertical slice and the wider one sees further sideways, which is the
    //    conventional bargain and the one every ultrawide player expects.
    //
    //  ==========================================================================
    //  Zoom() AND FrameHeight() BOTH ALWAYS WORK, whichever mode is set - each
    //  is derived from the other through the viewport. That is deliberate:
    //  every existing caller of Zoom() keeps compiling and keeps getting the
    //  number it always got, and nothing has to ask which mode a camera is in
    //  before it can draw.
    //
    //  THE RELATIONSHIP, once and for all:  zoom = viewport.y / frameHeight
    //
    //  Unity spells its half of this `orthographicSize`, which is HALF a frame
    //  height. The full height is used here because this engine's world is in
    //  pixels and "720 units tall" is the number somebody actually means;
    //  halving it to 360 would be a unit nobody asked for.
    // ==========================================================================
    enum class Framing {
        Zoom,     // the zoom is pinned; the viewport decides how much world
        Height,   // the world height is pinned; the viewport decides the zoom
    };

    Vec2  Position()     const { return m_position; }
    float Zoom()         const { return m_zoom; }
    Vec2  ViewportSize() const { return m_viewport; }

    Framing CurrentFraming() const { return m_framing; }

    // How many world units are visible vertically. Derived from the zoom when
    // the framing is by zoom, which is why it answers in both modes.
    float FrameHeight() const;

    void SetPosition(Vec2 position) { m_position = position; }
    void Move(Vec2 delta)           { m_position += delta; }

    // Zoom is kept away from zero. A zoom of exactly 0 would squash the whole
    // world onto a single point, and the matrix describing that cannot be
    // undone - so dragging a zoom slider to the bottom would break the ability
    // to click on anything. Clamping means it just gets very small.
    //
    // SWITCHES THE FRAMING TO Zoom, because setting a zoom by hand is saying
    // which of the two numbers you want held still. A camera framed by height
    // whose zoom was then set would have that zoom thrown away by the next
    // resize, silently - so the mode follows the last thing asked for.
    void SetZoom(float zoom);

    // Pins the world height instead, and switches the framing to Height.
    // Clamped away from zero for the same reason SetZoom is.
    void SetFrameHeight(float worldUnits);

    // How large the picture being drawn is, in pixels. The editor sets this
    // from the size of the panel; the standalone game sets it from the window.
    //
    // NOT INLINE ANY MORE: when the framing is by height this is what
    // re-derives the zoom, so it is the single point at which a resize turns
    // into a scale. Every host already calls it every frame - see
    // Engine::RenderWorld - so a camera framed by height needs nothing else
    // wired up.
    void SetViewportSize(Vec2 sizePixels);

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
    // Keeps m_zoom in step with m_frameHeight and the viewport. Does nothing
    // at all when the framing is by zoom, which is what makes the old
    // behaviour bit-for-bit unchanged.
    void RederiveZoom();

    Vec2  m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
    Vec2  m_viewport{1280.0f, 720.0f};

    // ZOOM BY DEFAULT, so a camera nobody has said anything to behaves
    // exactly as it did before this existed - which is the whole of the
    // backwards compatibility story for every scene already written.
    Framing m_framing     = Framing::Zoom;
    float   m_frameHeight = 720.0f;   // only consulted in Framing::Height
};

} // namespace eng
