// WEEK 6 - the camera as an inverse transform. See Camera.h.

#include <engine/debug/Camera.h>

#include <algorithm>

namespace eng {

void Camera::SetZoom(f32 zoom) {
    // Away from zero in both directions. A zoom of exactly 0 makes the view
    // matrix singular; a negative zoom mirrors the world, which is not a thing
    // anyone asks for on purpose.
    m_zoom = std::clamp(zoom, 0.01f, 1000.0f);
}

void Camera::Reset() {
    m_position = Vec2{0.0f, 0.0f};
    m_zoom     = 1.0f;
}

Mat3 Camera::ViewMatrix() const {
    const Vec2 half{m_viewport.x * 0.5f, m_viewport.y * 0.5f};

    // Row-vector convention: A * B means "apply A, then B". Reading left to
    // right, this is exactly the sentence in the header.
    //
    // The -zoom on y is THE y flip, and it is the only one in the engine.
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
    // Not optional and not symmetry for its own sake: the viewport panel's
    // world-space mouse readout is this function, and entity picking in
    // Week 8 onwards depends on it. The round trip is tested at four zoom
    // levels and several camera positions.
    return InverseViewMatrix().TransformPoint(screen);
}

Vec2 Camera::WorldToScreenVector(Vec2 world) const {
    return ViewMatrix().TransformVector(world);
}

AABB Camera::VisibleBounds() const {
    // The four screen corners pushed back into world space. Doing it by
    // transforming corners rather than by dividing the viewport by the zoom
    // means this keeps working the day the camera acquires rotation.
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
