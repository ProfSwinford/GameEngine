// ============================================================================
//  Tests for eng::Random.
//
//  What is worth checking about a random number generator is not "does it look
//  random" - that is a statistics problem, and the standard library has
//  already solved it. What is worth checking is the part this engine is
//  responsible for: that the same SEED gives the same sequence, and that every
//  range function stays inside the range it was asked for.
// ============================================================================

#include <doctest/doctest.h>
#include <engine/math/Random.h>

using namespace eng;

TEST_CASE("the same seed produces the same sequence") {
    // This is what makes a bug reproducible. Without it, "it crashes
    // sometimes" can never become "it crashes on seed 12345".
    Random a(12345);
    Random b(12345);
    for (int i = 0; i < 64; ++i) {
        CHECK(a.NextInt(0, 1000000) == b.NextInt(0, 1000000));
    }
}

TEST_CASE("different seeds produce different sequences") {
    Random a(1);
    Random b(2);
    int differences = 0;
    for (int i = 0; i < 32; ++i) {
        if (a.NextInt(0, 1000000) != b.NextInt(0, 1000000)) {
            ++differences;
        }
    }
    // Two different sequences agreeing on even a few of thirty-two values
    // would be suspicious.
    CHECK(differences > 28);
}

TEST_CASE("Reseed restarts the sequence") {
    Random    random(999);
    const int first = random.NextInt(0, 1000000);
    random.NextInt(0, 1000000);
    random.NextInt(0, 1000000);
    random.Reseed(999);
    CHECK(random.NextInt(0, 1000000) == first);
}

TEST_CASE("the seed can be read back") {
    Random random(8827341);
    CHECK(random.Seed() == 8827341u);
    random.NextInt(1, 6);
    CHECK(random.Seed() == 8827341u);   // using it does not change it
}

TEST_CASE("NextFloat01 stays in [0, 1)") {
    Random random(7);
    for (int i = 0; i < 20000; ++i) {
        const float value = random.NextFloat01();
        CHECK(value >= 0.0f);
        CHECK(value < 1.0f);   // never exactly 1
    }
}

TEST_CASE("NextRange stays within its bounds") {
    Random random(11);
    for (int i = 0; i < 10000; ++i) {
        const float value = random.NextRange(-3.5f, 9.25f);
        CHECK(value >= -3.5f);
        CHECK(value <= 9.25f);
    }
}

TEST_CASE("NextInt includes both ends of its range") {
    // The "inclusive" in hiInclusive is the part people get wrong, so it is
    // checked directly: rolling a die twenty thousand times must produce both
    // a 1 and a 6, and never a 0 or a 7.
    Random random(4242);
    bool   sawLow  = false;
    bool   sawHigh = false;
    for (int i = 0; i < 20000; ++i) {
        const int value = random.NextInt(1, 6);
        REQUIRE(value >= 1);
        REQUIRE(value <= 6);
        sawLow  = sawLow || (value == 1);
        sawHigh = sawHigh || (value == 6);
    }
    CHECK(sawLow);
    CHECK(sawHigh);
}

TEST_CASE("NextInt with a single-value range returns that value") {
    Random random(5);
    for (int i = 0; i < 100; ++i) {
        CHECK(random.NextInt(7, 7) == 7);
    }
}

TEST_CASE("NextInt handles negative ranges") {
    Random random(88);
    for (int i = 0; i < 5000; ++i) {
        const int value = random.NextInt(-10, -5);
        CHECK(value >= -10);
        CHECK(value <= -5);
    }
}

TEST_CASE("NextInt spreads values evenly enough") {
    // Not a rigorous statistical test - the standard library's generator has
    // already been tested far more thoroughly than anything here could. This
    // would catch a GROSSLY broken range mapping, which is what actually goes
    // wrong in practice.
    Random    random(31337);
    int       counts[3] = {0, 0, 0};
    const int kSamples  = 300000;
    for (int i = 0; i < kSamples; ++i) {
        ++counts[random.NextInt(0, 2)];
    }
    for (const int count : counts) {
        const double share = static_cast<double>(count) / kSamples;
        CHECK(share > 0.32);
        CHECK(share < 0.35);
    }
}

TEST_CASE("NextDirection returns a vector of length one") {
    Random random(2024);
    for (int i = 0; i < 2000; ++i) {
        const Random::UnitVector direction = random.NextDirection();
        const float lengthSquared =
            direction.x * direction.x + direction.y * direction.y;
        // doctest::Approx compares floats with a tolerance, which is what you
        // always want for a computed value. See the note in Vec2.h.
        CHECK(lengthSquared == doctest::Approx(1.0f).epsilon(0.001));
    }
}

TEST_CASE("the shared generator can also be reseeded") {
    GlobalRandom().Reseed(555);
    const int first = GlobalRandom().NextInt(0, 1000000);
    GlobalRandom().Reseed(555);
    CHECK(GlobalRandom().NextInt(0, 1000000) == first);
}
