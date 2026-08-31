# Week 7 — Milestone 2 Evidence

---

## 1. Subsystem boot order

Registration order **is** dependency order: each subsystem may assume everything
above it is up. Declared in `Engine::RegisterBuiltinSubsystems`
(`engine/src/core/Subsystem.cpp`).

| # | Subsystem | Depends on |
|---|---|---|
| 1 | **Logging** | nothing — and it must be first, because everything logs, *including everything's own shutdown* |
| 2 | **Profiling** | Logging (it reports through it). Second so its report is second-to-last, while the logger is still open |
| 3 | **FileSystem** | Logging (it logs its resolved asset root at Info, every boot) |
| 4 | **Memory** | Logging, plus the config having been read for its slab sizes |
| 5 | **Platform** (Window) | Logging |
| 6 | **Renderer** | Platform — it borrows the window's renderer |
| 7 | **EditorGui** *(editor only)* | Platform + Renderer. Not registered at all in `sandbox` |
| 8 | **Input** | Platform (events come from it), EditorGui when present (it asks whether the GUI swallowed a key) |
| 9 | **Jobs** | Logging |
| 10 | **Resources** | FileSystem (reads), Renderer (creates textures), **Jobs** (async reads) |
| 11 | **DebugDraw** | Renderer, Camera |
| 12 | **Messaging** | nothing — but registered **before** Collision, which dispatches through it |
| 13 | **Scene** | Resources (components acquire textures on attach), Messaging |
| 14 | **Collision** | Messaging, Scene |

*(13 in the sandbox, 14 in the editor — EditorGui is the difference, and that
difference is what proves the engine ships without its tools.)*

**Resources after Jobs** is the one the Week 9 patch calls out specifically: get
it backwards and everything works right up until the first async load. It is
exactly the latent ordering bug this week exists to prevent.

**Boot log** — full paste in `week03-shutdown-log.md` §1; the ordering line is:

```
[1/13] Logging up      [2/13] Profiling up    [3/13] FileSystem up
[4/13] Memory up       [5/13] Platform up     [6/13] Renderer up
[7/13] Input up        [8/13] Jobs up         [9/13] Resources up
[10/13] DebugDraw up  [11/13] Messaging up  [12/13] Scene up
[13/13] Collision up
```

**Shutdown log:**

```
[13/13] Collision down  [12/13] Scene down     [11/13] Messaging down
[10/13] DebugDraw down   [9/13] Resources down  [8/13] Jobs down
 [7/13] Input down       [6/13] Renderer down   [5/13] Platform down
 [4/13] Memory down      [3/13] FileSystem down [2/13] Profiling down
 [1/13] Logging down
```

**Is the shutdown order the exact reverse?** **Yes** — 1→13 up, 13→1 down. The
log prints the index precisely so this is checkable by eye rather than by
trusting the claim.

---

## 2. Forced failure

**This is the part that is actually graded, and it is performed without editing
engine code to break it.** `Engine::Options::forceFailSubsystem` names a
subsystem whose `Init()` is made to return false; the sandbox exposes it as a
flag, so the failure path can be exercised any time:

```bash
./build/debug/bin/Debug/sandbox.exe --fail-subsystem Resources
```

Output (ANSI colour stripped):

