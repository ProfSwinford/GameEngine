// WEEK 6 - SplitMix64, written out so the sequence is identical everywhere.
// See Random.h for why no std:: distribution appears in this file.

#include <engine/core/Assert.h>
#include <engine/math/Random.h>
#include <engine/math/Vec2.h>

#include <cmath>

namespace eng {

u64 Random::NextU64() {
    // SplitMix64. The state advance is one addition by the golden-ratio
    // constant; the output is a fixed avalanche of that state. Every constant
    // and shift below is part of the algorithm's definition - changing any of
    // them changes the sequence, which is exactly what determinism forbids.
    m_state += 0x9E3779B97F4A7C15ull;
    u64 z = m_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

u32 Random::NextU32() {
    // The HIGH 32 bits. For SplitMix64 either half is fine, but taking the
    // high half is the habit that keeps you out of trouble with generators
    // whose low bits are weak.
    return static_cast<u32>(NextU64() >> 32);
}

f32 Random::NextFloat01() {
    // 24 bits is exactly f32's mantissa width, so every representable value in
    // [0,1) with 24-bit precision is reachable and none is reachable twice.
    // Dividing by 2^24 gives [0, 1) - never exactly 1.
    const u32 bits = static_cast<u32>(NextU64() >> 40);   // top 24 bits
    return static_cast<f32>(bits) * (1.0f / 16777216.0f);
}

f32 Random::NextRange(f32 lo, f32 hi) {
    return lo + (hi - lo) * NextFloat01();
}

i32 Random::NextInt(i32 lo, i32 hiInclusive) {
    ENGINE_ASSERT_MSG(lo <= hiInclusive, "Random::NextInt called with an inverted range");
    if (lo >= hiInclusive) {
        return lo;
    }

    // Width as an unsigned value so that NextInt(INT32_MIN, INT32_MAX) does
    // not overflow while computing its own range - a real bug in a lot of
    // hand-written versions of this function.
    const u64 range = static_cast<u64>(hiInclusive) - static_cast<u64>(lo) + 1ull;

    // Lemire-style rejection: reject the tail that would bias the result. See
    // the header for the arithmetic; the loop body essentially never repeats.
    const u64 limit     = (range == 0) ? 0 : (~0ull / range) * range;
    u64       candidate = NextU64();
    if (limit != 0) {
        while (candidate >= limit) {
            candidate = NextU64();
        }
    }

    return static_cast<i32>(static_cast<i64>(lo) + static_cast<i64>(candidate % range));
}

bool Random::NextBool() {
    // The top bit, not the bottom one, for the reason given in NextU32.
    return (NextU64() >> 63) != 0;
}

Random::UnitVector Random::NextDirection() {
    const f32 angle = NextRange(0.0f, kTwoPi);
    return UnitVector{std::cos(angle), std::sin(angle)};
}

Random& GlobalRandom() {
    // A function-local static, which is the standard answer to the static
    // initialization order fiasco Week 7 opens with: it is constructed on
    // first use, so it cannot be used before it exists no matter what order
    // translation units initialise in.
    static Random instance;
    return instance;
}

} // namespace eng
