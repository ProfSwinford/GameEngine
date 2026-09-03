// =============================================================================
//  Camera.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Camera.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/render/Camera.h>

namespace eng {

// Sets how close the camera is. A zoom of zero or less would divide by zero
// later, so it has to be kept above it.
void Camera::SetZoom(float /*zoom*/) {
}

// Puts the camera back at the origin at normal zoom.
void Camera::Reset() {
}

// The camera as a matrix, BACKWARDS: it undoes placing something at the
// camera's position and scaling it by the zoom.
//
// THIS IS ALSO THE ONE PLACE THE Y AXIS FLIPS. The world is y-up and the screen
// is y-down; flip it anywhere else as well and the two cancel out, which looks
// almost right and is very hard to find.
Mat3 Camera::ViewMatrix() const {
    return Mat3::Identity();
}

// The matrix that undoes the view - what turns a screen position back into a
// world one.
Mat3 Camera::InverseViewMatrix() const {
    return Mat3::Identity();
}

// Where a point in the world appears on screen.
Vec2 Camera::WorldToScreen(Vec2 /*world*/) const {
    return Vec2{};
}

// Where a point on screen is in the world. This is what makes clicking on
// something work.
Vec2 Camera::ScreenToWorld(Vec2 /*screen*/) const {
    return Vec2{};
}

// Converts a world DIRECTION or size into screen units. Unlike a point, it
// ignores where the camera is and only takes the zoom and the y flip.
Vec2 Camera::WorldToScreenVector(Vec2 /*world*/) const {
    return Vec2{};
}

// The rectangle of world currently on screen - useful for skipping anything
// outside it.
AABB Camera::VisibleBounds() const {
    return AABB{};
}

} // namespace eng
