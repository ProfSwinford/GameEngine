# Week 6 — Milestone 1 Evidence

---

## 1. Conventions recorded

From the top of `engine/include/engine/math/Mat3.h`:

- **Storage:** **ROW-MAJOR.** `m[row][col]`; `m[0]` is the first row.
- **Vectors treated as:** **ROW vectors.** A point is `[x y 1]` and transforms
  as `v' = v * M`.
- **To apply transform A and then transform B, you write:** **`A * B`.**

This matches Gregory Ch. 5.3, which uses row vectors and row-major storage and
is explicit about it. Matching the book means the worked examples transcribe
directly instead of needing transposing in your head at 1am.

Consequences that follow and that are written into the header, because they are
what people get wrong:

- **Translation lives in the BOTTOM ROW** — `m[2][0]`, `m[2][1]`. (A
  column-vector convention puts it in the right-hand column. If you read engine
  code that puts it there, that engine uses `M*v`.)
- Composition reads **left to right**: `Scale * Rotation * Translation` means
  "scale it, then rotate it, then move it", in that order.
- A child's world matrix is **`local * parentWorld`**. Local first.
- **Rotation is counter-clockwise** for a positive angle in the y-up world.
  `(1,0)` rotated by +90° is `(0,1)`.

From `engine/include/engine/math/Overlap.h`:

- **Touching counts as overlap: YES.**

Two boxes sharing exactly one edge overlap; two circles touching at exactly one
point overlap; a point exactly on a boundary is `Contains`. Chosen because every
comparison then reads as `<=`/`>=` — one relational operator to keep straight
instead of a mixture — and because a trigger volume that fails to fire when the
player is exactly on its edge is a bug report waiting to happen.

`tests/src/test_overlap.cpp` sets `kTouchingCounts = true` and pins the decision
down in **all four shape combinations plus `Contains` on an edge, a corner and a
circle's rim** — three separate places where the same decision has to agree.

---

## 2. Hierarchy

The three-deep hierarchy is `SolarRoot → Planet → Moon` in
`assets/scenes/orbit_test.json`, loaded and rendered by both `sandbox` and
`editor`. The gate scene has a second one (`Beacon → BeaconArm → BeaconTip`) so
the hierarchy is exercised in the Week 10 scene too.

### It really orbits — and for a while it did not

**This section originally claimed the hierarchy orbited, and that claim was
false.** Nothing in the engine mutated a transform per frame: `orbit_test.json`
rendered 22 sprites that sat perfectly still. The milestone was being asserted
on the strength of the transform unit tests, which are real evidence of the
*maths* and are not the check M1 actually asks for. A grep settled it in
seconds — no system, anywhere, touched a transform.

The fix is `SpinComponent` (`engine/include/engine/scene/SpinComponent.h`) plus
a `SpinSystem` at `SystemStage::kMovement`. It adds `radiansPerSecond * dt` to
its own transform's **local** rotation and does nothing else. **There is no
orbit code in the engine**, and that is the point: a child's world matrix is
`local * parentWorld`, so spinning a *parent* sweeps everything under it. Three
numbers in the data file produce a three-deep orbiting system:

```json
{ "type": "SpinComponent", "radiansPerSecond": 0.35 }   // SolarRoot
{ "type": "SpinComponent", "radiansPerSecond": 0.9  }   // Planet
{ "type": "SpinComponent", "radiansPerSecond": -1.6 }   // Moon
```

### Verified numerically, not by eye

`sandbox --motion-check` exists because "it looks like it's orbiting" is exactly
the standard that let the static version pass:

```
depths: SolarRoot 0, Planet 1, Moon 2
frame 0    planet (  160.00,     0.00)  moon (  235.00,     0.00)  |moon-planet| 75.00
+30 frames planet (  157.87,    26.02)  moon (  220.47,    67.33)  |moon-planet| 75.00
+30 frames planet (  150.93,    53.11)  moon (  177.52,   123.23)  |moon-planet| 75.00
+30 frames planet (  139.38,    78.57)  moon (  119.91,   151.00)  |moon-planet| 75.00
+30 frames planet (  123.57,   101.64)  moon (   65.41,   148.99)  |moon-planet| 75.00
+30 frames planet (  103.99,   121.60)  moon (   29.11,   125.97)  |moon-planet| 75.00

planet moved 133.88 units
planet orbit radius 160.000 -> 160.000  (must be unchanged)
moon radius about planet 75.000 -> 75.000  (must be unchanged)
MOTION CHECK: moved=true planet-orbits=true moon-orbits-planet=true
```

The two properties together are what make it an **orbit** rather than a drift:
the planet *moved* 133.88 units, and its distance from the origin did **not**
change — 160.000 at both ends. The moon holds 75.000 from the planet throughout
while its world position sweeps all over. A hierarchy that translated instead of
rotating would pass the first test and fail the second, which is why both are
checked.

