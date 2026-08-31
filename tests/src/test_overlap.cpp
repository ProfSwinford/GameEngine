// ============================================================================
//  Tests for the shape-overlap functions in math/Overlap.h.
//
//  The awkward cases are the interesting ones: shapes that touch exactly,
//  shapes nested completely inside one another, and a circle sitting off the
//  CORNER of a box. That last one is where the tempting shortcut - comparing
//  bounding boxes - gives the wrong answer.
//
//  kTouchingCounts below matches the decision recorded at the top of
//  Overlap.h. The point is not which answer is chosen; it is that every
//  function agrees with the same choice.
// ============================================================================

#include <doctest/doctest.h>
#include <engine/math/Overlap.h>

using namespace eng;

// Matches the decision recorded at the top of Overlap.h. Every case below is
// written in terms of this, so the whole file agrees with one answer.
static constexpr bool kTouchingCounts = true;

// --- the awkward cases -----------------------------------------------------

TEST_CASE("boxes sharing exactly one edge") {
    const AABB left {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const AABB right{{1.0f, 0.0f}, {2.0f, 1.0f}};
    CHECK(Overlaps(left, right) == kTouchingCounts);
}

TEST_CASE("boxes sharing exactly one corner") {
    const AABB a{{0.0f, 0.0f}, {1.0f, 1.0f}};
    const AABB b{{1.0f, 1.0f}, {2.0f, 2.0f}};
    CHECK(Overlaps(a, b) == kTouchingCounts);
}

TEST_CASE("circles touching at exactly one point") {
    const Circle a{{0.0f, 0.0f}, 1.0f};
    const Circle b{{2.0f, 0.0f}, 1.0f};
    CHECK(Overlaps(a, b) == kTouchingCounts);
}

TEST_CASE("full containment counts as overlap in every combination") {
    const AABB   big   {{0.0f, 0.0f}, {10.0f, 10.0f}};
    const AABB   small {{4.0f, 4.0f}, { 5.0f,  5.0f}};
    const Circle inside{{5.0f, 5.0f}, 1.0f};
    const Circle huge  {{5.0f, 5.0f}, 50.0f};

    CHECK(Overlaps(big, small));
    CHECK(Overlaps(small, big));       // order must not matter
    CHECK(Overlaps(big, inside));
    CHECK(Overlaps(big, huge));        // box entirely inside the circle
}

TEST_CASE("zero-size shapes behave as documented") {
    const AABB   point {{5.0f, 5.0f}, {5.0f, 5.0f}};
    const AABB   around{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const Circle dot   {{5.0f, 5.0f}, 0.0f};

    CHECK(Overlaps(point, around));
    CHECK(Overlaps(around, dot));
    CHECK(Contains(around, Vec2{5.0f, 5.0f}));
}

TEST_CASE("a circle near a box corner is not fooled by the bounding box") {
    // The classic false positive. This circle's bounding box overlaps the
    // box's corner, but the circle itself does not reach it. An
    // implementation that compares bounding boxes instead of clamping to the
    // nearest point on the box gets this wrong, and it is the single most
    // common AABB-circle bug.
    const AABB   box{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const Circle c  {{11.0f, 11.0f}, 1.2f};
    CHECK_FALSE(Overlaps(box, c));
}

TEST_CASE("separation on one axis is enough to rule out overlap") {
    const AABB a{{0.0f, 0.0f}, {1.0f, 100.0f}};
    const AABB b{{5.0f, 0.0f}, {6.0f, 100.0f}};
    CHECK_FALSE(Overlaps(a, b));
}

// --- the ordinary cases -----------------------------------------------------
//
// Four shape combinations, each clearly overlapping and clearly separated,
// plus Contains() on a boundary - which is the kTouchingCounts decision in a
// third place and must agree with the other two.

TEST_CASE("AABB vs AABB: clearly overlapping and clearly separated") {
    const AABB a{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const AABB overlapping{{5.0f, 5.0f}, {15.0f, 15.0f}};
    const AABB far{{100.0f, 100.0f}, {110.0f, 110.0f}};

    CHECK(Overlaps(a, overlapping));
    CHECK(Overlaps(overlapping, a));      // order must not matter
    CHECK_FALSE(Overlaps(a, far));
    CHECK_FALSE(Overlaps(far, a));
}

TEST_CASE("circle vs circle: clearly overlapping and clearly separated") {
    const Circle a{{0.0f, 0.0f}, 5.0f};
    const Circle overlapping{{6.0f, 0.0f}, 3.0f};
    const Circle far{{50.0f, 0.0f}, 1.0f};

    CHECK(Overlaps(a, overlapping));
    CHECK(Overlaps(overlapping, a));
    CHECK_FALSE(Overlaps(a, far));
}

TEST_CASE("AABB vs circle: clearly overlapping and clearly separated, both orders") {
    const AABB   box{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const Circle touchingFace{{13.0f, 5.0f}, 4.0f};    // reaches the right face
    const Circle far{{40.0f, 5.0f}, 4.0f};

    CHECK(Overlaps(box, touchingFace));
    CHECK(Overlaps(touchingFace, box));               // the mirrored overload
    CHECK_FALSE(Overlaps(box, far));
    CHECK_FALSE(Overlaps(far, box));
}

TEST_CASE("a circle whose centre is inside the box overlaps it") {
    // The configuration where the clamped point IS the centre and the distance
    // is zero. Handled by the same branch-free clamp, and checked because an
    // implementation that only considered the outside cases would miss it.
    const AABB   box{{0.0f, 0.0f}, {10.0f, 10.0f}};
    const Circle inside{{5.0f, 5.0f}, 1.0f};
    CHECK(Overlaps(box, inside));
}

TEST_CASE("Contains for an AABB, including a point exactly on the boundary") {
    const AABB box{{0.0f, 0.0f}, {10.0f, 10.0f}};

    CHECK(Contains(box, Vec2{5.0f, 5.0f}));                       // inside
    CHECK_FALSE(Contains(box, Vec2{11.0f, 5.0f}));                // outside
    CHECK(Contains(box, Vec2{0.0f, 5.0f}) == kTouchingCounts);    // on an edge
    CHECK(Contains(box, Vec2{10.0f, 10.0f}) == kTouchingCounts);  // on a corner
}

TEST_CASE("Contains for a circle, including a point exactly on the rim") {
    const Circle circle{{0.0f, 0.0f}, 5.0f};

    CHECK(Contains(circle, Vec2{1.0f, 1.0f}));
    CHECK_FALSE(Contains(circle, Vec2{6.0f, 0.0f}));
    CHECK(Contains(circle, Vec2{5.0f, 0.0f}) == kTouchingCounts);   // on the rim
}

TEST_CASE("ClosestPointOnAABB clamps rather than projecting") {
    const AABB box{{0.0f, 0.0f}, {10.0f, 10.0f}};

    CHECK(ApproxEqual(ClosestPointOnAABB(box, Vec2{5.0f, 20.0f}), Vec2{5.0f, 10.0f}));
    CHECK(ApproxEqual(ClosestPointOnAABB(box, Vec2{-5.0f, -5.0f}), Vec2{0.0f, 0.0f}));
    // A point inside returns itself, which is what makes the circle test's
    // "centre inside the box" case work with no extra branch.
    CHECK(ApproxEqual(ClosestPointOnAABB(box, Vec2{3.0f, 4.0f}), Vec2{3.0f, 4.0f}));
}

TEST_CASE("AABB helpers agree with each other") {
    const AABB box = AABB::FromCenterHalfExtents(Vec2{5.0f, 5.0f}, Vec2{2.0f, 3.0f});
    CHECK(ApproxEqual(box.min, Vec2{3.0f, 2.0f}));
    CHECK(ApproxEqual(box.max, Vec2{7.0f, 8.0f}));
    CHECK(ApproxEqual(box.Center(), Vec2{5.0f, 5.0f}));
    CHECK(ApproxEqual(box.Size(), Vec2{4.0f, 6.0f}));
    CHECK(ApproxEqual(box.Extents(), Vec2{2.0f, 3.0f}));
    CHECK(box.IsValid());
}

TEST_CASE("Encapsulate grows a box to contain a point") {
    AABB box{{0.0f, 0.0f}, {1.0f, 1.0f}};
    box.Encapsulate(Vec2{5.0f, -3.0f});
    CHECK(ApproxEqual(box.min, Vec2{0.0f, -3.0f}));
    CHECK(ApproxEqual(box.max, Vec2{5.0f, 1.0f}));
    CHECK(Contains(box, Vec2{5.0f, -3.0f}) == kTouchingCounts);
}
