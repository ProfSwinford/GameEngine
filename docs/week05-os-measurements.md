# Week 5 — Platform Measurement Report

**Machine / CPU / core count:** x86-64, **16 hardware threads**.
**OS:** Windows 11 Pro 26200.
**Build configuration:** **Release**, MSVC 14.51, `/O2`. Measured with the
Week 4 `ScopedTimer` / `steady_clock`.

Reproduce with:

```bash
./build/release/bin/Release/sandbox.exe --os-measure
```

Raw output:

```
1. thread create+join :   160.83 us  (200 iterations, spawn a no-op thread and join it)
2. context switch     :     0.48 us  (5000 condition-variable round trips, 2 switches each, nothing subtracted)
3. first-touch page   :    0.392 us per 4096-byte page (64 MiB, one write per page; page size from the OS, not assumed)
```

---

## 1. Thread creation overhead

**Method.** Spawn a `std::thread` whose body is empty, join it, repeat **200
times**, divide the total by 200. Timing a single create+join would be swamped
by clock granularity and by which core the scheduler happened to pick.

- Iterations timed: **200**
- **Result: 160.83 µs per create+join**

**Implication.** A 60 FPS frame is 16,667 µs, so 1% of the frame budget is
**166.67 µs**.

```
166.67 µs / 160.83 µs = 1.04
```

**You can afford ONE thread creation per frame before thread creation alone eats
1% of your frame budget.** One. Not one per entity, not one per asset — one, in
total, for the whole frame.

That number is the argument for a thread **pool**, and it is not a close call.
Any design that spawns a thread per unit of work is spending more time creating
threads than doing work unless each unit takes well over a millisecond. This is
why `JobSystem` creates its 15 workers once at startup and then never creates
another: `JobSystem::Init` is called from subsystem [8] and the threads live
until `Shutdown`.

It is also worth noticing the scale relative to measurement 3: creating one
thread costs about the same as first-touching 410 pages, or 1.6 MB. Threads are
not cheap objects.

## 2. Context switch cost

**Method.** Two threads ping-ponging a `turn` flag through **one** mutex and
condition variable, **5,000 round trips**. Each round trip is **two** switches
(A wakes B, B wakes A), so the total is divided by 10,000.

**What was subtracted: nothing, and that is deliberate.** The figure therefore
includes the mutex acquire/release and the condition-variable signal as well as
the kernel switch itself. That is honest for the question actually being asked —
*"what does it cost to hand a piece of work to another thread?"* — and it is why
this number is larger than a raw kernel-switch microbenchmark would report. A
figure with the synchronisation subtracted out would be a number you could never
actually achieve.

- Iterations timed: **5,000 round trips = 10,000 switches**
- **Result: 0.48 µs per switch**

**Implication.** If two threads pass work back and forth once per item, each item
costs **two** switches — one to hand it over, one to hand the result back — so
the overhead floor is **~0.96 µs per item**.

For the handoff to be worth it, the work per item has to be meaningfully larger
than that, and "meaningfully" means at least an order of magnitude if you want
the parallel version to actually be *faster* rather than merely *concurrent*.
So: **~10 µs of work per item** is the sensible threshold.

Concretely, for this engine: at 0.48 µs per switch, handing a **single sprite
transform** to a worker thread would be absurd — the transform is a few dozen
nanoseconds. Handing a **BMP file read and decode** to a worker is obviously
worth it: a 3 KB BMP read plus `SDL_LoadBMP_IO` plus texture creation is
hundreds of microseconds, two to three orders of magnitude above the floor. That
is exactly the split this engine made — `FileSystem::ReadFileAsync` goes to the
pool, and nothing in the per-frame update does.

## 3. First-touch page fault cost

Allocating memory is not the same as having it. The OS hands back address space
and materialises physical pages lazily, on the first write to each.

**Method.** `::operator new` a **64 MiB** block, then time writing **one byte per
page** across the whole block, and divide by the page count. One byte per page is
what isolates the fault cost from the memory bandwidth of actually filling it.
The block is `volatile`-qualified through the write so the loop cannot be elided.

- Block size: **64 MiB**
- **Page size on this machine: 4096 bytes** — obtained from
  `GetSystemInfo().dwPageSize`, **not assumed**. (`sysconf(_SC_PAGESIZE)` on
  POSIX; the code has both.)
- Pages touched: 16,384
- **Result: 0.392 µs per page on first touch**

**Implication — and this is the one that feeds directly into Week 7.**

The engine's frame stack is **1 MiB = 256 pages**:

```
256 pages x 0.392 µs = 100 µs
```

**100 µs is 0.6% of a 60 Hz frame, paid in one lump.** The entity pool is
4096 × 256 bytes = 1 MiB, another 100 µs.

So: **an allocator must take its block at startup and never mid-frame.** A
`StackAllocator` constructed during a level load costs 100 µs once, where
nothing is watching. The same constructor called inside an update path costs
100 µs *in that frame*, which at 60 Hz is a visible hitch on its own and, if it
happens on a frame that was already near budget, is a dropped frame.

