#pragma once

// ============================================================================
//  Vec2.h - a 2D point or direction: two floats, x and y.
//
//  This is the type used for every position, size, velocity and offset in the
//  engine. If something in a 2D game has a location, it is stored in a Vec2.
//
//  WHY EVERYTHING IS DEFINED IN THE HEADER
//  Normally a class is declared in a .h and implemented in a .cpp. Not here.
//  Every function below is one or two arithmetic operations, and the cost of
//  *calling* a function is larger than the cost of the work it does. Putting
//  the bodies in the header lets the compiler paste them straight into the
//  caller (this is called inlining) and the call disappears entirely.
//
//  WHY `constexpr` IS ON ALMOST EVERYTHING
//  `constexpr` tells the compiler "this can be worked out while compiling, if
//  the inputs are known then". So `Vec2{3, 4} + Vec2{1, 1}` in your source
//  becomes the literal value (4, 5) in the finished program - no addition
//  happens while the game runs.
//
//  WHY <cmath> IS INCLUDED
//  It is the C++ standard maths header, and it is where std::sqrt, std::cos,
//  std::sin, std::atan2 and std::fabs come from. There is no reason to write
//  those by hand: the standard library versions are correct, fast, and
//  available on every platform.
// ============================================================================

#include <cmath>

namespace eng {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float inX, float inY) : x(inX), y(inY) {}

    // ---- operators that CHANGE this vector -------------------------------
    // These are member functions because the thing on the left of `+=` is
    // always a Vec2, which is exactly what a member function requires.
    constexpr Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    constexpr Vec2& operator*=(float scalar)    { x *= scalar; y *= scalar; return *this; }
    constexpr Vec2& operator/=(float scalar)    { x /= scalar; y /= scalar; return *this; }

    // `= default` asks the compiler to write == and != for us by comparing
    // every member. This is EXACT equality, which is rarely what you want for
    // floats - see ApproxEqual near the bottom of this file.
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;

    // The length of the vector, squared. This exists so that COMPARING two
    // distances never has to compute a square root:
    //
    //     if (a.LengthSquared() < b.LengthSquared())   // same answer,
    //     if (a.Length()        < b.Length())          // more work
    //
    // Both lines order the vectors identically, because squaring never changes
    // the order of non-negative numbers. Prefer the first.
    constexpr float LengthSquared() const { return x * x + y * y; }
    float           Length() const        { return std::sqrt(LengthSquared()); }

    // A vector pointing the same way but exactly 1 unit long.
    //
    // A vector of length zero has no direction, so there is nothing correct to
    // return. Dividing by zero here would NOT crash - floating point produces
    // "inf" or "NaN" instead, and those values then spread silently through
    // every later calculation until an object simply vanishes from the screen
    // with no error message anywhere. Returning (0, 0) is also wrong, but it
    // is wrong in one visible place instead of everywhere downstream.
    static constexpr float kNormalizeEpsilon = 1e-8f;

    Vec2 Normalized() const {
        const float lengthSq = LengthSquared();
        if (lengthSq < kNormalizeEpsilon) {
            return Vec2{0.0f, 0.0f};
        }
        // One division and two multiplies instead of two divisions.
        const float inverse = 1.0f / std::sqrt(lengthSq);
        return Vec2{x * inverse, y * inverse};
    }

    // Naming rule used throughout the engine: a name ending in -ed returns a
    // NEW value and leaves the original alone (Normalized); the bare verb
    // changes the object in place (Normalize).
    void Normalize() { *this = Normalized(); }

    // Turned 90 degrees anticlockwise. Two lines, and it comes up constantly
    // in 2D geometry - it is how you get the "sideways" direction from a
    // "forwards" one.
    constexpr Vec2 Perpendicular() const { return Vec2{-y, x}; }

    static constexpr Vec2 Zero()  { return Vec2{0.0f, 0.0f}; }
    static constexpr Vec2 One()   { return Vec2{1.0f, 1.0f}; }
    static constexpr Vec2 UnitX() { return Vec2{1.0f, 0.0f}; }
    static constexpr Vec2 UnitY() { return Vec2{0.0f, 1.0f}; }
};

// ---- operators that PRODUCE a new vector -----------------------------------
// These are free functions rather than members. The reason is the third line:
// `2.0f * v` has a float on the left, and a member function's left-hand side
// is always its own class. Once one of them has to be free, they all are, so
// that the whole family looks the same.
constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(const Vec2& v)                { return Vec2{-v.x, -v.y}; }
constexpr Vec2 operator*(const Vec2& v, float s)       { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator*(float s, const Vec2& v)       { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator/(const Vec2& v, float s)       { return Vec2{v.x / s, v.y / s}; }

// Multiplies x by x and y by y. Used for non-uniform scaling ("twice as wide,
// the same height"). This is NOT the dot product.
constexpr Vec2 Scale(const Vec2& a, const Vec2& b) { return Vec2{a.x * b.x, a.y * b.y}; }

// The dot product. Positive when the two vectors point roughly the same way,
// zero when they are at right angles, negative when they oppose.
constexpr float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

// In 2D the cross product is a single number, not a vector. Its SIGN says
// which side of `a` the vector `b` lies on, which makes it the standard tool
// for "is this point left or right of that line?"
constexpr float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

constexpr float DistanceSquared(const Vec2& a, const Vec2& b) { return (b - a).LengthSquared(); }
inline    float Distance(const Vec2& a, const Vec2& b)        { return (b - a).Length(); }

// Linear interpolation: t = 0 gives a, t = 1 gives b, t = 0.5 gives the midpoint.
constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }

// ---- comparing floats ------------------------------------------------------
//
// `==` on floats asks whether two numbers have identical bit patterns, and
// arithmetic that should mathematically give the same answer very often does
// not. Rotating (1, 0) by 90 degrees produces an x of about -0.000000044,
// not 0. Use these instead of == for anything that came out of a calculation.
inline constexpr float kDefaultEpsilon = 1e-4f;

inline bool ApproxEqual(float a, float b, float epsilon = kDefaultEpsilon) {
    return std::fabs(a - b) <= epsilon;
}

inline bool ApproxEqual(const Vec2& a, const Vec2& b, float epsilon = kDefaultEpsilon) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon);
}

// ---- angles ----------------------------------------------------------------
//
// The engine works in RADIANS everywhere. Degrees appear in exactly two
// places: the Inspector, because people think in degrees, and the call into
// SDL that draws a rotated sprite, because SDL's API asks for degrees.
inline constexpr float kPi       = 3.14159265358979323846f;
inline constexpr float kTwoPi    = kPi * 2.0f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

// Builds a vector pointing at `radians`, `length` units long.
inline Vec2 FromAngle(float radians, float length = 1.0f) {
    return Vec2{std::cos(radians) * length, std::sin(radians) * length};
}

// The angle a vector points in. std::atan2 is used rather than std::atan
// because it takes x and y separately and therefore knows which quadrant the
// vector is in; plain atan(y/x) cannot tell (1,1) from (-1,-1).
inline float AngleOf(const Vec2& v) { return std::atan2(v.y, v.x); }

} // namespace eng
