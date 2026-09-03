// =============================================================================
//  Mat3.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Mat3.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/math/Mat3.h>

namespace eng {

// The "do nothing" matrix. Every other matrix here is built by changing a copy
// of this one, so it is worth writing first.
Mat3 Mat3::Identity() {
    return Mat3{};
}

// A matrix that moves a point by t. The move part lives in the BOTTOM ROW,
// because points are written as rows in this engine.
Mat3 Mat3::Translation(Vec2 /*t*/) {
    return Mat3{};
}

// A matrix that turns a point anticlockwise about the origin.
Mat3 Mat3::Rotation(float /*radians*/) {
    return Mat3{};
}

// A matrix that resizes about the origin.
Mat3 Mat3::Scaling(Vec2 /*s*/) {
    return Mat3{};
}

// Scale, then rotate, then move - combined into one matrix. Every object's
// world position goes through this every frame.
Mat3 Mat3::FromTRS(Vec2 /*translation*/, float /*radians*/, Vec2 /*scale*/) {
    return Mat3{};
}

// Transforms a POSITION: the move part applies, so a point slides with the space.
Vec2 Mat3::TransformPoint(Vec2 /*point*/) const {
    return Vec2{};
}

// Transforms a DIRECTION: the move part does NOT apply, because sliding the
// world sideways does not change which way something is facing.
Vec2 Mat3::TransformVector(Vec2 /*direction*/) const {
    return Vec2{};
}

// The matrix that undoes this one. Turning a mouse click back into a world
// position is this, and nothing else.
Mat3 Mat3::Inverse() const {
    return Mat3{};
}

// Pulls the move part back out of a finished matrix.
Vec2 Mat3::GetTranslation() const {
    return Vec2{};
}

// Pulls the resize out of a finished matrix.
Vec2 Mat3::GetScale() const {
    return Vec2{};
}

// Pulls the turn out of a finished matrix, in radians.
float Mat3::GetRotation() const {
    return 0.0f;
}

// Combines two transforms. "Do a, then b" is written a * b, which is what the
// row-vector convention buys.
Mat3 operator*(const Mat3& /*a*/, const Mat3& /*b*/) {
    return Mat3{};
}

// Compares two matrices allowing for the small errors decimal arithmetic makes,
// because two matrices that should be equal rarely are exactly.
bool ApproxEqual(const Mat3& /*a*/, const Mat3& /*b*/, float /*epsilon*/) {
    return false;
}

} // namespace eng
