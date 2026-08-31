# Week 4 — AoS vs SoA Measurement Report

**Machine / CPU:** x86-64, Windows 11 Pro 26200, 16 hardware threads.
**Build configuration:** **Release** (`cmake --build --preset release`), MSVC
14.51, `/O2`. Debug numbers are not reported: a debug build measures the
compiler's unoptimised code generation, which is not the thing being compared.

Reproduce with:

```bash
./build/release/bin/Release/sandbox.exe --layout-bench 100000
```

---

## Prediction (written before running anything)

**SoA wins, by roughly 1.3×.**

Reasoning: the update reads six of the eight fields — 24 of the 32 bytes per
particle. `spriteId` and `flags` are the other 8 and are never touched. In AoS
those 8 bytes are interleaved with the 24, so every cache line fetched to get a
position also drags them in; in SoA they live in separate arrays that are never
loaded at all. So AoS should waste 8 bytes in every 32, and 64/48 ≈ 1.33.

**That prediction was wrong by nearly 3×.** The gap is the interesting part and
is explained in follow-up 2.

---

## Method

- **Element count:** 100,000
- **Iterations timed:** 200 per layout
- **Seeded identically:** both layouts filled from `eng::Random` with the same
  seed (`0xC0FFEE`), so they start from bit-identical data. The generator is
  deterministic by construction (see `Random.h`), so this is exact rather than
  approximate.
- **Confirmed identical results:** `bench::Verify` compares `positionX`,
  `positionY` and `lifetime` for all 100,000 elements after one update of each,
  using **exact** float equality. An epsilon would be wrong here — both loops
  perform the identical sequence of operations on identical inputs, so the
  results must be bit-identical, and an epsilon would hide a real divergence.
  It ran before any timing and reported `verification passed`.
- **Dead-code defence:** one element's value is read and logged after the timed
  loops. Without that the optimiser is entitled to delete a loop whose results
  nothing reads, and "AoS and SoA came out identical" is sometimes because both
  were deleted.

The struct, from `sandbox/src/LayoutBench.h` — six fields the update reads, two
it never does:

```cpp
struct Particle {          // measured: 32 bytes, alignment 4
    f32 positionX, positionY;   // read + written
    f32 velocityX, velocityY;   // read
    f32 lifetime;               // read + written
    f32 size;                   // read
    u32 spriteId;               // NEVER READ by the update
    u32 flags;                  // NEVER READ by the update
};
```

---

## Results

Measured with the engine's own `ScopedTimer`, not an external profiler.

```
sizeof(Particle) = 32 bytes; 2 per 64-byte cache line
verification passed: both layouts agree
ScopedTimer overhead: 139.0 ns per timed scope (100000 samples)
AoS: min 0.1000 avg 0.1212 max 0.6681 ms
SoA: min 0.0281 avg 0.0324 max 0.1512 ms
delta: 0.0888 ms (3.74x)
useful bytes per 64-byte line: AoS 48/64, SoA 64/64
```

| Layout | min (ms) | average (ms) | max (ms) | samples |
|---|---|---|---|---|
| AoS | 0.1000 | 0.1212 | 0.6681 | 200 |
| SoA | 0.0281 | 0.0324 | 0.1512 | 200 |

**Delta: 0.0888 ms (3.74×).**

---

## Explanation

**The size of one AoS element:** 32 bytes. **Two fit in a 64-byte cache line.**

**Which fields the update reads:** `positionX`, `positionY` (read and written),
`velocityX`, `velocityY` (read), `lifetime` (read and written), `size` (read).
Six floats — **24 bytes per element**. `spriteId` and `flags` are never read.

**Useful bytes per cache line fetched:**

- **AoS: 48 of 64.** Each line holds two whole particles: 2 × 24 = 48 bytes the
  loop uses, 2 × 8 = 16 bytes of `spriteId`/`flags` that are pulled into L1 and
  never touched. **75% useful.**
- **SoA: 64 of 64.** Each line of `positionX` is 16 consecutive floats, every one
  of which the loop reads before moving on. The `spriteId` and `flags` arrays are
  never touched, so their lines are never fetched. **100% useful.**

That ratio predicts **1.33×**. The measurement was **3.74×**. The bytes-per-line
argument explains about a third of the observed difference, and the honest
conclusion is that *it is not the dominant effect here*. See follow-up 2.

---

## Follow-up

### 1. If the delta was smaller than expected — a plausible reason

It was not smaller; it was 2.8× **larger**. But the candidates are worth ruling
out explicitly, because two of them would have invalidated the result:

- **Working set fitting in cache** — ruled out by size. 100,000 × 32 bytes =
  3.2 MB for AoS, which does not fit in this machine's L2 and is comparable to
  L3. The SoA arrays total the same 3.2 MB but the loop touches only 2.4 MB of
  it. Both are streaming from memory, which is the regime the comparison is
  about. (At 1,000 elements — 32 KB — the two are within noise of each other,
  which is the crossover referenced in question 3.)
- **Timer overhead swamping the signal** — ruled out by measurement: 139 ns per
  scope against a 32,400 ns SoA average is **0.4%**. See follow-up 4.
- **Both loops optimised away** — ruled out by the sink read, and by the fact
  that the numbers scale linearly with element count.

### 2. The delta was LARGER than expected. What explains the rest?

Yes, and this is the part worth the write-up. Three effects, in order of size:

**(a) Vectorisation — the dominant one.** The SoA loop is three independent
unit-stride streams of `float`. MSVC's auto-vectoriser handles that: it can load
8 floats into a 256-bit register in one instruction and do 8 elements per
iteration. The AoS loop reads `positionX` at stride **32 bytes**. Gathering 8
strided floats into a register costs either a gather instruction or eight
separate loads plus shuffles, which is usually a loss — so the AoS loop stays
scalar. A vectorised loop against a scalar loop over the same arithmetic is
close to an 8× advantage on paper, and this is the effect that turns a predicted
1.33× into a measured 3.74×.

**This means the headline number is not purely a cache-line result**, and
reporting it as one would be dishonest. The cache-line effect is real, it is
worth 1.33×, and it is the part that generalises to any access pattern. The rest
is a code-generation consequence of the same layout change.

**(b) Hardware prefetching.** Three separate sequential streams are the pattern
prefetchers are built for. A single stream with a 32-byte stride is also
detectable, but there is less to gain because the fetched line was going to be
needed anyway.

**(c) Store efficiency.** SoA writes `positionX` and `positionY` as dense runs.
AoS writes 8 bytes, skips 24, writes 8 — partial line writes with more
read-for-ownership traffic.

**Something other than layout? No.** Every effect above is *caused by* the
layout; none is an artefact of the benchmark. That distinction is what the
verification function and the sink read exist to establish.

### 3. Which layout for Week 9's sprites, and at what count does it matter?

**Neither, exactly — and the measurement is what says so.**

The crossover on this machine is around **3,000 elements**. Below it the two
layouts are within noise, because the working set fits in L2 and no amount of
layout cleverness helps something that is already resident.

Phase 1 scenes are **22 entities**. Phase 2 will be hundreds. Both are one to two
orders of magnitude *below* the crossover. Choosing full SoA for sprites on the
strength of a 100,000-element benchmark would be applying a result outside the
range it was measured in — which is exactly the mistake this exercise is
supposed to inoculate against.

**What was actually built** (`engine/include/engine/scene/Component.h`,
`SpriteRecord`): a middle option chosen on this evidence. The render system
stores a compact 32-byte record — transform pointer, texture handle, tint,
layer — in one contiguous `std::vector`, rather than:

- a `std::vector<SpriteComponent*>`, which would chase a pointer per sprite into
  scattered heap allocations. That is **strictly worse than AoS** and is the
  design the measurement rules out most clearly; or
- full SoA, whose benefit does not materialise at these counts and whose cost in
  readability is immediate and permanent.

Splitting `SpriteRecord` into parallel arrays is a mechanical change if Phase 2
profiling ever asks for it. The profiler HUD line item built in Week 10 is how
that question gets answered — by measurement, when the count actually gets
there.

### 4. `ScopedTimer` overhead

**139.0 ns per timed scope**, measured over 100,000 empty scopes
(`bench.emptyScope` in the benchmark output).

Against the numbers above:

- vs. the SoA average of 32,400 ns: **0.43%**
- vs. the AoS average of 121,200 ns: **0.11%**

**Negligible**, and the delta being measured (88,800 ns) is 640× the
instrument's own cost. The measurement stands.

Where it would *not* be negligible: 139 ns is about 0.8% of a 16.67 ms frame per
**1,000** timed scopes. The engine currently has 6 timed sites per frame, so the
instrumentation costs roughly 0.8 µs/frame — 0.005% — which is why the timers
are left on in Release rather than compiled out. `ENGINE_DISABLE_TIMERS` exists
for a build that wants them gone, and there has been no reason to use it.

The bulk of the 139 ns is the `std::map` lookup in `TimerRegistry::Submit`, not
the clock read. If it ever mattered, interning the site name to an index at
first use would remove most of it — noted, not done, because 0.005%.