```
[    0.331] [   Info] [Core        ] (t:b02c)   [5/13] Platform up
[    0.331] [   Info] [Render      ] (t:b02c) Renderer up (direct3d11)
[    0.331] [   Info] [Core        ] (t:b02c)   [6/13] Renderer up
[    0.331] [   Info] [Input       ] (t:b02c) input context 'gameplay': 8 action(s)
[    0.331] [   Info] [Input       ] (t:b02c) input context 'menu': 5 action(s)
[    0.331] [   Info] [Core        ] (t:b02c)   [7/13] Input up
[    0.332] [   Info] [Jobs        ] (t:b02c) JobSystem up with 15 worker thread(s)
[    0.332] [   Info] [Core        ] (t:b02c)   [8/13] Jobs up
[33m[    0.332] [Warning] [Core       ] (t:b02c) FORCED FAILURE: subsystem 'Resources' Init() is being made to return false on purpose
[31m[    0.332] [  Error] [Core       ] (t:b02c) subsystem 'Resources' failed to initialise
sandbox: initialisation failed. The boot log above names the subsystem that refused to start.
[    0.332] [   Info] [Core        ] (t:b02c) unwinding 8 already-initialised subsystem(s)
[    0.332] [   Info] [Core        ] (t:b02c)   [8] Jobs down (unwind)
[    0.334] [   Info] [Jobs        ] (t:b02c) JobSystem down (0 job(s) completed)
[    0.334] [   Info] [Core        ] (t:b02c)   [7] Input down (unwind)
[    0.334] [   Info] [Core        ] (t:b02c)   [6] Renderer down (unwind)
[    0.334] [   Info] [Render      ] (t:b02c) Renderer down
[    0.334] [   Info] [Core        ] (t:b02c)   [5] Platform down (unwind)
[    0.334] [   Info] [Platform    ] (t:b02c) Window destroyed
[    0.386] [   Info] [Core        ] (t:b02c)   [4] Memory down (unwind)
[    0.386] [   Info] [Memory      ] (t:b02c) FrameStack peak was 0 of 1048576 bytes
[    0.386] [   Info] [Memory      ] (t:b02c) EntityPool peak was 0 of 4096 blocks
[    0.386] [   Info] [Memory      ] (t:b02c) MemorySystem down
[    0.386] [   Info] [Core        ] (t:b02c)   [3] FileSystem down (unwind)
[    0.386] [   Info] [FileSystem  ] (t:b02c) FileSystem down
[    0.386] [   Info] [Core        ] (t:b02c)   [2] Profiling down (unwind)
[    0.386] [   Info] [Profile     ] (t:b02c) no timed scopes recorded
[    0.386] [   Info] [Core        ] (t:b02c)   [1] Logging down (unwind)
[    0.386] [   Info] [Core        ] (t:b02c) Log shutting down

process exit code: 1
```

### The checklist

- [x] **Subsystems registered BEFORE the failure were shut down** — 8 of them,
      and the log says "unwinding 8 already-initialised subsystem(s)" before
      doing it.
- [x] **They shut down in reverse order** — 8, 7, 6, 5, 4, 3, 2, 1.
- [x] **The failing subsystem was NOT shut down.** There is no
      `[9] Resources down (unwind)` line. It never came up, so calling
      `Shutdown()` on it would be tearing down something that was never built —
      which is precisely the bug this test exists to find. The unwind loop is
      `for (usize j = i; j-- > 0;)`, starting *below* the failure.
- [x] **Subsystems registered after it were never initialised** — DebugDraw,
      Messaging, Scene and Collision produce no lines at all, in either
      direction.
- [x] **Leak detector still reports zero on this path.** *(This is the box people
      miss — the failure path is a path, and it can leak like any other.)*
      Confirmed with the MSVC CRT debug heap and the `asan-msvc` preset. The
      specific thing that makes it work is RAII on the partial construction:
      `Window`'s `unique_ptr` members adopt whatever `SDL_CreateWindowAndRenderer`
      managed to create *before* failing, so the destructor cleans up whatever
      exists.
- [x] **Non-zero exit code and a readable message** — exit code **1**, and
      `sandbox: initialisation failed. The boot log above names the subsystem
      that refused to start.` on stderr.

**An ordered boot that has never been made to fail is an ordered boot that has
not been tested**, and the reason this is a runtime flag rather than a temporary
edit is so it can be re-run after any change to the boot order without anyone
having to remember to break something and then un-break it.

---

## 3. Allocator test suite

`tests/src/test_allocators.cpp` — provided, unedited. Green as part of the full
suite:

```
[doctest] test cases:    126 |    126 passed | 0 failed | 0 skipped
[doctest] assertions: 198689 | 198689 passed | 0 failed |
[doctest] Status: SUCCESS!
```

The three things the suite pins down that are easy to guess wrong, and what the
implementation does:

- **Exhaustion returns `nullptr`.** No assert, no throw, no growth — and the
  allocator stays usable afterwards, which the suite checks by making a smaller
  request straight after a refused one.
- **Alignment is honoured for every power of two up to 64.** The suite makes a
  deliberate 1-byte allocation before each aligned request so an implementation
  that is only *accidentally* aligned cannot pass by luck. `StackAllocator`
  aligns the **address**, not the size — `AlignUp(baseAddress + cursor, alignment)`
  converted back to an offset. Aligning the offset alone would only be correct
  if the base itself were aligned to at least `alignment`, and for alignment 64
  on a 16-aligned base it is not.
- **A pool's freed blocks are reusable in any order.** Free-list nodes live
  *inside* the free blocks, `memcpy`'d rather than cast-and-dereferenced (reading
  a `void*` through a `u8*` is a strict-aliasing violation — the kind that works
  until the optimiser is turned up).

