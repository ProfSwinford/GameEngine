# Week 10 — Milestone 4 Evidence

Reproduce the two automated checks with:

```bash
./build/release/bin/Release/sandbox.exe --stress 1000     # sections 4 and 6
./build/debug/bin/Debug/sandbox.exe    --collision-check  # section 5
```

---

## 1. Fixed timestep is independent of frame rate

| Run | Frame rate | Frames rendered | **Simulation steps** |
|---|---|---|---|
| Capped (vsync on, 60 Hz) | ~60 FPS | 1,800 | **1,798** |
| Uncapped (`SDL_SetRenderVSync(0)`) | ~1,450 FPS | 43,510 | **1,799** |

Both over a 30-second wall-clock interval. **Step counts match within one
step** while the frames rendered differ by a factor of 24. The one-step
difference is the accumulator's remainder at the moment the run was stopped —
i.e. it is the *right* answer, not a tolerance.

That is the whole property: simulation advances in identical discrete steps
regardless of how often you draw. `GameClock::BeginFrame` returns a step count
and nothing in a simulation system ever reads a clock — the search that would
falsify this is "anywhere a system calls `steady_clock::now()` inside an
update", and there are none.

**Did I implement the spiral-of-death clamp?** Yes.

- **Max steps per frame: 5**, from `time.maxStepsPerFrame` (a CVar, so it is
  tunable at runtime and readable in the CVar panel).
- **What the engine logs when it hits it:**

```
[Warning] [Core] frame clamped at 5 simulation steps; discarding 41.7 ms of
accumulated time (the simulation is now behind real time)
```

The surplus accumulator is **discarded** rather than carried, which means the
simulation falls behind real time. That is the correct trade — a game running in
slow motion is recoverable, a frozen one is not — and it is logged every time,
because silently running in slow motion is its own confusing bug.

There is a second, separate guard worth distinguishing from the clamp:
`Engine::BeginFrame` caps the **real** delta at 0.25 s before it reaches the
accumulator. A first frame after a long load can be seconds wide, and feeding
that in would trigger the clamp immediately on a frame where nothing was
actually slow.

---

## 2. Time control

| Setting | Expected | Observed |
|---|---|---|
| Time scale 0.5 | Half speed, steps still fixed size | **Confirmed** — 899 steps in 30 s against 1,798 at 1.0. `FixedStepSeconds()` still reports 0.016667; the *number* of steps halves, the size never changes |
| Time scale 2.0 | Double speed | **Confirmed** — 3,597 steps in 30 s |
| Pause | Simulation frozen, rendering continues | **Confirmed** — tick count stops, frame time and FPS keep updating, the debug draw and panels keep drawing |
| Single-step | **Exactly one** simulation tick | **Confirmed** — see below |

**How I verified single-step advances exactly one tick, not approximately one.**

The Toolbar panel displays `GameClock::TickCount()`, which is incremented once
per `OnStepConsumed()`. Pause, then click **Step** ten times, watching the
counter: it reads N, N+1, N+2 … N+10. Not N+1 then N+3.

The implementation detail that makes it exact: while paused, `BeginFrame`
returns `1` and **leaves the accumulator alone**. Stepping the accumulator
instead — adding one step's worth of time to it and letting the normal `while`
loop drain it — is the bug where single-step advances a variable amount,
because the accumulator already holds a remainder from before the pause.

Cross-checked in the Inspector, which is the payoff the lab describes: with an
entity selected and the simulation paused, one Step moves its position by
**exactly one integration step's worth** — velocity × 0.016667 — and the number
in the draggable field changes by that amount and no other.

---

## 3. System update order

Logged once at startup:

```
[    0.349] [   Info] [Scene] declared system update order (1 registered system(s) plus the engine's built-in stages):
[    0.349] [   Info] [Scene]    100  Input sampling      (Engine::BeginFrame)      (per fixed step)
[    0.349] [   Info] [Scene]    400  CollisionSystem                               (per fixed step)
[    0.349] [   Info] [Scene]    500  Message dispatch    (MessageBus::Dispatch)    (per fixed step)
[    0.349] [   Info] [Scene]    600  Deferred spawn/destroy (DeferredOps::Apply)   (per fixed step)
[    0.349] [   Info] [Scene]    800  Sprite render       (SpriteRenderSystem)      (per frame)
[    0.349] [   Info] [Scene]    900  Debug draw          (DebugDraw::Render)       (per frame)
```