The visual check is done in the editor: drag camera position and zoom in the
**Viewport** panel and the orbiting hierarchy stays correct through the pan and
the zoom, because the camera is an inverse transform rather than an offset
subtracted inside the renderer.

**Two bugs hit getting the hierarchy right, and how each was found.**

**The first is the static-scene one above**, and the interesting part is *why*
the unit tests did not catch it. They test `Transform2D` and `Mat3` — given a
parent rotation, is the child's world position right? Yes, always. What no
transform test can observe is that **nothing ever calls the setter**. The tests
covered the maths and the gap was in the wiring, which is the classic shape of a
green suite over a broken feature — the same lesson as Week 2's `Fill` overrun,
arriving from the opposite direction.

**The second:**

The three-deep test case caught a genuine error — in the *test*, which is the
more interesting version.

`test_transform.cpp` asserts the leaf's world position by hand-computation. My
first hand-computation of the grandparent-rotated case said `(-10, 150)`. The
test failed. The instinct was to go and look at `Transform2D::WorldMatrix`.

Working it through on paper against the recorded convention:

- the child sits at `(50, 10)` in the grandparent's own frame;
- a +90° rotation under row vectors sends `(x, y)` to `(-y, x)`, so that becomes
  `(-10, 50)`;
- the grandparent's translation `(100, 0)` puts it at **`(90, 50)`**.

The code was right and my arithmetic was wrong. **The test suite told me first** —
which is exactly the point the lab makes about why the hierarchy cases are
expressed as tests rather than checked by squinting at the screen. Had this been
verified visually, `(-10, 150)` and `(90, 50)` are both "somewhere up and to the
side" and I would have accepted the wrong one.

The thing that made it resolvable in two minutes rather than an afternoon was
that the convention block already existed and was unambiguous. There was a
written-down rule to check the arithmetic against, so the question "is the code
wrong or is my expectation wrong?" had an answer that was not "try flipping a
sign and see".

---

## 3. Test suite

- **Total test cases: 126** (whole suite), **198,689 assertions**
- Cases covering touching edges, containment, and zero-size shapes: **16 in
  `test_overlap.cpp`** — 7 provided (touching edge, touching corner, touching
  circles, full containment in every combination, zero-size shapes, circle near
  a box corner, single-axis separation) and 9 written (the ordinary cases in all
  four combinations, `Contains` on a boundary for both shapes,
  `ClosestPointOnAABB`, the AABB helpers, `Encapsulate`).
- Transform/camera cases: **15 in `test_transform.cpp`**, including the
  three-deep hierarchy, orphan-on-parent-destruction, and the screen↔world round
  trip.

Summary line from a green run:

```
[doctest] test cases:    126 |    126 passed | 0 failed | 0 skipped
[doctest] assertions: 198689 | 198689 passed | 0 failed |
[doctest] Status: SUCCESS!
```

---

## 4. Determinism across machines

Twenty values from seed 12345, via
`sandbox --random-check 12345`.

**Machine A** — x86-64, Windows 11, MSVC 14.51, **Release** build:

```
seed 12345
 0  u32= 571572824  int[1,6]=3
 1  u32= 879680741  int[1,6]=2
 2  u32= 513431484  int[1,6]=2
 3  u32= 756420222  int[1,6]=2
 4  u32=2177033948  int[1,6]=4
 5  u32=1447552346  int[1,6]=2
 6  u32= 526787252  int[1,6]=4
 7  u32=1853100464  int[1,6]=3
 8  u32=2060664889  int[1,6]=5
 9  u32=3725978293  int[1,6]=1
10  u32= 792963392  int[1,6]=3
11  u32=1633098536  int[1,6]=6
12  u32=3286256275  int[1,6]=4
13  u32=3769823981  int[1,6]=4
14  u32=4168207041  int[1,6]=1
15  u32=1682003353  int[1,6]=2
16  u32=3368655886  int[1,6]=3
17  u32=2186826076  int[1,6]=1
18  u32=3484281898  int[1,6]=2
19  u32=1615439941  int[1,6]=2
```

**Machine B** — the same hardware, **Debug** build (`/Od`, different code
generation, different inlining, asserts compiled in):

```
seed 12345
 0  u32= 571572824  int[1,6]=3
 1  u32= 879680741  int[1,6]=2
 2  u32= 513431484  int[1,6]=2
 3  u32= 756420222  int[1,6]=2
   ... identical for all twenty ...
```

**Identical? Yes** — byte for byte.

**This is a weaker check than the milestone asks for and it is recorded as
such.** The milestone wants two *machines*; this is two *builds* on one machine,
because there is no second machine available for this build. What it does rule
out is optimisation-level and inlining differences changing the sequence, which
is a real class of failure. What it cannot rule out is a different standard
library or a different architecture.

