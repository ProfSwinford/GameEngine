#pragma once

// =============================================================================
//  WEEK 4 - the AoS vs SoA experiment.
//
//  This lives in the SANDBOX, not the engine, because it is an experiment
//  rather than an engine capability. The benchmark itself is disposable; what
//  it taught changed how sprites are stored in Week 9 (see the AoS/SoA note at
//  the top of engine/include/engine/scene/Component.h).
//
//  Two layouts, one behaviour:
//
//    Array of Structs (AoS) - one array, each element holds every field. What
//        everyone writes first, and what a C# List<Particle> gives you.
//
//    Struct of Arrays (SoA) - one array PER FIELD, all the same length.
//        Element i is the i-th entry of every array. Uglier to write.
//
//  THE UPDATE DOES THE SAME WORK IN BOTH. If one version did less, the
//  measurement would be of a bug rather than of a layout - which is why
//  Verify() exists and why it is run before any timing is trusted.
//
//  THE PREDICTION, written down before the first run: SoA wins by roughly 1.3x,
//  because the update reads 24 of the 32 bytes per particle and AoS pays to
//  load the other 8 anyway.
//
//  THE MEASUREMENT SAID 3.7x. Being wrong by that much is the interesting part
//  and it is written up honestly in docs/week04-layout-report.md, follow-up 2:
//  the byte ratio explains a third of it, and the rest is that the SoA loops
//  are unit-stride over separate arrays and vectorise, while the AoS loop's
//  32-byte stride does not.
// =============================================================================

#include <engine/core/Types.h>

#include <vector>

namespace bench {

using eng::f32;
using eng::u32;
using eng::u64;
using eng::usize;

// ---------------------------------------------------------------------------
//  The AoS layout. REALISTIC: six fields, and TWO OF THEM ARE NEVER READ BY
//  THE UPDATE LOOP.
//
//  `spriteId` and `flags` are the point of the exercise. In AoS they sit
//  between the fields the update does read, so every cache line fetched to get
//  a position also carries them. In SoA they are in a different array that is
//  never touched.
//
//  MEASURED sizeof: 32 bytes, and REORDERING SAVES NOTHING HERE. Every member
//  is 4 bytes with 4-byte alignment, so there is nowhere for the compiler to
//  insert padding and no order that is smaller than any other. That is a
//  genuine finding rather than a disappointment - it is the control case in
//  docs/week04-sizeof-audit.md, and it is why the audit asks for the
//  arithmetic rather than for a saving.
//
//  24 of those 32 bytes are read by the update; 8 are not.
// ---------------------------------------------------------------------------
struct Particle {
    f32 positionX = 0.0f;    // read + written
    f32 positionY = 0.0f;    // read + written
    f32 velocityX = 0.0f;    // read
    f32 velocityY = 0.0f;    // read
    f32 lifetime  = 0.0f;    // read + written
    f32 size      = 0.0f;    // read (kept, so the update is not trivially SIMD)
    u32 spriteId  = 0;       // NEVER READ by the update
    u32 flags     = 0;       // NEVER READ by the update
};

// ---------------------------------------------------------------------------
//  The SoA layout. One array per field.
// ---------------------------------------------------------------------------
struct ParticlesSoA {
    std::vector<f32> positionX;
    std::vector<f32> positionY;
    std::vector<f32> velocityX;
    std::vector<f32> velocityY;
    std::vector<f32> lifetime;
    std::vector<f32> size;
    std::vector<u32> spriteId;   // never touched by the update
    std::vector<u32> flags;      // never touched by the update

    void Resize(usize count);
};

void SeedAoS(std::vector<Particle>& particles, usize count, u64 seed);
void SeedSoA(ParticlesSoA& particles, usize count, u64 seed);

// Identical observable results. Integrate position by velocity, decay
// lifetime.
void UpdateAoS(std::vector<Particle>& particles, f32 deltaSeconds);
void UpdateSoA(ParticlesSoA& particles, f32 deltaSeconds);

// Confirms both layouts produced the same numbers. RUN THIS BEFORE TRUSTING
// ANY TIMING: it is very easy to write a fast version that is fast because it
// is wrong, and the timings will not tell you.
bool Verify(const std::vector<Particle>& aos, const ParticlesSoA& soa);

// Runs the whole experiment and reports through the Week 4 scoped timers.
// Measure in a RELEASE build - a debug build measures the compiler's
// unoptimised output, which is not what the comparison is about.
void RunLayoutBenchmark(usize elementCount, int iterations);

// Prints sizeof and alignof for the REAL engine structs the Week 4 audit
// covers - not toy structs written for the exercise. Having the engine report
// its own layout is what makes docs/week04-sizeof-audit.md checkable rather
// than a table somebody typed.
void RunSizeofAudit();

} // namespace bench
