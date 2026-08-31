#pragma once

// =============================================================================
//  WEEK 6 - Vec2. Ch. 5.1-5.2.
//
//  This replaced the placeholder in Week 2's MemUtil.h. Everything in the
//  engine that has a position uses this type.
//
//  Header-only, and deliberately: every function here is two or three
//  arithmetic operations, and an out-of-line call would cost more than the
//  work. This is the one place in the engine where "put it in the header" is
//  the performance answer rather than the lazy one.
//
//  OPERATOR OVERLOADING - which are members and which are free:
//
//    Members:  +=, -=, *=, /=          - they mutate the left operand, and the
//                                        left operand is always a Vec2.
//    Free:     +, -, unary -, *, /     - because `2.0f * v` needs a left
//                                        operand of type f32, and a member
//                                        function's left operand is always its
//                                        own class. That single case decides
//                                        the whole family: for symmetry, the
//                                        binary arithmetic operators are all
//                                        free functions.
//    Free:     ==, !=                  - defaulted via C++20's ==, though see
//                                        ApproxEqual below for the version you
//                                        should almost always be using.
// =============================================================================

#include <engine/core/Types.h>

#include <cmath>

namespace eng {

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 inX, f32 inY) : x(inX), y(inY) {}

    // --- compound assignment: members -------------------------------------
    constexpr Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    constexpr Vec2& operator*=(f32 scalar)      { x *= scalar; y *= scalar; return *this; }
    constexpr Vec2& operator/=(f32 scalar)      { x /= scalar; y /= scalar; return *this; }

    // Exact equality. Present because the compiler can generate it and because
    // a few places genuinely want bitwise-identical (has this been written to
    // at all). Use ApproxEqual for anything that came out of arithmetic.
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;

    // LengthSquared exists so that distance COMPARISONS never pay for a square
    // root. `a.LengthSquared() < b.LengthSquared()` orders identically to
    // comparing the lengths, because sqrt is monotonic on non-negative inputs.
    // If only Length existed, every comparison in the engine would carry a
    // sqrt it did not need.
    constexpr f32 LengthSquared() const { return x * x + y * y; }
    f32           Length() const        { return std::sqrt(LengthSquared()); }

    // ZERO-LENGTH NORMALIZE, decided and tested:
    //   Normalized() on a vector shorter than kNormalizeEpsilon returns the
    //   ZERO VECTOR, not NaN and not infinity.
    //
    // Dividing by zero in floating point does not crash. It produces inf or
    // NaN, which then propagates silently through every subsequent
    // calculation and surfaces as an entity that has vanished from the screen
    // with no error anywhere. Zero is wrong too - but it is wrong LOUDLY and
    // locally, and it does not poison anything downstream.
    static constexpr f32 kNormalizeEpsilon = 1e-8f;

    Vec2 Normalized() const {
        const f32 lengthSq = LengthSquared();
        if (lengthSq < kNormalizeEpsilon) {
            return Vec2{0.0f, 0.0f};
        }
        const f32 inverse = 1.0f / std::sqrt(lengthSq);
        return Vec2{x * inverse, y * inverse};
    }

    // In place. Naming convention used across the whole engine:
    // a PAST PARTICIPLE returns a new value (Normalized, Transposed); a bare
    // VERB mutates in place (Normalize, Transpose).
    void Normalize() { *this = Normalized(); }

    // Perpendicular, rotated 90 degrees counter-clockwise. Two lines, and it
    // turns up constantly in 2D geometry.
    constexpr Vec2 Perpendicular() const { return Vec2{-y, x}; }

    static constexpr Vec2 Zero()  { return Vec2{0.0f, 0.0f}; }
    static constexpr Vec2 One()   { return Vec2{1.0f, 1.0f}; }
    static constexpr Vec2 UnitX() { return Vec2{1.0f, 0.0f}; }
    static constexpr Vec2 UnitY() { return Vec2{0.0f, 1.0f}; }
};

// --- free binary operators --------------------------------------------------
constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(const Vec2& v)                { return Vec2{-v.x, -v.y}; }
constexpr Vec2 operator*(const Vec2& v, f32 s)         { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator*(f32 s, const Vec2& v)         { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator/(const Vec2& v, f32 s)         { return Vec2{v.x / s, v.y / s}; }

// Component-wise product. Not a dot product; used for non-uniform scale.
constexpr Vec2 Scale(const Vec2& a, const Vec2& b) { return Vec2{a.x * b.x, a.y * b.y}; }

constexpr f32 Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

// In 2D the cross product is a SCALAR: the z component of the 3D cross product
// of the two vectors treated as lying in the xy plane. Its SIGN tells you
// which side of `a` the vector `b` is on, which makes it the workhorse of 2D
// geometry - winding order, point-in-triangle, separating axes.
constexpr f32 Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

constexpr f32 DistanceSquared(const Vec2& a, const Vec2& b) { return (b - a).LengthSquared(); }
inline    f32 Distance(const Vec2& a, const Vec2& b)        { return (b - a).Length(); }

constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t) { return a + (b - a) * t; }

// FLOATING POINT EQUALITY IS NOT EQUALITY. `==` asks whether two values have
// identical bit patterns, and arithmetic that mathematically produces the same
// number frequently does not: rotating (1,0) by 90 degrees gives an x of about
// -4.4e-8, not 0. Every test that checks a computed vector uses this.
//
// The default epsilon is absolute and suits a world measured in pixels, where
// coordinates are tens to thousands. For values spanning many orders of
// magnitude a relative comparison would be the right tool; this engine does
// not have that problem and a relative epsilon would misbehave near zero.
constexpr f32 kDefaultEpsilon = 1e-4f;

inline bool ApproxEqual(f32 a, f32 b, f32 epsilon = kDefaultEpsilon) {
    return std::fabs(a - b) <= epsilon;
}

inline bool ApproxEqual(const Vec2& a, const Vec2& b, f32 epsilon = kDefaultEpsilon) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon);
}

// Angle helpers. Radians everywhere in the engine; degrees only at the two
// edges where a human reads or writes them (the Inspector, and SDL's rotated
// blit, which wants degrees).
inline constexpr f32 kPi      = 3.14159265358979323846f;
inline constexpr f32 kTwoPi   = kPi * 2.0f;
inline constexpr f32 kDegToRad = kPi / 180.0f;
inline constexpr f32 kRadToDeg = 180.0f / kPi;

inline Vec2 FromAngle(f32 radians, f32 length = 1.0f) {
    return Vec2{std::cos(radians) * length, std::sin(radians) * length};
}

inline f32 AngleOf(const Vec2& v) { return std::atan2(v.y, v.x); }

} // namespace eng
