# Week 9 — Milestone 3 Evidence

All evidence here is a paste from `sandbox --m3-check`, which runs the three
checks headlessly so they can be re-run after any change:

```bash
./build/debug/bin/Debug/sandbox.exe --m3-check
```

---

## 1. Zero hardcoded content

Searching the entire `engine/` and `sandbox/` source for the name of any entity
in the scene, any position value from it, and any texture filename.

```bash
$ grep -rn "SolarRoot\|OrientationMarker\|Asteroid" engine/ sandbox/ editor/
(no output)

$ grep -rn "checker_red\|checker_blue\|checker_green\|marker_up" engine/ sandbox/ editor/
(no output)

$ grep -rn "319.5\|116.29\|334.83\|294.45" engine/ sandbox/ editor/
(no output)

$ grep -rn "orbit_test" engine/ sandbox/ editor/ config/
config/engine.json:31:    "scene": "scenes/orbit_test.json"
```

**One hit, and it is in the config file, not the C++.** Even the *startup scene
name* is data: `BootConfig::startupScene` defaults to a virtual path that
`config/engine.json` overrides, so there is no scene name compiled into the
engine or the sandbox either.

The only texture path anywhere in C++ is `"textures/missing.bmp"` in
`ResourceManager::MissingTexture()` — the magenta placeholder, which is engine
infrastructure rather than scene content.

**The honest version of the test: could you hand the scene file to someone who
cannot write C++, have them add three entities, and have it work?**

**Yes.** Concretely, adding this block to `orbit_test.json` and relaunching
produces three new rendered, parented, collidable entities with no rebuild:

```json
{ "name": "Comet", "parent": "Planet",
  "components": [
    { "type": "TransformComponent", "position": [80, 40], "scale": [0.4, 0.4] },
    { "type": "SpriteComponent", "texture": "textures/checker_green.bmp", "layer": 2 },
    { "type": "AABBColliderComponent", "halfExtents": [6, 6], "layer": "World", "mask": "Player" }
  ]
}
```

Nothing in that is a C++ concept the author has to know. `ComponentFactory`
turns `"SpriteComponent"` into an object; `Deserialize` reads every field;
`ResolveParents` runs as a **second pass** so `"parent": "Planet"` works whether
Planet appears above or below — the file is not order-sensitive, which would
otherwise be a rule the author had to learn the hard way.

**The gate scene is the same test applied to gameplay.** `CollectorGame` counts
pickups by looking for colliders on the `Pickup` layer rather than assuming
there are ten, so adding an eleventh pickup to `collector.json` changes the win
condition with no code change.

---

## 2. Path independence

| | Machine A | Machine B |
|---|---|---|
| Path to repo | `C:\dev\Game Engine\GameEngineRepo` | *(not available — see below)* |
| Asset root resolved to | `C:\dev\Game Engine\GameEngineRepo` | |
| Scene loaded? | **Yes** | |

The boot line, logged **at Info, every boot**, as `FileSystem.h` requires:

```
[    0.003] [   Info] [FileSystem  ] (t:2c13) asset root resolved to 'C:\dev\Game Engine\GameEngineRepo'
[    0.003] [   Info] [FileSystem  ] (t:2c13) (search started at 'C:\dev\Game Engine\GameEngineRepo\build\debug\bin\Debug\')
```

The second line is there because "where did it start looking" is the first
follow-up question whenever the first line is wrong.

**Only one machine was available**, so the two-machine column is empty rather
than invented. What *was* verified, and what it does and does not establish:

- The executable lives in `build/debug/bin/Debug/` and the assets live in
  `assets/`. Those are **four directory levels apart**, and the resolved root is
  correct — so the walk-upward search works rather than a relative path
  happening to line up.