This is precisely why `StackAllocator`'s constructor comment says the heap block
is taken **once**, and why `MemorySystem::Init` is subsystem [4] in the boot
order rather than something constructed on demand. The 100 µs figure is the
justification, and it is measured rather than asserted.

A second consequence, less obvious: it also argues against *shrinking* an
allocator and re-growing it. `StackAllocator::Clear()` moves a cursor and keeps
the pages resident, which is why the frame stack can be cleared 60 times a
second for free. Freeing and reallocating the slab would re-pay the 100 µs every
time.

---

## 4. Comparison against different hardware

| Measurement | Mine (x86-64, 16 threads, Win11) | Classmate | Ratio |
|---|---|---|---|
| Thread create+join | **160.83 µs** | *(to fill in)* | |
| Context switch | **0.48 µs** | *(to fill in)* | |
| Page fault (first touch) | **0.392 µs / 4 KiB page** | *(to fill in)* | |

**Not yet filled in** — this is a solo build and there is no second machine to
compare against, so recording a fabricated column would be worse than leaving it
empty. What can be said without a second machine, and what the exercise is
getting at:

The measurement expected to differ **most** is **thread creation**. It is the one
that is dominated by the *operating system's* work rather than the CPU's —
allocating a kernel thread object, a stack, scheduler bookkeeping — and those
costs differ substantially between Windows and Linux (Linux `clone()` is
typically several times cheaper than a Windows thread create). Page fault cost
also varies with page size: a machine configured with 2 MiB huge pages would
report a per-page figure hundreds of times larger and a per-*byte* figure much
smaller, which is why the code asks the OS for the page size instead of
hardcoding 4096.

Context switch cost should be the most *stable* across machines, because it is
mostly a fixed sequence of instructions plus a TLB/cache disruption.

**What that suggests about hardcoding any of these into engine code:** don't.
Not one of these three numbers is a constant, and the two that vary most are the
two an engine would be most tempted to build a policy on ("spawn a thread if the
job is bigger than N", "pre-fault M pages"). The engine's actual response is to
hardcode none of them: `workerThreadCount: 0` in `config/engine.json` means "ask
`std::thread::hardware_concurrency()` and leave one for the main thread", the
allocator sizes are config values rather than literals, and the page size is
queried. Where a threshold is genuinely needed, it belongs in a CVar so it can be
tuned per machine without a rebuild.

---

## 5. One paragraph: a BAD candidate for threading in this engine

Ch. 4.1–4.3 distinguishes concurrency from parallelism and is direct about where
the real wins are. Naming a bad candidate is more useful than listing good ones,
so:

**`SpriteRenderSystem::Render` is a bad candidate for threading, and would stay
bad even if the sprite count went up by two orders of magnitude.**

Three reasons, in order of how decisive they are. First, it is **not
parallelisable at all in its current form**: it issues `SDL_RenderTextureRotated`
calls against a single `SDL_Renderer`, which is not thread-safe, so the work
would have to be split into a "compute the transforms" phase and a "submit the
draws" phase, and only the first half could move. Second, the half that *could*
move is tiny — the measured cost of the whole pass is **0.089 ms average for 21
sprites**, and the transform arithmetic is a fraction of that. At 0.96 µs of
handoff overhead per item, and roughly 4 µs of total transform work in the whole
pass, the synchronisation would cost more than the work several times over.
Third, and most fundamentally, it happens **once per frame at a point where the
main thread has nothing else to do** — the main thread would spawn the work and
then immediately block waiting for it, which is concurrency with no parallelism:
all of the cost and none of the benefit.

The general shape of the mistake is worth stating: the tempting candidates are
the ones that show up in the profiler, and `SpriteRenderSystem::Render` is
currently the largest per-frame line item at 0.089 ms. Being the biggest line
item is not the same as being a good candidate — the questions are whether the
work is *divisible*, whether each division is *large relative to 1 µs*, and
whether the main thread has *something else to do while it waits*. Sprite
rendering fails all three. Asset loading passes all three, which is why it is the
one thing this engine actually threads.

---

## Thread sanitizer — stated honestly

**Not run.** The `tsan` preset exists in `CMakePresets.json` and is committed,
but **MSVC has no ThreadSanitizer**, and this build is MSVC-only. The preset is
guarded to non-Windows hosts.

What was done instead, since the queue is the file Week 9 depends on most:

- The provided suite (`tests/src/test_queue.cpp`) was run **repeatedly**, not
  once — race conditions are probabilistic and a single pass is not evidence.
- The engine's shutdown path exercises `Stop()` → `join()` on 15 workers on every
  single run of `sandbox` and `editor`, and **has never hung**. That path is the
  Week 9 failure mode (`notify_one` instead of `notify_all`), and it is exercised
  far more often by ordinary use than by the test.

That is weaker evidence than TSan and it is recorded as such. The async load path
is the single most likely place in the whole engine for a race, and Phase 2 will
exercise it much harder than this week does — so the honest statement is that
the design is *argued* to be race-free (the callback is deferred to the main
thread by `FileSystem::PumpCompletions`, precisely so that no worker ever touches
the renderer or the resource tables) and *not verified* by a sanitizer on this
machine.
