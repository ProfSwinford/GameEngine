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

#include <algorithm>   // std::clamp, used by Clamp01 and InverseLerp below
#include <cmath>
#include <format>
#include <string>

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

    // The four compass directions by name. UnitX and UnitY above are the
    // mathematical spelling; these are the one game code writes, and
    // `Vec2::Down() * fallSpeed` says what it means in a way that
    // `-Vec2::UnitY() * fallSpeed` does not.
    //
    // +Y IS UP IN THIS ENGINE. Worth stating here rather than leaving to be
    // discovered, because the renderer's own coordinates run the other way
    // and Down() is exactly where somebody would get it wrong.
    static constexpr Vec2 Up()    { return Vec2{0.0f, 1.0f}; }
    static constexpr Vec2 Down()  { return Vec2{0.0f, -1.0f}; }
    static constexpr Vec2 Right() { return Vec2{1.0f, 0.0f}; }
    static constexpr Vec2 Left()  { return Vec2{-1.0f, 0.0f}; }
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

// ---- the handful of operations game code writes constantly -----------------
//
// ==========================================================================
//  WHAT IS DELIBERATELY NOT HERE, so that nobody looks for it
//
//  No Lerp(float, float, float) - std::lerp is in <cmath>, which this file
//  already includes, and it handles the edge cases correctly. No Min, Max,
//  Abs, Sqrt or Clamp for scalars: std::min, std::max, std::fabs, std::sqrt
//  and std::clamp are correct, fast and available on every platform, and a
//  wrapper would only be one more spelling to learn. The same rule the rest
//  of this header follows.
//
//  What IS here is the set that has no standard form, or whose standard form
//  is wrong for a game: a clamp that reads as a speed limit, a step that
//  cannot overshoot, an angle difference that knows angles wrap.
// ==========================================================================

// Turn a vector by an angle, anticlockwise.
//
// The alternatives are both worse: going through Mat3 builds a whole matrix
// for two multiplies of real work, and going through FromAngle(AngleOf(v) + a)
// * v.Length() costs two transcendental calls and a square root to get back
// what it started with. For the quarter turn specifically, Vec2::Perpendicular
// is cheaper still and exact.
inline Vec2 Rotated(const Vec2& v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

// Bounce a direction off a surface. `unitNormal` must be unit length, which is
// exactly what a Collision2D hands you - see physics/Contact.h. A
// non-unit normal silently scales the result, which is why the requirement is
// in the parameter name.
inline Vec2 Reflect(const Vec2& direction, const Vec2& unitNormal) {
    return direction - unitNormal * (2.0f * Dot(direction, unitNormal));
}

// Shorten a vector to at most maxLength, leaving a shorter one ALONE.
//
// This is how a speed limit is spelled. The naive version - Normalized() *
// std::min(Length(), maxLength) - also normalises a vector that was already
// short enough, so a thing drifting at 3 units a second is snapped up to the
// limit instead of being left drifting.
inline Vec2 ClampMagnitude(const Vec2& v, float maxLength) {
    if (maxLength <= 0.0f) {
        return Vec2::Zero();
    }
    const float lengthSq = v.LengthSquared();
    if (lengthSq <= maxLength * maxLength) {
        return v;
    }
    return v * (maxLength / std::sqrt(lengthSq));
}

// Step from `current` towards `target` by at most maxDelta, NEVER
// OVERSHOOTING. Both forms, because a number needs this as often as a
// position does - a health bar catching up to a new value, a volume fading.
//
// WHY NOT Lerp. Lerp(current, target, 0.1f) is the usual answer and it is
// subtly wrong twice: it never actually arrives, so the comparison `== target`
// is never true and whatever was waiting on it never fires; and the distance
// covered depends on the frame rate, so it moves at a different speed on a
// different machine. This arrives, exactly, and arrives at the same moment
// everywhere.
inline float MoveTowards(float current, float target, float maxDelta) {
    const float delta = target - current;
    if (std::fabs(delta) <= maxDelta) {
        return target;
    }
    return current + (delta > 0.0f ? maxDelta : -maxDelta);
}

inline Vec2 MoveTowards(const Vec2& current, const Vec2& target, float maxDelta) {
    const Vec2  delta    = target - current;
    const float lengthSq = delta.LengthSquared();
    if (lengthSq <= maxDelta * maxDelta || lengthSq <= Vec2::kNormalizeEpsilon) {
        return target;
    }
    return current + delta * (maxDelta / std::sqrt(lengthSq));
}

// 0 at `from`, 1 at `to`, and where a value sits between them. The inverse of
// a lerp, which is what a progress bar and a "how far through the fade am I"
// both need. A zero-length range answers 0 rather than dividing.
inline float InverseLerp(float from, float to, float value) {
    const float range = to - from;
    if (range == 0.0f) {
        return 0.0f;
    }
    return std::clamp((value - from) / range, 0.0f, 1.0f);
}

// std::clamp(v, 0, 1), which is written often enough to deserve a name.
inline float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

// ---- angles, which wrap, and that is the whole point ----------------------

// An angle folded into [-kPi, kPi]. Every angle comparison in a game needs
// this somewhere, because 359 degrees and -1 degree are the same direction and
// no amount of arithmetic on the raw numbers knows that.
inline float WrapAngle(float radians) {
    radians = std::fmod(radians + kPi, kTwoPi);
    if (radians < 0.0f) {
        radians += kTwoPi;
    }
    return radians - kPi;
}

// The SHORTEST way round from `from` to `to`, signed. Turning from 350 degrees
// to 10 degrees is +20, not -340 - and a turret written with the plain
// subtraction spins the long way round exactly once per revolution, which is
// the kind of bug that looks like a physics problem.
inline float DeltaAngle(float from, float to) { return WrapAngle(to - from); }

// Turn from `from` towards `to` by at most maxDelta, the short way round and
// without overshooting. MoveTowards for angles.
inline float MoveTowardsAngle(float from, float to, float maxDelta) {
    const float delta = DeltaAngle(from, to);
    if (std::fabs(delta) <= maxDelta) {
        return WrapAngle(to);
    }
    return WrapAngle(from + (delta > 0.0f ? maxDelta : -maxDelta));
}

// The signed angle from `from` to `to`, in [-kPi, kPi]. Positive means `to` is
// anticlockwise of `from`. This is the "which way should I turn" question, and
// the sign is the answer - which is why Cross is used rather than the
// arc-cosine of Dot, whose result has no sign at all.
inline float SignedAngle(const Vec2& from, const Vec2& to) {
    return std::atan2(Cross(from, to), Dot(from, to));
}

} // namespace eng

