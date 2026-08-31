#pragma once

// =============================================================================
//  WEEK 6 - a deterministic, seeded random number generator. Ch. 5.7.
//
//  DETERMINISTIC means more than "reproducible on my machine": the same seed
//  must produce the same sequence on every machine, and that is the
//  Milestone 1 verification.
//
//  ---------------------------------------------------------------------------
//  WHAT WAS CHOSEN, AND WHY - because the tempting shortcuts are all wrong.
//
//  RULED OUT: std::uniform_int_distribution and friends. The standard specifies
//  the GENERATOR ENGINES exactly (mt19937 produces a defined sequence), but it
//  does NOT specify the distributions. Two conforming standard libraries can
//  hand you different numbers from the same engine and the same seed, and both
//  are correct. That is precisely the cross-machine check M1 performs, so a
//  distribution would fail it in a way that looks like a bug in your code.
//
//  CHOSEN: SplitMix64, written out here, about ten lines.
//    - It is fully specified by its own arithmetic: no library, no ambiguity,
//      identical output on any conforming C++ compiler.
//    - 64-bit state, 64-bit output, passes the usual statistical suites, and
//      the state advance is a single addition.
//    - It has no bad seeds - unlike a plain LCG or an xorshift, seed 0 is
//      fine, which matters when a config file's default is 0.
//
//  The range mapping is written out below rather than delegated, for the same
//  portability reason.
//
//  Why an engine cares: reproducible bugs. "It crashes on level 3" is only
//  actionable if level 3 is the same every time. Phase 2 will want to replay a
//  run from a seed - which is why Seed() is readable and logged at startup.
// =============================================================================

#include <engine/core/Types.h>

namespace eng {

class Random {
public:
    // Default seed is a fixed, arbitrary constant rather than the clock. An
    // engine that seeds itself from the clock by default produces bugs nobody
    // can reproduce; seeding from the clock is a decision a caller makes on
    // purpose, once, and then logs.
    static constexpr u64 kDefaultSeed = 0x9E3779B97F4A7C15ull;

    Random() : Random(kDefaultSeed) {}
    explicit Random(u64 seed) : m_state(seed), m_seed(seed) {}

    u32 NextU32();
    u64 NextU64();

    // [0, 1). Uses the top 24 bits, which is exactly the mantissa width of an
    // f32 - taking the low bits instead is the classic mistake, because the
    // low bits of many generators are the weakest.
    f32 NextFloat01();

    f32 NextRange(f32 lo, f32 hi);

    // [lo, hiInclusive].
    //
    // THE MODULO TRAP, and what was done about it: `value % range` is very
    // slightly biased toward the low end whenever `range` does not divide
    // 2^64 evenly, because the last, partial block of the generator's output
    // space maps onto only the first few results. For a range of 6 out of 2^64
    // the bias is around 1 part in 3x10^18 and is genuinely unmeasurable.
    //
    // It is fixed anyway, with Lemire's rejection: draw again on the rare
    // values that fall in the partial block. The rejection probability is
    // range/2^64, so the loop essentially never runs twice, and the result is
    // exactly uniform rather than nearly uniform. "Not knowing" was the one
    // unacceptable answer; this costs nothing, so there was no reason to
    // accept the bias.
    i32 NextInt(i32 lo, i32 hiInclusive);

    bool NextBool();

    // Uniform on the unit circle. Used by the debug-draw stress scene and by
    // Phase 2 spawn code; it is here because writing it with two NextRange
    // calls and rejecting is the naive answer and this is the cheap one.
    struct UnitVector { f32 x, y; };
    UnitVector NextDirection();

    void Reseed(u64 seed) { m_state = seed; m_seed = seed; }

    // The seed this generator was last reseeded with. Log it at startup; a bad
    // run is then reproducible from the log alone.
    u64 Seed() const { return m_seed; }

private:
    u64 m_state = kDefaultSeed;
    u64 m_seed  = kDefaultSeed;
};

// The engine's shared generator. Deliberately a plain accessor rather than a
// hidden global sprinkled through the code - anything that wants
// reproducibility can construct its own Random with its own seed instead.
Random& GlobalRandom();

} // namespace eng