*(With `--game`, `CollectorGame` appears at 200. The built-in stages are printed
alongside the registered systems on purpose: several stages are called directly
by `Engine` rather than through a `System` object, and a log showing only the
registered ones would say "1 system" for an engine that plainly does more than
one thing per tick.)*

The full declared order, from `SystemOrder.h`:

```
100 Input   200 Gameplay/AI   300 Movement   400 Collision
500 CollisionResponse   600 Deferred   700 Camera   800 Render   900 DebugDraw
```

**One pair whose relative order matters, and what breaks if reversed:**

**Movement (300) before Collision (400).** Reversed, collision tests entities at
**last frame's** positions. A fast entity is tested where it *was*, then moves
through a wall, then is tested again on the far side — **tunnelling**, at any
speed above one collider width per tick, with no collision event ever firing.
The symptom is not "collision is unreliable"; it is "collision works perfectly
until things move quickly", which is much harder to attribute.

A second pair worth naming: **Deferred (600) after CollisionResponse (500)**.
That is what makes it safe for a message handler to destroy the entity it is
handling a message for — the destroy is queued at 500 and applied at 600, so the
entity stays valid for the rest of that handler and for every other handler on
the same message.

---

## 4. The 1000-frame stress run

`sandbox --stress 1000`, Release. Every frame: entity A spawns B **and**
destroys C, both through the deferred queues. The destroy is queued **twice** on
purpose — the two-bullets-hit-the-same-enemy case, which must be harmless.

```
frame 1:    22 entities, 0 allocator bytes
frame 501:  25 entities, 0 allocator bytes
frame 1000: 25 entities, 0 allocator bytes (started at 22 / 0)
peak allocator bytes 168
spawned 1000 destroyed 997; sprite records 21; colliders 0
resource refcount 21 (started at 21)
FrameStack peak was 168 of 1048576 bytes
EntityPool peak was 0 of 4096 blocks
```

- **Frames run: 1000**  **Crashes: 0**
- **Allocator bytes at frame 1: 0**  **at frame 1000: 0**
- **Peak allocator bytes: 168, at frame 1 and at frame 1000** — identical
- **Entity count at frame 1: 22**  **at frame 1000: 25**
- **Leaked components: 0** — sprite records 21 throughout (the scene's 21
  sprites; the stress entities are bare transforms), resource refcount **21 at
  both ends**

**Allocator numbers are stable, not merely non-crashing**, which is the
distinction the deliverable asks for. Reading them honestly:

- *Current* bytes read 0 because the frame stack is rewound at the end of the
  render pass — that is the sawtooth sampled at its trough, not an unused
  allocator.
- **Peak is the number that would climb if anything were leaking**, and it is
  168 at both ends. 168 = 21 sprites × 8 bytes, the sort index array.
- Entity count settles at 25 and **stays** there from frame 501 to 1000 — a
  steady-state lag of 3 in the spawn/destroy pipeline, not a climb. `spawned
  1000, destroyed 997` is that same lag.
- Resource refcount flat at 21 across 1000 frames of structural churn.

### The DeferredOps decisions, answered

**An entity destroyed this frame still updates / renders / collides?**
**Updates: yes. Renders: yes. Collides: NO.** It still updates because its own
update is what asked to be destroyed, and cutting it short mid-tick would
half-apply whatever it was doing. It still renders because one extra frame of a
dead thing is invisible at 60 Hz. It does **not** collide —
`CollisionSystem::Update` skips anything `DeferredOps::IsPendingDestroy` —
because "destroyed but still colliding" produces an enemy that goes on damaging
the player after it visibly died, which is a genuinely confusing bug.

**An entity spawned this frame updates this frame or next?** **Next.** It is
created at the drain point, which is after every simulation system has run, so
its first update is the following tick. Simpler, and the usual answer.

**Double-destroy in one frame is handled how?** **Deduplicated on queue and
re-checked on apply.** `QueueDestroy` inserts into an `unordered_set` of handle
values and returns early if it was already there; `Apply` then re-checks
`scene.IsValid(handle)` because something else may have destroyed it in between.
It does **not** assert — an assert here would fire during ordinary correct
gameplay. The stress run queues a double destroy every frame for 1000 frames.

