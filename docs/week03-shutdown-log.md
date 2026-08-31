# Week 3 — Shutdown Order Evidence 🏁 Milestone 0

**Run:** `sandbox --stress 120`, debug preset. The stress mode is used here
rather than an interactive run only because it terminates on its own and so
produces a complete log ending in a clean shutdown.

---

## 1. Construction and destruction order

### Construction — `logs/engine.log`, boot

```
[    0.001] [   Info] [Core        ] (t:2c13)   [1/13] Logging up
[    0.003] [   Info] [Core        ] (t:2c13)   [2/13] Profiling up
[    0.003] [   Info] [FileSystem  ] (t:2c13) asset root resolved to 'C:\dev\Game Engine\GameEngineRepo'
[    0.003] [   Info] [FileSystem  ] (t:2c13) (search started at 'C:\dev\Game Engine\GameEngineRepo\build\debug\bin\Debug\')
[    0.003] [   Info] [Core        ] (t:2c13)   [3/13] FileSystem up
[    0.004] [   Info] [Memory      ] (t:2c13) MemorySystem up: frame stack 1048576 bytes, entity pool 4096 x 256 bytes
[    0.004] [   Info] [Core        ] (t:2c13)   [4/13] Memory up
[    0.346] [   Info] [Platform    ] (t:2c13) Window created: 1280x720 "Engine2D" (renderer: direct3d11)
[    0.347] [   Info] [Core        ] (t:2c13)   [5/13] Platform up
[    0.347] [   Info] [Render      ] (t:2c13) Renderer up (direct3d11)
[    0.347] [   Info] [Core        ] (t:2c13)   [6/13] Renderer up
[    0.347] [   Info] [Input       ] (t:2c13) input context 'gameplay': 8 action(s)
[    0.347] [   Info] [Input       ] (t:2c13) input context 'menu': 5 action(s)
[    0.347] [   Info] [Core        ] (t:2c13)   [7/13] Input up
[    0.348] [   Info] [Jobs        ] (t:2c13) JobSystem up with 15 worker thread(s)
[    0.348] [   Info] [Core        ] (t:2c13)   [8/13] Jobs up
[    0.348] [   Info] [Resource    ] (t:2c13) ResourceManager up
[    0.348] [   Info] [Core        ] (t:2c13)   [9/13] Resources up
[    0.348] [   Info] [Core        ] (t:2c13)  [10/13] DebugDraw up
[    0.348] [   Info] [Core        ] (t:2c13)  [11/13] Messaging up
[    0.349] [   Info] [Core        ] (t:2c13)  [12/13] Scene up
[    0.349] [   Info] [Core        ] (t:2c13)  [13/13] Collision up
```

### Destruction — same run, exit

```
[    2.343] [   Info] [Core        ] (t:2c13)  [13/13] Collision down
[    2.344] [   Info] [Core        ] (t:2c13)  [12/13] Scene down
[    2.345] [   Info] [Scene       ] (t:2c13) scene unloaded; 0 texture(s) resident, total refcount 0
[    2.346] [   Info] [Core        ] (t:2c13)  [11/13] Messaging down
[    2.346] [   Info] [Core        ] (t:2c13)  [10/13] DebugDraw down
[    2.346] [   Info] [Core        ] (t:2c13)   [9/13] Resources down
[    2.346] [   Info] [Resource    ] (t:2c13) ResourceManager down
[    2.346] [   Info] [Core        ] (t:2c13)   [8/13] Jobs down
[    2.348] [   Info] [Jobs        ] (t:2c13) JobSystem down (0 job(s) completed)
[    2.348] [   Info] [Core        ] (t:2c13)   [7/13] Input down
[    2.348] [   Info] [Core        ] (t:2c13)   [6/13] Renderer down
[    2.348] [   Info] [Render      ] (t:2c13) Renderer down
[    2.348] [   Info] [Core        ] (t:2c13)   [5/13] Platform down
[    2.348] [   Info] [Platform    ] (t:2c13) Window destroyed
[    2.425] [   Info] [Core        ] (t:2c13)   [4/13] Memory down
[    2.426] [   Info] [Memory      ] (t:2c13) FrameStack peak was 168 of 1048576 bytes
[    2.426] [   Info] [Memory      ] (t:2c13) EntityPool peak was 0 of 4096 blocks
[    2.427] [   Info] [Memory      ] (t:2c13) MemorySystem down
[    2.427] [   Info] [Core        ] (t:2c13)   [3/13] FileSystem down
[    2.427] [   Info] [FileSystem  ] (t:2c13) FileSystem down
[    2.427] [   Info] [Core        ] (t:2c13)   [2/13] Profiling down
[    2.427] [   Info] [Profile     ] (t:2c13) site                                   min ms     avg ms     max ms   samples
   ... the timer report ...
[    2.428] [   Info] [Core        ] (t:2c13)   [1/13] Logging down
[    2.428] [   Info] [Core        ] (t:2c13) Log shutting down
```

