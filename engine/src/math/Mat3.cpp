// WEEK 6 - Mat3. Row-major storage, row vectors (v * M). See Mat3.h for the
// convention block; this file implements exactly what is written there.

#include <engine/core/Assert.h>
#include <engine/math/Mat3.h>

#include <cmath>

namespace eng {

Mat3 Mat3::Identity() {
    Mat3 result;
    result.m[0][0] = 1.0f; result.m[0][1] = 0.0f; result.m[0][2] = 0.0f;
    result.m[1][0] = 0.0f; result.m[1][1] = 1.0f; result.m[1][2] = 0.0f;
    result.m[2][0] = 0.0f; result.m[2][1] = 0.0f; result.m[2][2] = 1.0f;
    return result;
}

Mat3 Mat3::Translation(Vec2 t) {
    Mat3 result = Identity();
    // Bottom row, because vectors are rows. See the convention block.
    result.m[2][0] = t.x;
    result.m[2][1] = t.y;
    return result;
}

Mat3 Mat3::Rotation(f32 radians) {
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);

    Mat3 result = Identity();
    // [ c  s  0 ]   With v as a row, (1,0) * R = (c, s), so a positive angle
    // [-s  c  0 ]   rotates counter-clockwise in a y-up space. That is the
    // [ 0  0  1 ]   behaviour the transform test pins down.
    result.m[0][0] =  c; result.m[0][1] = s;
    result.m[1][0] = -s; result.m[1][1] = c;
    return result;
}

Mat3 Mat3::Scaling(Vec2 s) {
    Mat3 result = Identity();
    result.m[0][0] = s.x;
    result.m[1][1] = s.y;
    return result;
}

Mat3 Mat3::FromTRS(Vec2 translation, f32 radians, Vec2 scale) {
    // Equivalent to Scaling(scale) * Rotation(radians) * Translation(translation)
    // under this engine's convention: scale it, rotate it, then move it.
    // Written out because this is called once per node per WorldMatrix() call,
    // and the naive version builds and multiplies three matrices to fill in
    // six numbers.
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);

    Mat3 result;
    result.m[0][0] = scale.x *  c; result.m[0][1] = scale.x * s; result.m[0][2] = 0.0f;
    result.m[1][0] = scale.y * -s; result.m[1][1] = scale.y * c; result.m[1][2] = 0.0f;
    result.m[2][0] = translation.x; result.m[2][1] = translation.y; result.m[2][2] = 1.0f;
    return result;
}

Vec2 Mat3::TransformPoint(Vec2 point) const {
    // [x y 1] * M, taking the first two components. The `1` is what picks up
    // the bottom row, which is why translation applies here.
    return Vec2{point.x * m[0][0] + point.y * m[1][0] + m[2][0],
                point.x * m[0][1] + point.y * m[1][1] + m[2][1]};
}

Vec2 Mat3::TransformVector(Vec2 direction) const {
    // [x y 0] * M. The 0 in the homogeneous slot is the entire difference:
    // the bottom row - the translation - is multiplied away.
    return Vec2{direction.x * m[0][0] + direction.y * m[1][0],
                direction.x * m[0][1] + direction.y * m[1][1]};
}

Mat3 Mat3::Inverse() const {
    // The assumed affine shape, asserted rather than trusted.
    ENGINE_ASSERT_MSG(ApproxEqual(m[0][2], 0.0f) && ApproxEqual(m[1][2], 0.0f) &&
                          ApproxEqual(m[2][2], 1.0f),
                      "Mat3::Inverse assumes an affine matrix (third column 0,0,1)");

    const f32 a = m[0][0];
    const f32 b = m[0][1];
    const f32 c = m[1][0];
    const f32 d = m[1][1];

    const f32 determinant = a * d - b * c;
    if (std::fabs(determinant) < 1e-12f) {
        ENGINE_ASSERT_MSG(false, "Mat3::Inverse on a singular matrix (zero scale?)");
        return Identity();
    }

    const f32 invDet = 1.0f / determinant;

    Mat3 result = Identity();
    result.m[0][0] =  d * invDet;
    result.m[0][1] = -b * invDet;
    result.m[1][0] = -c * invDet;
    result.m[1][1] =  a * invDet;

    // M = [ A 0 ; t 1 ]  ->  M^-1 = [ A^-1 0 ; -t*A^-1 1 ].
    const f32 tx = m[2][0];
    const f32 ty = m[2][1];
    result.m[2][0] = -(tx * result.m[0][0] + ty * result.m[1][0]);
    result.m[2][1] = -(tx * result.m[0][1] + ty * result.m[1][1]);

    return result;
}

Vec2 Mat3::GetTranslation() const {
    return Vec2{m[2][0], m[2][1]};
}

Vec2 Mat3::GetScale() const {
    // The length of each basis row. Loses the sign of a mirroring scale, which
    // is the standard limitation of decomposing a matrix and is fine for
    // everything the engine does with it (sprite size, Inspector display).
    const Vec2 rowX{m[0][0], m[0][1]};
    const Vec2 rowY{m[1][0], m[1][1]};
    return Vec2{rowX.Length(), rowY.Length()};
}

f32 Mat3::GetRotation() const {
    // Row 0 is (sx*cos, sx*sin), so atan2 recovers the angle regardless of a
    // positive uniform scale.
    return std::atan2(m[0][1], m[0][0]);
}

Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 result;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = a.m[row][0] * b.m[0][col] +
                                 a.m[row][1] * b.m[1][col] +
                                 a.m[row][2] * b.m[2][col];
        }
    }
    return result;
}

bool ApproxEqual(const Mat3& a, const Mat3& b, f32 epsilon) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (!ApproxEqual(a.m[row][col], b.m[row][col], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace eng
