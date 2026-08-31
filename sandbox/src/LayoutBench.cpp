// WEEK 4 - the AoS vs SoA experiment. See LayoutBench.h.

#include "LayoutBench.h"

#include <engine/core/Log.h>
#include <engine/debug/ScopedTimer.h>
#include <engine/math/Random.h>
#include <engine/Engine.h>
#include <engine/math/Vec2.h>

#include <cmath>

namespace bench {

void ParticlesSoA::Resize(usize count) {
    positionX.resize(count);
    positionY.resize(count);
    velocityX.resize(count);
    velocityY.resize(count);
    lifetime.resize(count);
    size.resize(count);
    spriteId.resize(count);
    flags.resize(count);
}

void SeedAoS(std::vector<Particle>& particles, usize count, u64 seed) {
    eng::Random random(seed);
    particles.resize(count);
    for (usize i = 0; i < count; ++i) {
        particles[i].positionX = random.NextRange(-500.0f, 500.0f);
        particles[i].positionY = random.NextRange(-500.0f, 500.0f);
        particles[i].velocityX = random.NextRange(-40.0f, 40.0f);
        particles[i].velocityY = random.NextRange(-40.0f, 40.0f);
        particles[i].lifetime  = random.NextRange(1.0f, 10.0f);
        particles[i].size      = random.NextRange(1.0f, 4.0f);
        particles[i].spriteId  = random.NextU32();
        particles[i].flags     = random.NextU32();
    }
}

void SeedSoA(ParticlesSoA& particles, usize count, u64 seed) {
    // THE SAME SEED, so both layouts start from identical data - which is what
    // makes Verify() meaningful. The generator is deterministic (see Random.h),
    // so this is exact rather than approximate.
    eng::Random random(seed);
    particles.Resize(count);
    for (usize i = 0; i < count; ++i) {
        particles.positionX[i] = random.NextRange(-500.0f, 500.0f);
        particles.positionY[i] = random.NextRange(-500.0f, 500.0f);
        particles.velocityX[i] = random.NextRange(-40.0f, 40.0f);
        particles.velocityY[i] = random.NextRange(-40.0f, 40.0f);
        particles.lifetime[i]  = random.NextRange(1.0f, 10.0f);
        particles.size[i]      = random.NextRange(1.0f, 4.0f);
        particles.spriteId[i]  = random.NextU32();
        particles.flags[i]     = random.NextU32();
    }
}

void UpdateAoS(std::vector<Particle>& particles, f32 deltaSeconds) {
    for (Particle& particle : particles) {
        particle.positionX += particle.velocityX * deltaSeconds;
        particle.positionY += particle.velocityY * deltaSeconds;
        particle.lifetime  -= deltaSeconds * (1.0f + particle.size * 0.01f);
    }
}

void UpdateSoA(ParticlesSoA& particles, f32 deltaSeconds) {
    const usize count = particles.positionX.size();
    for (usize i = 0; i < count; ++i) {
        particles.positionX[i] += particles.velocityX[i] * deltaSeconds;
        particles.positionY[i] += particles.velocityY[i] * deltaSeconds;
        particles.lifetime[i]  -= deltaSeconds * (1.0f + particles.size[i] * 0.01f);
    }
}

bool Verify(const std::vector<Particle>& aos, const ParticlesSoA& soa) {
    if (aos.size() != soa.positionX.size()) {
        return false;
    }
    for (usize i = 0; i < aos.size(); ++i) {
        // Exact float equality is correct HERE and nowhere else in the engine:
        // both loops perform the identical sequence of operations on identical
        // inputs, so the results must be bit-identical. An epsilon would hide
        // a real divergence, which is the thing this function exists to find.
        if (aos[i].positionX != soa.positionX[i] || aos[i].positionY != soa.positionY[i] ||
            aos[i].lifetime != soa.lifetime[i]) {
            return false;
        }
    }
    return true;
}

void RunLayoutBenchmark(usize elementCount, int iterations) {
    ENGINE_LOG_INFO(eng::Channels::kProfile,
                    "AoS/SoA benchmark: {} elements x {} iterations", elementCount,
                    iterations);
    ENGINE_LOG_INFO(eng::Channels::kProfile, "sizeof(Particle) = {} bytes; {} per 64-byte "
                                             "cache line",
                    sizeof(Particle), 64 / sizeof(Particle));

    std::vector<Particle> aos;
    ParticlesSoA          soa;

    constexpr u64 kSeed = 0xC0FFEEull;
    SeedAoS(aos, elementCount, kSeed);
    SeedSoA(soa, elementCount, kSeed);

    constexpr f32 kDelta = 1.0f / 60.0f;

    // VERIFY BEFORE TIMING. One update each, then compare. It is very easy to
    // write a fast version that is fast because it is wrong.
    UpdateAoS(aos, kDelta);
    UpdateSoA(soa, kDelta);
    if (!Verify(aos, soa)) {
        ENGINE_LOG_ERROR(eng::Channels::kProfile,
                         "VERIFICATION FAILED: the two layouts disagree. Any timing "
                         "below would be measuring a bug.");
        return;
    }
    ENGINE_LOG_INFO(eng::Channels::kProfile, "verification passed: both layouts agree");

    // Re-seed so the timed runs start from the same place.
    SeedAoS(aos, elementCount, kSeed);
    SeedSoA(soa, elementCount, kSeed);

    // The timer's own overhead, measured rather than assumed - follow-up 4 in
    // the report. An instrument that is a significant fraction of the signal
    // is not an instrument.
    {
        constexpr int kEmptyScopes = 100000;
        const auto    start = std::chrono::steady_clock::now();
        for (int i = 0; i < kEmptyScopes; ++i) {
            ENGINE_SCOPED_TIMER("bench.emptyScope");
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double totalMs = std::chrono::duration<double, std::milli>(elapsed).count();
        ENGINE_LOG_INFO(eng::Channels::kProfile,
                        "ScopedTimer overhead: {:.1f} ns per timed scope ({} samples)",
                        totalMs * 1e6 / kEmptyScopes, kEmptyScopes);
    }

    for (int i = 0; i < iterations; ++i) {
        ENGINE_SCOPED_TIMER("bench.AoS");
        UpdateAoS(aos, kDelta);
    }
    for (int i = 0; i < iterations; ++i) {
        ENGINE_SCOPED_TIMER("bench.SoA");
        UpdateSoA(soa, kDelta);
    }

    // The compiler is entitled to delete a loop whose results nothing reads.
    // Touching one value afterwards is the standard defence, and it is worth
    // saying out loud: "AoS and SoA came out identical" is sometimes because
    // both were optimised away entirely.
    ENGINE_LOG_INFO(eng::Channels::kProfile, "sink (ignore): {:.3f} / {:.3f}",
                    static_cast<double>(aos[elementCount / 2].positionX),
                    static_cast<double>(soa.positionX[elementCount / 2]));

    const eng::TimerStats aosStats = eng::TimerRegistry::Get("bench.AoS");
    const eng::TimerStats soaStats = eng::TimerRegistry::Get("bench.SoA");

    ENGINE_LOG_INFO(eng::Channels::kProfile, "AoS: min {:.4f} avg {:.4f} max {:.4f} ms",
                    aosStats.minMs, aosStats.AverageMs(), aosStats.maxMs);
    ENGINE_LOG_INFO(eng::Channels::kProfile, "SoA: min {:.4f} avg {:.4f} max {:.4f} ms",
                    soaStats.minMs, soaStats.AverageMs(), soaStats.maxMs);

    if (soaStats.AverageMs() > 0.0) {
        ENGINE_LOG_INFO(eng::Channels::kProfile,
                        "delta: {:.4f} ms ({:.2f}x)",
                        aosStats.AverageMs() - soaStats.AverageMs(),
                        aosStats.AverageMs() / soaStats.AverageMs());
    }

    // The ratio that IS the answer; everything else is commentary.
    //
    // 24 of the 32 bytes per particle are read by the update (six floats);
    // spriteId and flags are the other 8 and are never touched. So a 64-byte
    // line holding two AoS particles carries 48 useful bytes, while an SoA line
    // is 16 consecutive floats the loop reads every one of.
    constexpr usize kUsefulPerElement = 24;
    ENGINE_LOG_INFO(eng::Channels::kProfile,
                    "useful bytes per 64-byte line: AoS {}/64, SoA 64/64",
                    (64 / sizeof(Particle)) * kUsefulPerElement);
    ENGINE_LOG_INFO(eng::Channels::kProfile,
                    "that ratio alone predicts {:.2f}x; the measured figure is larger, "
                    "and follow-up 2 in the report says why",
                    64.0 / static_cast<double>((64 / sizeof(Particle)) * kUsefulPerElement));
}

void RunSizeofAudit() {
    // The engine reporting its own layout. Every struct here is one the engine
    // actually uses; none was written for the exercise.
    ENGINE_LOG_INFO(eng::Channels::kProfile, "{:<26} {:>6} {:>6}", "struct", "sizeof",
                    "align");

    const auto row = [](const char* name, usize size, usize align) {
        ENGINE_LOG_INFO(eng::Channels::kProfile, "{:<26} {:>6} {:>6}", name, size, align);
    };

    row("eng::Vec2", sizeof(eng::Vec2), alignof(eng::Vec2));
    row("eng::Mat3", sizeof(eng::Mat3), alignof(eng::Mat3));
    row("eng::Color", sizeof(eng::Color), alignof(eng::Color));
    row("eng::RawEvent", sizeof(eng::RawEvent), alignof(eng::RawEvent));
    row("eng::TimerStats", sizeof(eng::TimerStats), alignof(eng::TimerStats));
    row("eng::AABB", sizeof(eng::AABB), alignof(eng::AABB));
    row("eng::Circle", sizeof(eng::Circle), alignof(eng::Circle));
    row("eng::Handle<Texture>", sizeof(eng::Handle<eng::Texture>),
        alignof(eng::Handle<eng::Texture>));
    row("eng::Texture", sizeof(eng::Texture), alignof(eng::Texture));
    row("eng::SpriteRecord", sizeof(eng::SpriteRecord), alignof(eng::SpriteRecord));
    row("eng::Message", sizeof(eng::Message), alignof(eng::Message));
    row("eng::LogRecord", sizeof(eng::LogRecord), alignof(eng::LogRecord));
    row("eng::ByteBuffer", sizeof(eng::ByteBuffer), alignof(eng::ByteBuffer));
    row("eng::Transform2D", sizeof(eng::Transform2D), alignof(eng::Transform2D));
    row("bench::Particle", sizeof(Particle), alignof(Particle));
    row("eng DebugDraw command", eng::DebugDraw::CommandSizeBytes(),
        eng::DebugDraw::CommandAlignBytes());

    ENGINE_LOG_INFO(eng::Channels::kProfile, "pointer size on this build: {} bytes",
                    sizeof(void*));

    // ------------------------------------------------------------------
    //  THE REORDERING DEMONSTRATION.
    //
    //  Reordering saved NOTHING on any struct above, and that is the audit's
    //  actual finding rather than a disappointment: the engine already uses
    //  fixed-width types grouped by width, and each of those structs has at
    //  most ONE sub-word member, which can only ever produce one run of
    //  padding no matter where it sits.
    //
    //  So here is the shape that DOES benefit, measured rather than asserted -
    //  alternating small and large members, which is what you get when a
    //  struct grows a field at a time over several weeks.
    // ------------------------------------------------------------------
    struct PaddedNaive {
        eng::u8  flagA;
        eng::u64 handleA;
        eng::u8  flagB;
        eng::u64 handleB;
        eng::u8  flagC;
    };
    struct PaddedSorted {
        eng::u64 handleA;
        eng::u64 handleB;
        eng::u8  flagA;
        eng::u8  flagB;
        eng::u8  flagC;
    };

    ENGINE_LOG_INFO(eng::Channels::kProfile, "");
    ENGINE_LOG_INFO(eng::Channels::kProfile,
                    "reordering demonstration (same five members, two orders):");
    row("  alternating order", sizeof(PaddedNaive), alignof(PaddedNaive));
    row("  widest-first order", sizeof(PaddedSorted), alignof(PaddedSorted));
    ENGINE_LOG_INFO(eng::Channels::kProfile,
                    "  saving: {} bytes per instance ({:.0f}%), which at 10,000 instances "
                    "is {} bytes and {} cache lines of 64",
                    sizeof(PaddedNaive) - sizeof(PaddedSorted),
                    100.0 * static_cast<double>(sizeof(PaddedNaive) - sizeof(PaddedSorted)) /
                        static_cast<double>(sizeof(PaddedNaive)),
                    (sizeof(PaddedNaive) - sizeof(PaddedSorted)) * 10000,
                    ((sizeof(PaddedNaive) - sizeof(PaddedSorted)) * 10000) / 64);
}

} // namespace bench