**Under a sanitizer:** clean under `asan-msvc`.

**What the sanitizer can and cannot see here, which is itself part of the
week's lesson.** The allocators hand out memory from *inside* one big heap
block, so ASan cannot see an overrun from one of my blocks into the next — that
memory legitimately belongs to the process and there is no redzone between them.
It **can** see an overrun past the end of the whole slab. So the tool covers the
outer boundary and is blind to the inner ones, which is exactly why the
guard-byte stretch goal exists and why `PoolAllocator::Free`'s debug ownership
check is worth its two comparisons: it catches inside-the-slab mistakes that no
sanitizer will.

---

## 4. Allocator statistics on screen

Two implementations, both kept, as the Week 7 patch permits:

- **`sandbox`** — debug **text** HUD via `DebugDraw::Text` in screen space
  (`DrawAllocatorHud` in `sandbox/src/main.cpp`), gated on the
  `debug.showAllocatorHud` CVar. The sandbox has no ImGui, and the sandbox is
  what ships.
- **`editor`** — the **Memory panel**, which replaces it and is far better:
  progress bars, peak marked distinctly, and a history plot per allocator.

**Which numbers are displayed, and why those:**

| Number | Why |
|---|---|
| **bytes used / capacity** | the current level, as a progress bar — "am I close to the wall" |
| **PEAK bytes** | *(coloured, on its own line)* the number that answers **"how big does this buffer actually need to be"**, which is the Phase 2 question and which nothing else can answer |
| allocation count | distinguishes "one big allocation" from "ten thousand small ones" at the same byte count |
| blocks in use / block count / peak blocks | the pool's equivalents |
| **a history plot** | the **shape**, which is the thing a single number cannot give |

Peak is displayed prominently and deliberately. A HUD that showed current bytes
but not peak would be answering the less useful question — and the provided
suite has a case requiring peak to survive a rewind precisely because the naive
implementation forgets.

**Live numbers changing as the engine runs.** From a 120-frame run:

```
FrameStack peak was 168 of 1048576 bytes
EntityPool peak was 0 of 4096 blocks
```

168 bytes is 21 sprites × 8 bytes — `SpriteRenderSystem::Render`'s sort index
array, which is allocated from the frame stack each frame and rewound at the end
of the pass. On the Memory panel's plot that is the **sawtooth** a healthy
scratch allocator is supposed to show. It read a flat zero before the render
system was converted to use it, which is a suspiciously flat line for an
allocator that exists to be used — see stretch goal 3 below.

---

## 5. Questions

### 1. What did the slab cost at startup, and what does that imply?

From `week05-os-measurements.md`: **0.392 µs per 4 KiB page** on first touch.

The frame stack is **1 MiB = 256 pages**:

```
256 x 0.392 µs = 100 µs
```

The entity pool is 4096 × 256 B = 1 MiB, another **100 µs**. So the engine pays
about **0.2 ms at startup**, once, where nothing is watching.

**What that implies about ever constructing one mid-frame:** 100 µs is **0.6% of
a 60 Hz frame budget, paid in one lump**. Constructing a `StackAllocator` inside
an update path costs that in *that frame*, and on a frame already near budget it
is a dropped frame — a visible hitch caused by an allocation that "should be
free" because it is just one `operator new`. It is not one `operator new`; it is
256 page faults.

A second consequence, less obvious: it argues against shrinking and re-growing.
`Clear()` moves a cursor and **keeps the pages resident**, which is why the frame
stack can be cleared 60 times a second for nothing. Freeing and reallocating the
slab would re-pay the 100 µs every time.

### 2. One thing for the stack, one for the pool — justified by LIFETIME

**Stack — `SpriteRenderSystem::Render`'s sort index array.** Its lifetime is
*exactly one render pass*. Every element dies at the same instant, which is
precisely the case where "you cannot free individually" costs nothing and
"freeing a thousand costs the same as freeing one" is free money. It is a single
`FreeToMarker` at the end of the pass — one pointer assignment for however many
sprites there were. **(This is done — see stretch goal 3.)**

**Pool — entity records.** Entities have **independent lifetimes**: a bullet
spawned on frame 12 and destroyed on frame 40 has no relationship to an enemy
spawned on frame 13. That is exactly what a stack cannot express — memory comes
back in reverse order or not at all — and exactly what a pool is for, because a
freed block is the right shape for the next request and neither allocate nor free
searches anything.

Note both justifications are about **when things die**, not how big they are. A
1 KB per-frame temporary belongs in the stack and a 16-byte entity belongs in the
pool; size did not enter into it.

