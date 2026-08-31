// =============================================================================
//  WEEK 6 - Vec2. The type everything with a position uses, so it is worth
//  pinning down completely.
//
//  Every comparison of a COMPUTED vector uses ApproxEqual rather than ==,
//  because floating point equality is not equality: rotating (1,0) by 90
//  degrees gives an x of about -4.4e-8, not 0.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/math/Vec2.h>

using namespace eng;

TEST_CASE("a default Vec2 is the origin") {
    const Vec2 v;
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
}

TEST_CASE("arithmetic operators") {
    const Vec2 a{3.0f, 4.0f};
    const Vec2 b{1.0f, 2.0f};

    CHECK(ApproxEqual(a + b, Vec2{4.0f, 6.0f}));
    CHECK(ApproxEqual(a - b, Vec2{2.0f, 2.0f}));
    CHECK(ApproxEqual(-a, Vec2{-3.0f, -4.0f}));
    CHECK(ApproxEqual(a * 2.0f, Vec2{6.0f, 8.0f}));
    CHECK(ApproxEqual(a / 2.0f, Vec2{1.5f, 2.0f}));
}

TEST_CASE("scalar multiplication works with the scalar on the left") {
    // The case that decided which operators are free functions: `2.0f * v`
    // cannot be a member of Vec2, because a member's left operand is always
    // its own class.
    const Vec2 v{3.0f, 4.0f};
    CHECK(ApproxEqual(2.0f * v, Vec2{6.0f, 8.0f}));
    CHECK(ApproxEqual(2.0f * v, v * 2.0f));
}

TEST_CASE("compound assignment mutates in place") {
    Vec2 v{1.0f, 1.0f};
    v += Vec2{2.0f, 3.0f};
    CHECK(ApproxEqual(v, Vec2{3.0f, 4.0f}));
    v -= Vec2{1.0f, 1.0f};
    CHECK(ApproxEqual(v, Vec2{2.0f, 3.0f}));
    v *= 2.0f;
    CHECK(ApproxEqual(v, Vec2{4.0f, 6.0f}));
    v /= 4.0f;
    CHECK(ApproxEqual(v, Vec2{1.0f, 1.5f}));
}

TEST_CASE("Length and LengthSquared agree") {
    const Vec2 v{3.0f, 4.0f};
    CHECK(v.LengthSquared() == doctest::Approx(25.0f));
    CHECK(v.Length() == doctest::Approx(5.0f));
}

TEST_CASE("LengthSquared orders identically to Length") {
    // The property that makes the sqrt-free comparison legitimate, checked
    // rather than assumed - because every distance comparison in the engine
    // relies on it.
    const Vec2 shorter{1.0f, 2.0f};
    const Vec2 longer{3.0f, 4.0f};
    CHECK((shorter.LengthSquared() < longer.LengthSquared()) ==
          (shorter.Length() < longer.Length()));
}

TEST_CASE("Normalized produces a unit vector and does not modify the source") {
    const Vec2 v{3.0f, 4.0f};
    const Vec2 unit = v.Normalized();
    CHECK(unit.Length() == doctest::Approx(1.0f));
    CHECK(ApproxEqual(unit, Vec2{0.6f, 0.8f}));
    CHECK(ApproxEqual(v, Vec2{3.0f, 4.0f}));   // source untouched
}

TEST_CASE("Normalize mutates in place") {
    Vec2 v{0.0f, 5.0f};
    v.Normalize();
    CHECK(ApproxEqual(v, Vec2{0.0f, 1.0f}));
}

TEST_CASE("normalizing a ZERO-LENGTH vector returns zero, not NaN") {
    // THE CASE THAT MATTERS, and the documented decision. Dividing by zero in
    // floating point does not crash - it produces inf or NaN, which then
    // propagates silently through every subsequent calculation and surfaces as
    // an entity that has vanished with no error anywhere.
    const Vec2 zero{0.0f, 0.0f};
    const Vec2 result = zero.Normalized();
    CHECK(result.x == 0.0f);
    CHECK(result.y == 0.0f);
    CHECK_FALSE(std::isnan(result.x));
    CHECK_FALSE(std::isinf(result.x));

    // A vector that is tiny but not exactly zero takes the same path, because
    // the epsilon is on the SQUARED length and 1e-9 squared is well under it.
    const Vec2 tiny{1e-9f, 0.0f};
    CHECK(tiny.Normalized().x == 0.0f);
}

TEST_CASE("Dot") {
    CHECK(Dot(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}) == doctest::Approx(0.0f));
    CHECK(Dot(Vec2{2.0f, 3.0f}, Vec2{4.0f, 5.0f}) == doctest::Approx(23.0f));
    CHECK(Dot(Vec2{1.0f, 0.0f}, Vec2{-1.0f, 0.0f}) == doctest::Approx(-1.0f));
}

TEST_CASE("Cross returns a SCALAR whose sign says which side") {
    // In 2D the cross product is the z component of the 3D cross product. Its
    // sign is the workhorse: positive means b is counter-clockwise from a.
    CHECK(Cross(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}) == doctest::Approx(1.0f));
    CHECK(Cross(Vec2{0.0f, 1.0f}, Vec2{1.0f, 0.0f}) == doctest::Approx(-1.0f));
    CHECK(Cross(Vec2{2.0f, 2.0f}, Vec2{4.0f, 4.0f}) == doctest::Approx(0.0f));  // parallel
}

TEST_CASE("Distance and DistanceSquared") {
    const Vec2 a{0.0f, 0.0f};
    const Vec2 b{3.0f, 4.0f};
    CHECK(Distance(a, b) == doctest::Approx(5.0f));
    CHECK(DistanceSquared(a, b) == doctest::Approx(25.0f));
    CHECK(Distance(a, b) == doctest::Approx(Distance(b, a)));
}

TEST_CASE("Lerp") {
    const Vec2 a{0.0f, 0.0f};
    const Vec2 b{10.0f, 20.0f};
    CHECK(ApproxEqual(Lerp(a, b, 0.0f), a));
    CHECK(ApproxEqual(Lerp(a, b, 1.0f), b));
    CHECK(ApproxEqual(Lerp(a, b, 0.5f), Vec2{5.0f, 10.0f}));
    // Not clamped, deliberately: extrapolation is useful and clamping would
    // silently change what a caller asked for.
    CHECK(ApproxEqual(Lerp(a, b, 2.0f), Vec2{20.0f, 40.0f}));
}

TEST_CASE("ApproxEqual tolerates the error that == does not") {
    // The exact case the function exists for.
    const Vec2 rotated = FromAngle(kPi * 0.5f);   // should be (0, 1)
    CHECK_FALSE(rotated == Vec2{0.0f, 1.0f});     // == is too strict
    CHECK(ApproxEqual(rotated, Vec2{0.0f, 1.0f}));
}

TEST_CASE("Perpendicular rotates 90 degrees counter-clockwise") {
    CHECK(ApproxEqual(Vec2{1.0f, 0.0f}.Perpendicular(), Vec2{0.0f, 1.0f}));
    CHECK(Dot(Vec2{3.0f, 7.0f}, Vec2{3.0f, 7.0f}.Perpendicular()) == doctest::Approx(0.0f));
}

TEST_CASE("FromAngle and AngleOf round-trip") {
    for (f32 angle = -3.0f; angle < 3.0f; angle += 0.37f) {
        CHECK(AngleOf(FromAngle(angle)) == doctest::Approx(angle).epsilon(0.001));
    }
}