**Is the destruction order exactly the reverse of the construction order?**
**Yes.** 1→13 up, 13→1 down, and the log prints the index so it can be checked
by eye rather than by trusting the claim.

Two things in that pair are worth pointing at:

- **Logging is [1] and therefore [13-last].** It has to be, because everything
  logs *during its own shutdown* — look at `[9] Resources down` being followed by
  `ResourceManager down` from inside the subsystem itself. A logger torn down
  earlier would take those lines with it.
- **Profiling is [2], so `TimerRegistry::Report()` runs second-to-last**, while
  the logger is still open. That ordering is why the timing table is in the log
  file at all.

Week 3 achieved this ordering *implicitly*, by the order objects happened to be
declared. Week 7 made it explicit and declared — the log above is Week 7's
`SubsystemStack`, and the numbers in brackets are the thing Week 3 could only
achieve by being careful.

---

## 2. Leak report

**Zero.**

```
Detected memory leaks!
  (none)
```

MSVC's CRT debug heap reports nothing outstanding at exit; the `asan-msvc`
preset agrees. The specific things that had to be true for this to be zero:

- **`SDL_Quit()` is called exactly once, at the very end of `Engine::Shutdown`,
  after every subsystem that touched SDL is down.** Week 1's `~Window` called it,
  which was wrong the moment `FileSystem` started using `SDL_GetBasePath` before
  the window existed. `~Window` now calls `SDL_QuitSubSystem(SDL_INIT_VIDEO)`
  only. This was found exactly as the lab's "leaks reported inside SDL with no
  frame in your code" hint predicts.
- **The allocators own heap blocks and something has to free them in the right
  order.** `MemorySystem::Shutdown` releases the entity pool then the frame
  stack, and it is subsystem [4], so it happens after everything that could
  still be allocating from them.
- **`ResourceManager::Shutdown` asserts every refcount is zero**, having first
  logged every resource that is not, with its path and count. That assert found
  more real bugs in Week 9 than any test written that week.

---

## 3. Assert diagnostic

A deliberately failed assert, debug build:

```
ASSERTION FAILED: marker <= m_cursor
  at C:\dev\Game Engine\GameEngineRepo\engine\src\memory\StackAllocator.cpp:106
  StackAllocator::FreeToMarker to a marker already rewound past

[    1.204] [  Fatal] [Core        ] (t:2c13) ASSERTION FAILED: marker <= m_cursor at
  C:\dev\Game Engine\GameEngineRepo\engine\src\memory\StackAllocator.cpp:106 -
  StackAllocator::FreeToMarker to a marker already rewound past
```

**Does it name the failing expression as text, the file, and the line?** Yes —
`marker <= m_cursor` is the stringised expression via the preprocessor's `#`
operator, plus `__FILE__` and `__LINE__`, plus the optional message.

It is emitted **twice on purpose**: once to `stderr` directly and once through
the logger at Fatal. The logger is a subsystem and subsystems can be the thing
that is broken; an assert that only works when the engine is healthy is not much
of an assert. The stderr write happens first and unconditionally.

`ENGINE_DEBUG_BREAK()` is `__debugbreak()` on MSVC, so with a debugger attached
this stops *at the failing line with the call stack intact*, rather than
`abort()`ing and leaving only the knowledge that it died.

### How I confirmed it costs nothing in release — naming what I actually inspected

"It did not print" is not sufficient, because a macro that evaluates its
expression and discards the result also does not print. Two things were
inspected:

**1. The preprocessed output.** Compiling with `/EP` (MSVC's preprocess-to-stdout)
and searching for the assert's expression text shows that in the release
configuration the call site expands to:

```cpp
do { } while (false);
```

The expression `marker <= m_cursor` **does not appear anywhere in the expansion**.
It is not evaluated-and-discarded; it is not present. That is the difference
between a macro that expands to `((void)(expr))` and one that expands to nothing,
and it is the whole reason the release definition is written the way it is in
`Assert.h`.

**2. The disassembly.** `/FAcs` on `StackAllocator.cpp` in release: the body of
`FreeToMarker` is a compare, a conditional move and a return. There is no call to
`ReportAssertFailure` and no second comparison — the compare that remains is the
`if (marker > m_cursor) return;` release-mode guard, which is deliberately
separate code, not the assert.

**Why the separate guard exists.** Several asserts in this engine are followed by
an explicit release-mode fallback (`FreeToMarker` returns; `Mat3::Inverse`
returns identity; `EventPump::At` returns a static inert event). The assert says
"this is a programmer error"; the fallback says "and if it happens in a shipped
build, degrade rather than corrupt". They are two different mechanisms and it is
worth not confusing them, because the assert vanishing in release is precisely
why the fallback has to be written by hand.

**The related trap, met deliberately:** nothing that must happen may live inside
an assert, because in release it vanishes. `ENGINE_VERIFY` exists for the case
where the expression has a side effect that must survive — it always evaluates,
and only the *reporting* is compiled out.

---

## 4. Log threshold

Same session, two thresholds, changed in `config/engine.json` with **no rebuild**
(the threshold is read at Init from the config file).

**Run 1 — `"threshold": "Debug"`**

```
[    0.348] [  Debug] [Scene       ] component type registered: TransformComponent
[    0.348] [  Debug] [Scene       ] component type registered: SpriteComponent
[    0.348] [  Debug] [Scene       ] component type registered: AABBColliderComponent
[    0.348] [  Debug] [Scene       ] component type registered: CircleColliderComponent
[    0.349] [  Debug] [Memory      ] StackAllocator 'FrameStack' reserved 1048576 bytes
[    0.349] [  Debug] [Memory      ] PoolAllocator 'EntityPool': 4096 blocks x 256 bytes (requested 256), align 8
[    0.352] [  Debug] [Resource    ] loaded 'textures/checker_blue.bmp' (32x32)
[    0.353] [  Debug] [Resource    ] loaded 'textures/checker_green.bmp' (32x32)
[    0.356] [  Debug] [Resource    ] loaded 'textures/marker_up.bmp' (32x32)
[    0.358] [  Debug] [Resource    ] loaded 'textures/checker_red.bmp' (32x32)
[    0.364] [   Info] [Scene       ] scene 'OrbitTest' loaded from 'scenes/orbit_test.json': 22 entities, 4 textures resident, total refcount 21
[    0.364] [  Debug] [Jobs        ] worker 0 started
  ... 14 more worker lines ...
```

**Run 2 — `"threshold": "Info"`, same session, nothing else changed**

```
[    0.364] [   Info] [Scene       ] scene 'OrbitTest' loaded from 'scenes/orbit_test.json': 22 entities, 4 textures resident, total refcount 21
```

Every `Debug` line is gone; the `Info` line survives. The suppression is not
cosmetic — `Log::ShouldLog` is checked by the `ENGINE_LOG_*` macro **before**
`std::format` runs, so a suppressed message never pays to build the string it
was not going to print. That is why the threshold check lives in the macro and
not inside `Write()`, and it is a design decision rather than an optimisation:
it determines where the check goes relative to argument formatting.

---

## 5. What breaks when a subsystem outlives the logger

**It was caused deliberately, as the lab suggests.** The `Logging` subsystem was
moved from position [1] to position [13] — last up, therefore first down — and
the engine was run.

What it looked like: the process printed the boot log normally, ran, and then at
shutdown produced **partial output and then nothing**. `[13] Logging down` was
the last line. Every subsystem that tore down afterwards — the scene, the
resource manager, the job system, the allocators — announced itself into a
logger whose file stream was closed and whose `g_initialised` flag was false. On
this build the `std::ofstream` writes to a closed stream simply fail silently and
set the stream's fail bit, so:

- The console still showed the lines, because `stdout` is not the logger's to
  close, which made it look like it was working.
- **The log file just stopped**, mid-shutdown, with no error and no marker. The
  most useful part of the file — the teardown, which is where ordering bugs live
  — was exactly the part that was missing.
- The `ResourceManager::Shutdown` refcount audit still ran and still asserted,
  but its explanatory lines naming *which* resource leaked were gone. The assert
  fired with no context.

That last point is the real answer to the question. The failure is not that the
program crashes — it did not crash. It is that **the diagnostic channel is
destroyed before the things it was there to diagnose**, so the exact class of bug
you most need the log for (shutdown-ordering bugs) is the class it cannot report.
A logger that dies first turns every later problem into a silent one.

There is a worse variant that this engine avoids by construction: if `Log::Write`
had dereferenced a destroyed sink rather than checking a flag, the shutdown would
have crashed *inside the logger*, and the stack trace would have blamed whichever
subsystem happened to log next — an innocent one.

The fix is not defensive coding inside the logger; it is ordering. Week 7 makes
that ordering **declared** — `SubsystemStack` initialises in registration order
and shuts down in exact reverse, and `Logging` is registered first on purpose,
with a comment saying why. The bracketed indices in the log above are what turned
"I think it is in the right order" into something checkable.

---

## 🏁 Milestone 0 — checklist

- [x] Fresh clone builds and runs (`scripts/fresh-clone-check.sh`)
- [x] Window opens, clears, shuts down cleanly
- [x] Leak detector reports **zero**
- [x] A failed assert names the expression, the file and the line
- [x] Assert compiles out entirely in release — verified by preprocessed output
      and disassembly, not by absence of printing
- [x] One run's log contains **14 channels** at multiple levels; raising the
      threshold demonstrably suppresses the lower one
- [x] Every `printf`/`cout` gone from the engine. The two survivors are
      deliberate and are not the engine: `Assert.cpp`'s `fprintf(stderr, ...)`,
      which must work when the logger does not, and `sandbox/src/main.cpp`'s
      usage text and final "Clean exit."
- [x] **SDL is `PRIVATE`.** `grep -rn "SDL_Window\*\|SDL_Renderer\*"` over
      `engine/include/`, `sandbox/src/` and `editor/src/` returns only
      `SdlHandles.h`'s two deleter declarations and two comments. Nothing in the
      public headers, the sandbox or the editor can reach SDL.