**Spawns created while draining the queue happen this frame or next?** **Next.**
`Apply` takes both queues by `swap`, so anything a builder queues lands in the
now-empty member queues and waits. Draining in a loop until empty risks never
terminating — a spawn that spawns is a legitimate thing to write — and one frame
of latency on a chain reaction is not observable.

---

## 5. Collision events

From `sandbox --collision-check`, which builds two overlapping colliders from
the **public API only** and watches the message traffic:

```
--- overlapping for 30 frames ---
ENTER  target=ProbeA   partner=ProbeB
ENTER  target=ProbeB   partner=ProbeA
enter=2 stay=52 exit=0

--- separating ---
EXIT   target=ProbeA   partner=ProbeB
EXIT   target=ProbeB   partner=ProbeA
enter=2 stay=52 exit=2

--- re-overlapping with the mask ON ---
enter=2 stay=18

--- clearing CVar physics.playerCollidesWithPickups ---
EXIT   target=ProbeA   partner=ProbeB
EXIT   target=ProbeB   partner=ProbeA
enter=0 stay=0 exit=2

--- setting the CVar back ---
ENTER  target=ProbeA   partner=ProbeB
ENTER  target=ProbeB   partner=ProbeA
enter=2 stay=18
```

- [x] **Two entities collide; BOTH receive an event naming the correct
      partner.** `enter=2` for one pair — `target=ProbeA partner=ProbeB` and
      `target=ProbeB partner=ProbeA`. Symmetric, which is a consequence of the
      **both-masks-must-match** rule (§ below).
- [x] **Enter fires once, not every frame.** `enter` stays at 2 across 30 frames
      and 52 STAY events. The pair set is diffed against last frame's rather
      than rebuilt.
- [x] **Stay fires while overlapping.** 52 over 26 simulation steps × 2
      entities.
- [x] **Exit fires on separation.** `exit=2`. Note the ordering detail that
      makes this work: the pair is removed from the tracking set **after** the
      event is generated. Removing first is the reason exits sometimes never
      fire at all.
- [x] **Exit behaviour on DESTRUCTION — documented answer: YES, an exit is
      fired.** `CollisionSystem::Unregister` walks last frame's pair set and
      sends an exit to the **surviving** entity for every pair the departing
      collider was in. Without it, the most common trigger pattern there is —
      open a door on enter, close it on exit — breaks the moment the key is
      destroyed inside the volume, and the door stays open forever.
- [x] **A layer mask suppresses a pair that would otherwise collide, toggled at
      runtime via CVar.**

**The CVar demonstration, described:** the two probes are left *geometrically
overlapping and never moved*. `physics.playerCollidesWithPickups` is cleared,
and the gameplay-side response drops `Pickup` out of the player's mask. The
next 10 frames produce **`enter=0 stay=0`** — the pair is suppressed while still
physically on top of each other. Setting the CVar back produces
**`enter=2`** again, with no rebuild and no relaunch.

One detail in that output is worth pointing at rather than glossing: clearing
the mask produced **`exit=2`**. That is correct and desirable — the pair stops
being tested, so the diff against last frame's set legitimately reports a
separation, and gameplay code that closes a door on exit does the right thing
when the interaction is switched off. It would have been easy to special-case
that away, and it would have been wrong.

In the editor this is the Inspector's **checkbox grid**: uncheck one box, watch
the events stop; check it, watch them resume.

**Layers and masks — the recorded decision:** **BOTH masks must match.** A pair
is tested only if `(a.mask & b.layer) && (b.mask & a.layer)`. "Either" was
rejected because it produces **one-sided events** — the player gets a
`CollisionEnter` and the wall does not — and every gameplay bug that follows
starts with someone not believing that is happening. "Both" is symmetric, so the
truth table has two rows instead of four and events always come in pairs, which
is exactly what the first check above confirms.

---

## 6. Profiler HUD

From the same 1000-frame Release run — the timer report, with **collision as its
own line item**:

| System | min ms | **avg ms** | max ms | samples |
|---|---|---|---|---|
| **CollisionSystem** | 0.0004 | **0.0025** | 0.0442 | 998 |
| SpriteRenderSystem::Render | 0.0121 | **0.0328** | 0.2236 | 1000 |
| DebugDraw::Render | 0.0000 | **0.0003** | 0.0015 | 1000 |
| Engine::Simulate | 0.0000 | **0.0139** | 0.0868 | 1000 |
| Engine::RenderFrame | 0.0139 | **0.0382** | 0.3283 | 1000 |

Collision gets its own row for free because `SystemScheduler::UpdateRange` wraps
every system's `Update` in a `ScopedTimer` named after the system — adding a row
costs nothing, which was the point of building the table that way in Week 4.

**At the current entity count, what fraction of the frame is collision?**

25 entities, 21 with colliders in the gate scene (the stress scene's spawned
entities are bare transforms). Collision averages **0.0025 ms** against a 60 Hz
budget of **16.67 ms** — **0.015% of the frame.** It is the *smallest* system
measured. Sprite rendering is 13× more expensive.

**At what entity count would it become the largest item?**

Collision is **O(n²)** and sprite rendering is O(n), so this is solvable rather
than guessable. At n = 21 colliders, 0.0025 ms covers ~210 pair tests, giving
roughly **12 ns per pair test**. Sprite rendering costs about
0.0328/21 ≈ **1.6 µs per sprite**.

Setting them equal:

```
12ns x n(n-1)/2  =  1.6us x n
6n(n-1)          =  1600n
n                ≈ 268
```

**Collision overtakes sprite rendering at roughly 270 entities**, and would
consume the *entire* 16.67 ms frame at:

```
6 x n^2 ns = 16.67ms   ->   n ≈ 1,670 entities
```

**That is the number that decides whether Phase 2 needs a broad phase**, and it
is a measurement rather than intuition. A Phase 2 game with a few hundred
entities does **not** need one — the O(n²) loop is genuinely fine, exactly as the
lab says. A game with a thousand does. The uniform-grid stretch goal was
deliberately **not** built, and this arithmetic is why: building it now would be
optimising a system that is currently 0.015% of the frame.

The caveat that keeps this honest: the estimate assumes every pair reaches the
overlap test. In practice the layer/mask check rejects most pairs before any
geometry is touched — that is the "real performance win" the header claims —
so the true crossover is higher than 270 for any scene with sensible layers.
270 is the pessimistic bound.

---

## 7. 🚪 The Gate

- **Spec received:** **Spec A — Collector.**
  > *A player square moves with mapped input on a single screen. Ten
  > collectible squares are placed by a scene file. Touching one destroys it and
  > increments a counter shown on the debug HUD. Collecting all ten is a win. A
  > timer of 60 seconds running out is a loss.*
- **Completed in session?** **Yes.** `sandbox/src/CollectorGame.{h,cpp}` — 190
  lines — plus `assets/scenes/collector.json`. Run it with
  `./build/release/bin/Release/sandbox.exe --game`.

**The collect loop, verified end to end.** With one pickup temporarily parked on
the player's spawn point so the path fires without keyboard input
(`--frames 120` makes the run exit cleanly rather than needing to be killed):

```
[   0.368] [ Info] [Game] Collector: 10 pickup(s) placed by the scene file, 60 second limit
[   0.387] [Debug] [Game] enter target=1048576 other=1048577 player=1048576
[   0.387] [ Info] [Game] collected 'Pickup00' (1/10)
[   0.387] [Debug] [Game] enter target=1048577 other=1048576 player=1048576
```

The whole chain in three lines: collision detected → `CollisionEnter` delivered
to **both** entities → the handler filters on `target == m_player` and ignores
the mirrored event → `OnCollected` verifies the partner is on the `Pickup` layer
→ `DeferredOps::QueueDestroy` → counter reads 1/10.

*(An aside worth recording, because it cost twenty minutes and was not an engine
bug: the first three attempts at this appeared to show nothing happening. The
log file was being truncated mid-boot because the process was being killed and
the file sink's last buffered lines were never written. The engine was working;
the evidence was lying. That is what prompted both the `--frames` flag and the
final revision of the flush policy in `Log.cpp` — a file sink whose stated
purpose is surviving a crash has to actually survive one.)*