### 3. Is the pool's debug ownership check worth a comparison per free?

**Debug build: unambiguously yes.** It is two comparisons and a modulo against a
`Free` that is already a handful of instructions, so call it a 30–50% overhead on
an operation measured in single-digit nanoseconds — utterly invisible against a
16 ms frame. What it buys is converting *silent heap corruption* into an
immediate, named failure at the moment of the mistake. Freeing a pointer that
came from a different pool would otherwise thread a foreign address onto this
pool's free list, and the failure would surface as a wrong pointer handed out
*later*, from an innocent call site. That is a multi-day bug traded for a
comparison.

**Release build: no, and it is compiled out** — the whole block is inside
`#if ENGINE_ASSERTS_ENABLED`. The reasoning is that the check catches a
**programmer error**, and by release the code paths that free pool blocks are
fixed and exercised. Paying for it forever to catch a bug class that ships
already-tested is the wrong trade in the one place where the cost is measured
per-allocation.

The asymmetry is the point: this is a check whose value is entirely in
*development*, so it belongs in the category that vanishes — the same category
as `ENGINE_ASSERT` and for the same reason.

### 4. What still calls `new` or `delete` directly? *(list it; do not have to fix it)*

**Inside the engine, directly:**

| Site | What | Why it is still there |
|---|---|---|
| `StackAllocator` ctor/dtor | `::operator new/delete` with `align_val_t` | The slab itself. Has to come from somewhere. |
| `PoolAllocator` ctor/dtor | same | The slab itself. |
| `ByteBuffer` | `new u8[] / delete[]` | Week 2's module, kept deliberately as written. |
| `Scene::CreateEntity` | `std::make_unique<Entity>` | **The obvious candidate for the entity pool, and the honest gap.** `MemorySystem::EntityPool()` exists, is sized from config, and is currently **unused** — `EntityPool peak was 0 of 4096 blocks` in every run above. Wiring `Entity` allocation through it needs a custom deleter on the `unique_ptr` and is a contained change. It is written down rather than done. |
| `ComponentFactory::Create` | `std::make_unique<Component>` per component | Components are polymorphic and differently sized, so a single fixed-block pool does not fit them without a pool per type. A Phase 2 job. |
| `MessageBus` | `std::make_unique<Subscription>` | Stable addresses, needed so a handler can subscribe while running. Rare — subscriptions are created at setup, not per frame. |

**Indirectly, via the standard library:** every `std::vector`, `std::string`,
`std::map` and `std::function` in the engine. The significant ones are
`LogRecord`'s two `std::string`s (one pair of allocations per surviving log
line — which is why the threshold check happens *before* formatting), and
`std::function` in `JobSystem` and `MessageBus`.

**What is NOT on this list, and matters:** the per-frame update path.
`EventPump`'s vectors are reserved once and `clear()`ed (which keeps capacity),
`DebugDraw`'s command vector likewise, and the sprite sort array comes from the
frame stack. The allocation-count evidence for that is in
`week08-verification.md` §4.

---

## Stretch goals

- **3. Convert one system to a scratch buffer — done.**
  `SpriteRenderSystem::Render` takes its sort index array from
  `MemorySystem::FrameStack()` and rewinds to a marker at the end of the pass,
  with a heap fallback if the stack is full (the allocator returns `nullptr`
  rather than growing, and a render pass must not stop drawing because a buffer
  was sized too small). **This is the first time the instrumentation said
  something that was not already known** — the frame stack read a flat zero
  before, and it now shows a 168-byte sawtooth, which is what confirmed the
  allocator was wired up rather than merely present.
- **2. Guard bytes — not done.** The most valuable of the three and the honest
  gap. The reasoning for wanting it is recorded in `PoolAllocator.h`: ASan
  cannot see an overrun from one of my blocks into the next, so this is the
  category of bug the tooling is blind to.
- **1. Double-ended stack allocator — not done.** No current use; there is only
  one category of temporary.

---

## 🏁 Milestone 2 — checklist

- [x] Provided allocator suite passes, alignment and exhaustion included
- [x] Clean under the sanitizer, with the limits of what it can see stated
- [x] Boot log shows initialisation in declared order
- [x] Shutdown is the exact reverse
- [x] Forced failure produces a clean, leak-free partial teardown, non-zero exit
- [x] Allocator numbers on screen, in both `sandbox` (debug text) and `editor`
      (Memory panel), observably changing as the engine runs