**What makes the cross-machine claim defensible anyway** is that the check is
structural rather than empirical. `Random.h` deliberately does **not** use
`std::uniform_int_distribution` or any other standard distribution, because the
standard specifies the generator *engines* exactly but **does not specify the
distributions** — two conforming standard libraries can return different numbers
from the same engine and the same seed, and both are correct. That is precisely
the trap this milestone is designed to catch.

Instead `Random` implements **SplitMix64** by hand — ten lines, no library calls,
fully defined by its own arithmetic — and writes the range mapping itself
(Lemire rejection for `NextInt`, top-24-bits for `NextFloat01`). Every operation
is on `u64` with defined wrapping behaviour. There is nothing in the sequence
that an implementation is free to choose.

`tests/src/test_random.cpp` locks this down further: it asserts the **published
SplitMix64 reference vectors for seed 0**

```
0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, 0x06C45D188009454F
```

so if anyone changes a constant or a shift in `Random.cpp`, the test fails and
names it — rather than the sequence quietly diverging and every "reproduce it
from the seed" claim in the engine becoming void.

---

## 5. Debug draw

- [x] **A shape drawn from a non-rendering function.**
      `CollisionSystem::Update` (`engine/src/physics/Collider.cpp`) debug-draws
      every collider from inside a *physics* update. `SpriteComponent`'s bounds
      and the selection highlight are drawn from `HierarchyPanel::Draw`, which is
      editor code. No renderer pointer is threaded through any of those calls —
      every `DebugDraw` entry point is static and takes no context, which is the
      first of the three requirements.
- [x] **A world-space shape that moves correctly when the camera pans and
      zooms.** The collider boxes and the grid. `DebugDraw::Render` transforms
      world-space commands through `Camera::WorldToScreen`; the radius of a
      circle goes through `WorldToScreenVector` rather than `WorldToScreen`, so
      the translation does not apply to it — getting that wrong makes circles
      drift off their own centres as the camera pans, which is the
      `TransformPoint`-vs-`TransformVector` distinction arriving in practice.
- [x] **A screen-space element that does NOT move when the camera moves.** The
      gate game's HUD (`CollectorGame::DrawHud`) and the sandbox's allocator HUD
      (`DrawAllocatorHud` in `main.cpp`), both `DebugSpace::Screen`.
- [x] **A shape with a multi-second lifetime, drawn once and persisting.**
      `CollectorGame::OnCollected` drops a 3-second yellow circle where a pickup
      was — the event happened once and is over, and the marker is what lets you
      go and look at it. The Debug Draw panel has a button that drops one
      manually.

**Does debug geometry track the hierarchy without a separate code path?**

**Yes.** `DebugDraw::TransformedBox(const Mat3& worldMatrix, ...)` takes a world
matrix **the caller already had** and pushes four corners through it. Collider
drawing calls `WorldBounds()`, which is itself computed from
`Transform2D::WorldMatrix()` — the same matrix the collision test used and the
same one the sprite renderer used.

There is no second position computation anywhere for debug geometry. That is
why Week 10's "debug-draw every collider" was three lines rather than an
afternoon, and it is the thing the question is really asking about.

---

## Stretch goals

- **2. Debug-drawn world grid and origin axes — done.** `DebugDraw::Grid` and
  `DebugDraw::OriginAxes`, on their own toggleable categories, wired to the
  `debug.showGrid` CVar. Used constantly since; "where *is* the origin" is the
  first question every rendering bug asks.
- **1. OBB overlap via separating axes — not done.** Deferred to Phase 2, where
  a game with rotating entities will actually want it. The current answer is
  documented rather than silent: the collision system takes the
  **axis-aligned bounds of the rotated box**, which over-approximates by up to
  41% at 45° and therefore fires collisions slightly *early* rather than late.
  Early is the safe direction. See `engine/include/engine/physics/Collider.h`.
- **3. Hundred-entity stress scene — partially covered.** The 1000-frame stress
  run in Week 10 exercises the naive `WorldMatrix` under load and its numbers
  are in `week10-milestone4.md`. `SpriteRenderSystem::Render` measures
  **0.089 ms average for 21 sprites at 3 deep**, which is the "before" number if
  Phase 2 ever caches world matrices.

---

## 🏁 Milestone 1 — checklist

- [x] A three-deep parented hierarchy **orbits and rotates** correctly —
      `orbit_test.json`, driven by `SpinComponent`, verified numerically by
      `sandbox --motion-check` and pinned down mathematically by
      `test_transform.cpp`. (This was claimed before it was true; see §2.)
- [x] Panning and zooming leave it visually correct — camera built as an
      inverse transform, not a special case in the renderer
- [x] Debug-drawn shapes track their transforms with **no separate code path**
- [x] Overlap suite green including touching-edge, full-containment and
      zero-size cases
- [x] Same seed produces an identical sequence across builds; the *structural*
      argument for cross-machine identity is in section 4, along with what this
      evidence does and does not establish
- [x] `ScreenToWorld` implemented and round-trip tested at four zoom levels and
      three camera positions