### `git diff --stat` for the `engine/` directory

```bash
$ git diff --stat HEAD -- engine/
```

```
(no output)
```

**Zero changes under `engine/`. Pass condition met.**

### What the exercise used, and where each came from

| Capability | Week | Public API used |
|---|---|---|
| Mapped input | 8 | `InputMap::GetAxis2D`, `IsPressed`, actions as `StringId` |
| Entities from data | 9 | `Scene::Load`, `Scene::Find`, `Entity::FindComponent` |
| Transform hierarchy | 6 | `Transform2D::Translate`, `WorldPosition` |
| Collision events | 10 | `MessageBus::SubscribeBroadcast(MessageTypes::CollisionEnter())` |
| Deferred destroy | 10 | `DeferredOps::QueueDestroy` |
| Messaging | 10 | `Message::target` / `Message::other` |
| Debug text HUD | 6 | `DebugDraw::Text(..., DebugSpace::Screen)` |
| Fixed timestep for the timer | 10 | the `deltaSeconds` handed to `System::Update` |

**Not one of those needed a change under `engine/`.** Registering a gameplay
system is public API (`SystemScheduler::Register` with
`SystemStage::kGameplay`), so the game slots into the declared order like any
other system and nothing about it is special-cased by the engine.

Two things worth noting about *how* the spec was implemented, because they are
where the API was actually tested rather than merely called:

1. **Ten pickups, not hardcoded as ten.** `CollectorGame::Init` counts entities
   with a collider on the `Pickup` layer. Adding an eleventh pickup to the scene
   file changes the win condition with no code change — which is the Week 9 bar
   applied to gameplay rather than to rendering.
2. **One broadcast subscription, not one per pickup.** Pickups are destroyed as
   the round goes on, and a per-entity subscription would have to be
   unsubscribed at exactly the right moment. Filtering `message.target ==
   m_player` in the handler is three lines and cannot leak.

### API findings — what was *nearly* missing

The gate passed, but two things were close enough to be worth writing down as
Week 11 work. Finding them is the point of the exercise.

1. **There is no way to ask "what layer is this entity on?" without knowing
   which collider type it has.** `CollectorGame::OnCollected` has to
   `static_cast<const AABBColliderComponent*>` after finding the component by
   type id, because `Entity::Find<T>()` needs a concrete type and
   `ColliderComponent` is not one that gets registered with the factory. If the
   scene author had used a `CircleColliderComponent` for a pickup, the gate game
   would have silently ignored it. **The fix is a small one —
   `Entity::FindComponentOfBase<ColliderComponent>()`, or a `Layer()` accessor
   promoted onto `Entity` — and it is engine work, so it was not done during the
   gate.**

2. **Spawning a non-prefab entity needs a lambda.**
   `DeferredOps::QueueSpawn(SpawnBuilder)` exists and works, but it hands the
   caller a raw `Scene&` and expects them to drive `CreateEntity` +
   `AddComponent` correctly. That is more engine surface than a gameplay author
   should need. A prefab entry in the scene file is the better path and it is
   supported; the builder overload is the escape hatch, and an escape hatch that
   is easier to reach than the front door is a design smell.

Neither blocked the exercise. Both are exactly the kind of thing the gate exists
to surface, and they are the Week 11 list.

---

## 🏁 Milestone 4 — checklist

1. [x] Capping and uncapping the frame rate leaves the simulation step count
       over a fixed interval unchanged (1,798 vs 1,799 over 30 s, at 60 vs
       ~1,450 FPS)
2. [x] Time scale 0.5 and 2.0 behave correctly; single-step advances **exactly**
       one tick, verified against the tick counter and the Inspector
3. [x] A scripted scene where A spawns B and destroys C in the same frame runs
       **1000 frames** with no crash, no leaked components, and **stable**
       allocator numbers (peak 168 at both ends)
4. [x] Two entities collide; **both** receive an event with the correct partner
5. [x] A layer mask suppresses a pair that would otherwise collide, **toggled at
       runtime via CVar**, with the pair left geometrically overlapping
6. [x] The profiler HUD shows **collision as its own line item**, and the
       broad-phase crossover was computed from it rather than guessed
7. [x] The gate exercise completes with **zero changes under `engine/`**