// ---------------------------------------------------------------------------
//  So that "{}" prints a Vec2.
//
//      Debug::Log("standing at {}", transform->WorldPosition());
//      ENGINE_LOG_WARN(Channels::kPhysics, "normal was {}", hit.normal);
//
//  prints `standing at (120, 48.5)`. Without this, the two lines above are a
//  wall of template errors ending in "the associated constraints are not
//  satisfied", which says nothing about the actual problem - that a position
//  is the single most commonly printed value in a 2D engine and there was no
//  way to print one.
//
//  THE FORMAT SPECIFICATION IS HANDED TO THE NUMBERS, NOT TO THE FINISHED
//  TEXT. So "{:.2f}" means two decimals on each of x and y - `(120.00,
//  48.50)` - rather than trying to apply `.2f` to a string and failing. That
//  is why this is a formatter written out rather than one inheriting from
//  std::formatter<std::string>, which is how NameHandle and TagHandle do it
//  in scene/ScriptComponent.h: for a name, padding the whole thing is what
//  you want; for a pair of numbers it is not.
//
//  Verified at /std:c++20, which is what the editor compiles a project's
//  scripts with. The engine itself is C++23, and that gap is exactly where a
//  formatter breaks if it is written against the newer library.
// ---------------------------------------------------------------------------
template <>
struct std::formatter<eng::Vec2> {
    std::formatter<float> number;

    constexpr auto parse(std::format_parse_context& context) {
        return number.parse(context);
    }

    auto format(const eng::Vec2& value, std::format_context& context) const {
        auto out = context.out();
        *out++   = '(';
        context.advance_to(out);
        out    = number.format(value.x, context);
        *out++ = ',';
        *out++ = ' ';
        context.advance_to(out);
        out    = number.format(value.y, context);
        *out++ = ')';
        return out;
    }
};