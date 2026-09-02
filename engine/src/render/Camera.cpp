// =============================================================================
//  Camera.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/render/Camera.h>

namespace eng {

// TODO: a zoom of zero or less would divide by zero later. Clamp it.
void Camera::SetZoom(float zoom) { m_zoom = zoom; }

void Camera::Reset() {
    m_position = Vec2{0.0f, 0.0f};
    m_zoom     = 1.0f;
}

// TODO: the camera is a transform, BACKWARDS - the matrix that undoes placing
// an object at the camera's position and scaling it by the zoom.
//
// THIS IS ALSO THE ONE PLACE THE Y AXIS FLIPS. The world is y-up and the
// screen is y-down; see Camera.h. Flip it anywhere else as well and the two
// cancel out, which looks almost right and is very hard to find.
Mat3 Camera::ViewMatrix() const        { return Mat3::Identity(); }
Mat3 Camera::InverseViewMatrix() const { return Mat3::Identity(); }

Vec2 Camera::WorldToScreen(Vec2 world) const  { return world; }
Vec2 Camera::ScreenToWorld(Vec2 screen) const { return screen; }
Vec2 Camera::WorldToScreenVector(Vec2 world) const { return world; }

// TODO: the rectangle of world currently on screen.
AABB Camera::VisibleBounds() const { return AABB{}; }

} // namespace eng
