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
}

void Camera::Reset() {
    m_position = Vec2{0.0f, 0.0f};
    m_zoom     = 1.0f;
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
