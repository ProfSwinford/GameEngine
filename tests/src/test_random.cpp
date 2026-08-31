// =============================================================================
//  WEEK 6 - the deterministic generator.
//
//  The cross-machine half of the Milestone 1 check cannot be done from inside
//  one process; that is `sandbox --random-check`, run on two machines and
//  diffed, and the output is pasted in docs/week06-milestone1.md.
//
//  What CAN be checked here is everything that makes the cross-machine result
//  possible: that the same seed gives the same sequence, that a specific seed
//  gives specific KNOWN VALUES (which is what would catch someone quietly
//  changing the algorithm), and that the range mapping is unbiased and in
//  range.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/math/Random.h>

#include <vector>

using namespace eng;

TEST_CASE("the same seed produces the same sequence") {
    Random a(12345);
    Random b(12345);
    for (int i = 0; i < 64; ++i) {
        CHECK(a.NextU64() == b.NextU64());
    }
}

TEST_CASE("different seeds produce different sequences") {
    Random a(1);
    Random b(2);
    int differences = 0;
    for (int i = 0; i < 32; ++i) {
        if (a.NextU64() != b.NextU64()) {
            ++differences;
        }
    }
    CHECK(differences == 32);
}

TEST_CASE("a specific seed produces specific known values") {
    // SplitMix64 is fully specified by its own arithmetic, so these are the
    // same numbers on every conforming compiler and platform - which is the
    // property the milestone's two-machine diff depends on.
    //
    // These constants were produced by this implementation and then checked
    // against the published SplitMix64 reference vectors for seed 0. If a
    // change to Random.cpp breaks this case, the sequence has changed and
    // every "reproduce it from the seed" claim in the engine is void.
    Random random(0);
    CHECK(random.NextU64() == 0xE220A8397B1DCDAFull);
    CHECK(random.NextU64() == 0x6E789E6AA1B965F4ull);
    CHECK(random.NextU64() == 0x06C45D188009454Full);
}

TEST_CASE("Reseed restarts the sequence") {
    Random random(999);
    const u64 first = random.NextU64();
    random.NextU64();
    random.NextU64();
    random.Reseed(999);
    CHECK(random.NextU64() == first);
}

TEST_CASE("the seed can be read back, so a bad run is reproducible") {
    // Phase 2 logs this at startup. It is one accessor and it is the
    // difference between "it crashes sometimes" and "it crashes on seed
    // 8827341".
    Random random(8827341);
    CHECK(random.Seed() == 8827341);
    random.NextU64();
    CHECK(random.Seed() == 8827341);   // consuming values does not change it
}

TEST_CASE("NextFloat01 stays in [0, 1)") {
    Random random(7);
    for (int i = 0; i < 20000; ++i) {
        const f32 value = random.NextFloat01();
        CHECK(value >= 0.0f);
        CHECK(value < 1.0f);   // never exactly 1
    }
}

TEST_CASE("NextRange stays within its bounds") {
    Random random(11);
    for (int i = 0; i < 10000; ++i) {
        const f32 value = random.NextRange(-3.5f, 9.25f);
        CHECK(value >= -3.5f);
        CHECK(value <= 9.25f);
    }
}

TEST_CASE("NextInt is inclusive at both ends and never leaves the range") {
    Random random(4242);
    bool sawLow  = false;
    bool sawHigh = false;
    for (int i = 0; i < 20000; ++i) {
        const i32 value = random.NextInt(1, 6);
        REQUIRE(value >= 1);
        REQUIRE(value <= 6);
        sawLow  = sawLow || (value == 1);
        sawHigh = sawHigh || (value == 6);
    }
    CHECK(sawLow);
    CHECK(sawHigh);   // hiInclusive really is inclusive
}

TEST_CASE("NextInt with a single-value range returns that value") {
    Random random(5);
    for (int i = 0; i < 100; ++i) {
        CHECK(random.NextInt(7, 7) == 7);
    }
}

TEST_CASE("NextInt is not visibly biased across a range that does not divide evenly") {
    // The modulo trap, checked empirically. A range of 3 does not divide 2^64
    // evenly, so the naive implementation is very slightly biased toward the
    // low end. The rejection loop removes it entirely; this case would catch a
    // GROSS bias (a broken mapping), which is what actually goes wrong in
    // practice - the theoretical modulo bias is around one part in 10^18 and
    // no test could see it.
    Random random(31337);
    int counts[3] = {0, 0, 0};
    constexpr int kSamples = 300000;
    for (int i = 0; i < kSamples; ++i) {
        ++counts[random.NextInt(0, 2)];
    }
    for (int count : counts) {
        const double share = static_cast<double>(count) / kSamples;
        CHECK(share > 0.32);
        CHECK(share < 0.35);
    }
}

TEST_CASE("NextInt handles negative ranges") {
    Random random(88);
    for (int i = 0; i < 5000; ++i) {
        const i32 value = random.NextInt(-10, -5);
        CHECK(value >= -10);
        CHECK(value <= -5);
    }
}

TEST_CASE("NextDirection returns a unit vector") {
    Random random(2024);
    for (int i = 0; i < 2000; ++i) {
        const Random::UnitVector direction = random.NextDirection();
        const f32 lengthSquared = direction.x * direction.x + direction.y * direction.y;
        CHECK(lengthSquared == doctest::Approx(1.0f).epsilon(0.001));
    }
}

TEST_CASE("the global generator exists and is deterministic after reseeding") {
    GlobalRandom().Reseed(555);
    const u64 first = GlobalRandom().NextU64();
    GlobalRandom().Reseed(555);
    CHECK(GlobalRandom().NextU64() == first);
}
