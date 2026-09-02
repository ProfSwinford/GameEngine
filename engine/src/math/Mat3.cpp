// =============================================================================
//  Mat3.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/math/Mat3.h>

namespace eng {

// Given for free, because every other matrix here is built by changing a copy
// of this one - and a stub that returned a matrix full of zeros would collapse
// every position in the world onto the origin.
Mat3 Mat3::Identity() {
    Mat3 result;
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    return result;
}

// TODO: a matrix that moves a point by t. See Mat3.h - the move part lives in
// the BOTTOM ROW, because points are written as rows in this engine.
Mat3 Mat3::Translation(Vec2 /*t*/) { return Identity(); }

// TODO: a matrix that turns a point anticlockwise about the origin.
Mat3 Mat3::Rotation(float /*radians*/) { return Identity(); }

// TODO: a matrix that resizes about the origin.
Mat3 Mat3::Scaling(Vec2 /*s*/) { return Identity(); }

// TODO: scale, then rotate, then move - in one matrix.
Mat3 Mat3::FromTRS(Vec2 /*translation*/, float /*radians*/, Vec2 /*scale*/) {
    return Identity();
}

// TODO: transform a POSITION (the move part applies).
Vec2 Mat3::TransformPoint(Vec2 point) const { return point; }

// TODO: transform a DIRECTION (the move part does NOT apply).
Vec2 Mat3::TransformVector(Vec2 direction) const { return direction; }

// TODO: the matrix that undoes this one.
Mat3 Mat3::Inverse() const { return Identity(); }

// TODO: pull the move / resize / turn back out of a finished matrix.
Vec2  Mat3::GetTranslation() const { return Vec2{0.0f, 0.0f}; }
Vec2  Mat3::GetScale() const       { return Vec2{1.0f, 1.0f}; }
float Mat3::GetRotation() const    { return 0.0f; }

// TODO: combine two transforms. "Do a, then b" is written a * b here.
Mat3 operator*(const Mat3& /*a*/, const Mat3& /*b*/) { return Mat3::Identity(); }

// TODO: compare two matrices allowing for floating-point drift.
bool ApproxEqual(const Mat3& /*a*/, const Mat3& /*b*/, float /*epsilon*/) {
    return false;
}

} // namespace eng
