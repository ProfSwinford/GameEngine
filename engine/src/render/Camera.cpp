// ============================================================================
//  Camera.cpp - the camera declared in Camera.h.
//
//  The whole class is three or four lines of real work, because building the
//  camera as a matrix hands the rest of the job to Mat3.
// ============================================================================

#include <engine/render/Camera.h>

#include <algorithm>

namespace eng {

void Camera::SetZoom(float zoom) {
    // std::clamp keeps the value inside a range. The lower bound stops the
    // view matrix from becoming impossible to invert; the upper bound stops a
    // stray scroll wheel from zooming so far that positions lose precision.
    m_zoom = std::clamp(zoom, 0.01f, 1000.0f);

    // THE MODE FOLLOWS THE LAST THING ASKED FOR. See the note on the
    // declaration: a camera framed by height whose zoom was set would have
    // that zoom thrown away by the next resize, silently.
    m_framing = Framing::Zoom;
}

void Camera::SetFrameHeight(float worldUnits) {
    // The same floor and ceiling, expressed in the other unit. A frame height
    // of zero is a zoom of infinity, which is the unusable matrix SetZoom's
    // lower bound exists to prevent.
    m_frameHeight = std::clamp(worldUnits, 0.01f, 1000000.0f);
    m_framing     = Framing::Height;
    RederiveZoom();
}

float Camera::FrameHeight() const {
    if (m_framing == Framing::Height) {
        return m_frameHeight;
    }

    // DERIVED THE OTHER WAY, so this answers in both modes and nothing has to
    // ask which one a camera is in before it can report what is in shot.
    return m_zoom > 0.0f ? m_viewport.y / m_zoom : 0.0f;
}

void Camera::SetViewportSize(Vec2 sizePixels) {
    m_viewport = sizePixels;
    RederiveZoom();
}

void Camera::RederiveZoom() {
    // ======================================================================
    //  NOTHING AT ALL IN Framing::Zoom, and that is the backwards
    //  compatibility story in one line: every camera in every scene already
    //  written is in that mode, so this function is a branch they never take
    //  and the arithmetic they always had is untouched.
    // ======================================================================
    if (m_framing != Framing::Height) {
        return;
    }
    if (m_frameHeight <= 0.0f || m_viewport.y <= 0.0f) {
        return;   // nothing sensible to derive from; the last zoom stands
    }

    // The one relationship, stated once: zoom = viewport.y / frameHeight.
    // Through SetZoom rather than assigned, so the clamp applies - a one-pixel
    // viewport with a large frame height would otherwise produce a zoom the
    // view matrix cannot be inverted from.
    //
    // AND THE MODE IS PUT BACK, because SetZoom switches it. Deriving a zoom
    // is not somebody choosing one.
    const float wanted = m_viewport.y / m_frameHeight;
    SetZoom(wanted);
    m_framing = Framing::Height;
}

void Camera::Reset() {
    m_position = Vec2{0.0f, 0.0f};
    m_zoom     = 1.0f;

    // BACK TO THE DEFAULT MODE TOO. Reset means "as a new camera is", and a
    // new camera is framed by zoom - otherwise pressing Reset on a
    // height-framed camera would leave it in a mode whose number had just
    // been thrown away.
    m_framing     = Framing::Zoom;
    m_frameHeight = 720.0f;
}

Mat3 Camera::ViewMatrix() const {
    const Vec2 half{m_viewport.x * 0.5f, m_viewport.y * 0.5f};

    // Read left to right, this says exactly what it does:
    //   1. slide the world so the camera's position sits at the origin
    //   2. scale by the zoom, and FLIP Y (that is the -m_zoom)
    //   3. slide the origin to the middle of the picture
    //
    // The negative y is the single y flip in the engine. See Camera.h.
    return Mat3::Translation(-m_position) *
           Mat3::Scaling(Vec2{m_zoom, -m_zoom}) *
           Mat3::Translation(half);
}

Mat3 Camera::InverseViewMatrix() const {
    return ViewMatrix().Inverse();
}

Vec2 Camera::WorldToScreen(Vec2 world) const {
    return ViewMatrix().TransformPoint(world);
}

Vec2 Camera::ScreenToWorld(Vec2 screen) const {
    // This is what makes clicking on things work. Because the camera is a
    // matrix, "undo the camera" is literally the inverse matrix - there is no
    // second piece of code that has to be kept in step with ViewMatrix.
    return InverseViewMatrix().TransformPoint(screen);
}

Vec2 Camera::WorldToScreenVector(Vec2 world) const {
    // TransformVector rather than TransformPoint: a size or a direction should
    // not be shifted by where the camera happens to be looking.
    return ViewMatrix().TransformVector(world);
}

AABB Camera::VisibleBounds() const {
    // Push all four corners of the screen back out into the world and take the
    // box around them.
    //
    // Doing it this way rather than "viewport divided by zoom" costs nothing
    // and keeps working unchanged on the day the camera learns to rotate.
    const Mat3 inverse = InverseViewMatrix();

    const Vec2 corners[4] = {
        inverse.TransformPoint(Vec2{0.0f, 0.0f}),
        inverse.TransformPoint(Vec2{m_viewport.x, 0.0f}),
        inverse.TransformPoint(Vec2{0.0f, m_viewport.y}),
        inverse.TransformPoint(Vec2{m_viewport.x, m_viewport.y}),
    };

    AABB bounds{corners[0], corners[0]};
    for (int i = 1; i < 4; ++i) {
        bounds.Encapsulate(corners[i]);
    }
    return bounds;
}

} // namespace eng