- The same binary was run from **three different working directories** (the repo
  root, `build/debug/bin/Debug/`, and `C:\`) and resolved the same root every
  time. That is the specific thing the design is for: `SDL_GetBasePath` gives the
  **executable's own location**, and the CWD differs between running from an IDE,
  a terminal, and a double-click — which the header calls a classic half-day of
  confusion.
- The Release build resolves the same root from `build/release/bin/Release/`, a
  differently-named path at the same depth.

What it does not establish is a different drive letter or a different OS. The
mechanism that would carry it there — no absolute path in source, no reliance on
the CWD, a marker-directory search that counts nothing — is in
`FileSystem::Init` and is 20 lines.

---

## 3. Refcounting

With the example scene loaded:

```
--- scene loaded ---
virtual path                        refs      size    state
textures/checker_blue.bmp              1   32x32      Ready
textures/checker_green.bmp             1   32x32      Ready
textures/marker_up.bmp                 1   32x32      Ready
textures/checker_red.bmp              18   32x32      Ready
loaded 4 | TOTAL REFCOUNT 21 | 16384 bytes resident
```

- **Entities: 22**  **Distinct textures loaded: 4**  **Total refcount: 21**

**`checker_red.bmp` reads exactly 18** — the eighteen asteroids sharing one
texture, which is what the provided scene is built to check. One texture object,
eighteen references, not eighteen copies. The lookup is keyed on the
**`StringId` of the virtual path**, which is where Week 8 pays off.

(21 = 18 + 3, and 22 entities because `SolarRoot` is a bare transform with no
sprite.)

After `Unload()`:

```
--- after Unload() ---
virtual path                        refs      size    state
loaded 0 | TOTAL REFCOUNT 0 | 0 bytes resident
```

- **Loaded count: 0**  **TOTAL REFCOUNT: 0**  **Bytes resident: 0**

**One design change was needed to make that a genuine zero**, and it is worth
recording because the first version read `1`. The magenta `missing.bmp`
placeholder was originally acquired eagerly in `ResourceManager::Init`, so the
engine held a real reference for the whole session and the total after unload
was 1. That is not wrong — it is an honest count of a real reference — but the
milestone is read live off a panel, where 1 and 0 mean very different things to
whoever is checking. `MissingTexture()` is now **lazy**: acquired on the first
*failed* load. In a healthy run the count genuinely reaches zero, and when it
does not, the placeholder appearing in the list is itself the diagnostic.

The chain that makes the zero happen: `Scene::Unload` destroys every entity →
`Entity::DestroyInternal` calls `OnDetach` on every component in **reverse
attach order** → `SpriteComponent::OnDetach` deregisters from the render system
and calls `Release`. One `Acquire` in `OnAttach`, one `Release` in `OnDetach`,
both a single line you can point at.

**A real bug this caught during Week 9**, since the number was wrong once for a
better reason: the first version attached components and *then* deserialised
them, so `OnAttach` ran while `m_texturePath` was still empty and acquired
nothing. The symptom was `1 texture resident, total refcount 1` and a scene that
rendered nothing — the "scene loads but nothing appears" entry in the lab's
troubleshooting list, arrived at from the resource side. `Scene::CreateEntityFromNode`
now creates the component, deserialises it, and attaches it last.

---

## 4. Stale handle detection

Acquire a texture, release it to zero so the slot is recycled, then use the
handle we kept:

```
--- stale handle ---
acquired: index 3 generation 2, valid=true
released. now dereferencing the handle we kept:
stale texture handle: index 3 generation 2 (slot is at generation 3, free) - returning null
Get() returned nullptr - the engine is still running
```

- [x] **It was detected as stale** — generation 2 against a slot now at
      generation 3
- [x] **It was reported, naming the index and the generation** — both the
      handle's and the slot's, plus whether the slot is free or has been reused
      by a newer asset, which is the difference between "this is gone" and
      "this is now something else"
- [x] **It was not dereferenced** — `Get()` returned `nullptr`
- [x] **The engine kept running** — no assert, no crash. A missing texture is a
      magenta square, not a crash, so this deliberately does **not**
      assert-and-die in release

The mechanism is fifteen lines: a 32-bit handle packs a 20-bit index and a
12-bit generation; the slot's generation is incremented on free; a handle is
valid iff the two match. O(1), no per-handle bookkeeping, nothing to scan.

**The wrap risk is documented rather than ignored** (`Handle.h`): 12 generation
bits means a slot may be reused 4,095 times before the counter wraps and a very
old handle starts looking valid again. At a worst case of one create/destroy per
slot per frame at 60 Hz, a specific slot wraps after about 68 seconds — not
hypothetical for a bullet pool. What makes it survivable here is that
`Scene`'s free list is **FIFO**, so reuse spreads across the whole slot array
rather than hammering whichever slot was freed last. It is the first thing to
widen if Phase 2 grows a bullet hell, and moving to a 64-bit handle with a 32/32
split makes the problem disappear for four bytes per reference.

---

## 5. Async loading

- **Does the async path route through the Week 5 queue? Where, specifically:**
  Yes. `FileSystem::ReadFileAsync` calls `JobSystem::Enqueue`, which pushes onto
  `ThreadSafeQueue<Job>`; a worker pops it in `WorkerMain` and performs the read.
  `ResourceManager::AcquireTextureAsync` is the caller.
- **Which thread does the completion callback run on:** **The main thread.**
  Not the worker. Finished reads are pushed onto a *second* `ThreadSafeQueue`
  (`g_completions`) and `FileSystem::PumpCompletions` drains it from
  `Engine::BeginFrame`, firing every callback from there.
- **How I verified there is no data race:** by construction, and the argument is
  the design rather than a tool run. The obvious version runs the callback on the
  worker, and the callback then touches the renderer (creating a texture) or the
  resource manager's slot vector. Both are main-thread-only —
  `SDL_CreateTextureFromSurface` is not thread-safe, and `g_slots` is a
  `std::vector` that reallocates. Deferring costs at most one frame of latency on
  something that was already asynchronous. The worker's lambda touches **only**
  the local path string and the byte vector it fills, then pushes; that is the
  whole of its shared-state contact, and it is a queue designed for it.

**Thread sanitizer: NOT RUN, and this is the honest statement the section asks
for.** MSVC has no TSan, and this build is MSVC-only; the `tsan` preset is
committed but guarded to non-Windows hosts.

The async path is the single most likely place in the whole engine for a race,
and Phase 2 will exercise it far harder than this week does. So: the design is
*argued* to be race-free above, and it is *not verified* by a sanitizer on this
machine. The mitigations actually in place are that the two queues are the
Week 5 queue, which the provided stress suite exercises repeatedly, and that the
worker-thread lambda's shared-state surface is deliberately one push.

**A scope note:** the engine currently uses the **synchronous** `AcquireTexture`
for scene loading. `AcquireTextureAsync` is implemented, routed and reachable,
but the scene loader does not call it, because a 3 KB BMP loads in under a
millisecond and making the startup path asynchronous would add a "sprite is
valid but not ready yet" state to the render path for no measured benefit. The
third `ResourceState::Loading` state exists precisely so that path *can* be
taken when Phase 2 has assets worth streaming. Said plainly rather than implied.

---

## 6. The design questions

### 1. One thing that would make the C# binding hard, and what I would change

**`Component` is an abstract C++ base class with virtual functions, and a C#
component would have to inherit from it — which it cannot.**

The rest of the model is already binding-friendly and was designed that way:
components are found by **`StringId`**, created by **name** through
`ComponentFactory`, and entities are referred to by **handle** (an integer, which
survives a GC compaction, unlike a pointer). A C# caller can say "give me the
TransformComponent of entity 7" today, because that sentence contains no C++
type.

What it cannot do is *be* a component. `ComponentFactory::CreateFn` is
`std::unique_ptr<Component>(*)()` — a raw C++ function pointer returning a C++
object — so every component type must be a C++ class known at compile time. A
C# `HealthComponent` has no way in.

**What I would change:** make the factory able to produce a **proxy** component.
Add a `ScriptComponent` that implements the C++ interface and forwards
`TypeId`/`Deserialize`/`OnAttach`/`OnDetach` across the boundary to a managed
object it holds by handle, and let `ComponentFactory::Register` accept a
`std::function` rather than a raw function pointer so the registration can carry
that managed handle. Then `"type": "HealthComponent"` in a scene file resolves
to a proxy wrapping a C# class, and no other engine code learns anything.

The reason that change is *cheap* is the thing worth noticing: every system
already talks to components through the abstract interface and a string id.
Nothing does `dynamic_cast<SpriteComponent*>` to decide what to do — the sprite
system knows its components because they **registered themselves with it**.
That property, which was built for cache locality, turns out to be what makes
the language boundary tractable.

### 2. Am I storing components in the layout my own Week 4 numbers recommended?

**Yes, and the honest answer includes why it is not full SoA.**

Week 4 measured SoA at **3.74× faster than AoS at 100,000 elements**, with a
crossover around **3,000** — below which the two are within noise because the
working set fits in L2.

Phase 1 scenes are **22 entities**; Phase 2 will be hundreds. Both are one to
two orders of magnitude *below* the crossover. Choosing full SoA on the strength
of a 100,000-element benchmark would be applying a result outside the range it
was measured in, which is the mistake the exercise exists to inoculate against.

So `SpriteRenderSystem` stores a **dense `std::vector<SpriteRecord>`** — a
compact 32-byte record of transform pointer, texture handle, tint and layer —
rather than:

- a `std::vector<SpriteComponent*>`, which chases a pointer per sprite into
  scattered heap allocations. That is **strictly worse than AoS** and is what the
  measurement rules out most clearly. It is also the design you get by default
  if you do not think about it.
- full SoA, whose benefit does not materialise at these counts and whose cost in
  readability is immediate and permanent.

The sort is done on an **index array** rather than by moving records, because
moving a record would invalidate every owner's cached `m_recordIndex`. That
index array comes from the **frame stack allocator** (Week 7), which is where
the 168-byte sawtooth on the Memory panel comes from.

Splitting `SpriteRecord` into parallel arrays is mechanical if Phase 2
profiling asks for it. The profiler HUD line item is how that question gets
answered — by measurement, when the count actually gets there, not now.

### 3. What happens to my arrays if a component is removed during iteration?

**It is a real problem, and I know exactly what it looks like.**

`SpriteRenderSystem::Unregister` does a **swap-and-pop**: the record at the back
is moved into the freed slot and the vector shrinks. If that happens while
`Render` is walking the array, the record that was at the back moves into a slot
the loop **has already passed**, so one sprite is drawn twice and another is
skipped. That is the mild version — the same pattern in `CollisionSystem`, which
holds `ColliderComponent*`, would leave the loop reading a pointer to a
destroyed object.

Two things stop it, and they are different mechanisms:

- **Structurally: `DeferredOps`.** Nothing structural happens during a system
  update. Destroys are queued and applied at **one defined point** — stage 600,
  after every simulation system and after message dispatch. That is Week 10's
  answer and it is the one that matters.
- **Locally: the fixup line.** `Unregister` corrects the moved record's owner's
  cached index:
  ```cpp
  g_sprites[index] = g_sprites[last];
  g_sprites[index].owner->m_recordIndex = index;
  ```
  Forgetting that second line means the *next* `Unregister` removes the wrong
  sprite — which presents as a random sprite vanishing when a different one is
  destroyed, and is a genuinely horrible afternoon.

The collision system additionally skips anything `DeferredOps::IsPendingDestroy`,
so an entity destroyed this frame stops colliding immediately even though it
still exists for the rest of the frame. "Destroyed but still colliding" produces
an enemy that goes on damaging the player after it visibly died.

---

## Stretch goals

- **1. Hot reload — not done.** The most valuable of the three and the honest
  gap. `SpriteComponent::SetTexture` already does the acquire-then-release dance
  correctly (acquire the new one *before* releasing the old, so re-setting the
  same path is not a load-unload-reload hitch), so the component-side work is
  there; what is missing is a file watcher.
- **2. Serialize a live scene back to a file — DONE, after being under-sold as
  "not done".** Listing it as an unfinished stretch goal was accurate and
  badly misleading: without it the editor's Inspector was a viewer with drag
  handles, because every edit was lost on reload. Now:
  - `ConfigWriter` is the write counterpart to `ConfigNode`, pimpl'd over the
    same parser and equally absent from the public headers.
  - Every component implements `Serialize`, writing **the same keys its
    `Deserialize` reads**. The base returns `false`, so a component that cannot
    save is **reported by name** rather than silently dropped from the file.
  - `Scene::Save` regenerates only `name`, `camera` and `entities`, and
    **preserves every other top-level key** — `_comment`, `prefabs`, anything a
    newer build understands. Parenting is recovered from the **transform tree**,
    not from the source file, so an entity reparented in the editor saves
    correctly.
  - `Engine::SaveScene` pushes the **live camera** in first, mirroring
    `LoadScene` applying the scene's camera to the live one.

  Verified rather than assumed — `sandbox --save-check` loads, saves elsewhere,
  reloads, and compares name, parent, component count, local transform and
  world position for every entity:

  ```
  captured 22 entities, refcount 21
  ROUND TRIP: 22 entities, 0 mismatch(es), refcount 21 -> 21 -- PASS

  captured 14 entities, refcount 13
  ROUND TRIP: 14 entities, 0 mismatch(es), refcount 13 -> 13 -- PASS
  ```

  The refcount check matters as much as the geometry: a round trip that
  reloaded textures without releasing the originals would pass every positional
  comparison and leak.
- **3. Scene reload preserving camera state — partially.** The editor's
  **File > Reload Scene** reloads from `Scene::SourcePath()` and clears the
  selection first (a selection surviving into a new scene would name a slot a
  different entity now occupies). It applies the scene file's camera rather than
  preserving the current one.

---

## 🏁 Milestone 3 — checklist

- [x] A scene file describing **22 entities** loads and renders with **zero
      hardcoded content in the C++** — greps in §1
- [x] Path independence: asset root discovered from the executable's location,
      not the CWD; verified from three working directories and two build
      configurations. Two-machine column left empty rather than invented (§2)
- [x] **Unloading returns every refcount to zero** — `TOTAL REFCOUNT 0`, watched
      live on the Resources panel or via `--m3-check`
- [x] `checker_red.bmp` at **refcount 18**, proving the path lookup finds the
      existing entry rather than loading eighteen copies
- [x] A stale handle is **detected and reported** with its index and generation,
      **not dereferenced**, and the engine keeps running
- [x] Async path routes through the Week 5 queue; the callback thread is
      documented and deferred to the main thread deliberately
