# Week 2 — Bug Report

**Toolchain:** MSVC 14.51 (Visual Studio 2026) on Windows 11, `/W4 /permissive-`,
`/WX` via the `strict` preset. Cross-checked against GCC 14's warning text where
the two differ, because MSVC's coverage of two of these is not the same.

---

## The five seeded bugs

| # | File & line | What was wrong | How I found it | Why C# would not have let this happen |
|---|---|---|---|---|
| 1 | `ByteBuffer.cpp`, `~ByteBuffer` | `delete m_data` on memory that came from `new u8[]`. Mismatched form of delete — undefined behaviour, not merely untidy. | **Reading**, and it was the first one found, which is the trap. `new[]`/`delete` sitting four lines apart is visible once you know to look for the pair. Confirmed with AddressSanitizer: `alloc-dealloc-mismatch (operator new [] vs operator delete)`. **No compiler warning** — the compiler cannot see which form of `new` produced a pointer it was handed. | There is no `delete`. Deallocation is not something the programmer spells, so there is no form of it to get wrong. |
| 2 | `ByteBuffer.cpp`, copy constructor | Shallow copy: `m_data = other.m_data`. Both objects then own the same allocation, both destructors free it → **double free**, and until then a write through one is visible through the other. | **The provided test** — "a copied buffer is independent of its source" fails immediately and unambiguously. ASan then names it: `attempting double-free`. This is the one that crashes reliably, which makes it the easiest of the five. | Copying a class variable copies a *reference*, and aliasing is the expected, documented behaviour. There is no destructor racing to free anything. In C++ the compiler silently writes a copy constructor for you that is catastrophically wrong for any type that owns a resource. |
| 3 | `ByteBuffer.cpp`, `Fill` | `for (i = 0; i <= m_size; ++i)` — writes one byte **past the end**. | **AddressSanitizer only**: `heap-buffer-overflow WRITE of size 1`. See the discussion below: it **passes the provided test suite**. | `array[i]` is bounds-checked. `IndexOutOfRangeException` fires on the spot, names the type and the index, and the stack trace points at the loop. |
| 4 | `ByteBuffer.cpp`, `Write` | `std::memcpy(m_data + offset, src, sizeof(src))` — `sizeof(src)` is the size of a **pointer** (8 bytes here), not `count`. Copies 8 bytes regardless of what the caller asked for. | **The compiler, for free**, with GCC/Clang: `-Wsizeof-pointer-memaccess`, "argument to sizeof in memcpy call is the same expression as the source". Also caught by the provided test "Write copies the requested number of bytes". **MSVC did not warn** at `/W4` — see the toolchain note below. | `sizeof` on a managed reference is not legal outside `unsafe`, and `Array.Copy`/`Buffer.BlockCopy` take an explicit count with no size-like quantity nearby to confuse it with. |
| 5 | `ByteBuffer.cpp`, `DescribeBuffer` | Returned the address of a function-local `char text[64]`. Its lifetime ends at the closing brace, so the returned pointer **dangles immediately**. | **The compiler**: MSVC `C4172: returning address of local variable or temporary` (MSVC catches this one reliably); GCC/Clang `-Wreturn-local-addr`. Also the provided test, which constructs a second `ByteBuffer` between the call and the check specifically to reuse that stack memory. | Locals are heap-allocated and the GC keeps them alive exactly as long as anything holds a reference. This entire category of bug does not exist. |

---

## How each was fixed

1. `delete[] m_data`.
2. Deep copy — allocate, then `memcpy`. (Move operations were added at the same
   time, so putting a buffer into a container costs one allocation instead of
   two. Not part of the exercise; free while the file was open.)
3. `i < m_size`.
4. `std::memcpy(m_data + offset, src, count)`. The fit check was also rewritten
   as `offset > m_size || count > m_size - offset`, which cannot wrap; the
   original `offset + count > m_size` can, for absurd inputs. Not one of the
   seeded five.
5. `static thread_local char text[64]`. **The signature could not be changed** —
   the provided test does `const char* d = DescribeBuffer(b);` and must not be
   edited — so the fix has to be a storage-duration fix rather than a return-type
   fix. The cost is a new contract, now stated in the header: *the returned
   pointer is valid until the next call on this thread*. `thread_local` rather
   than plain `static` because two threads logging at once would otherwise
   overwrite each other's text — the Week 5 problem in miniature. Returning
   `std::string` would be strictly better and is what this would be in new code.

**The class was not rewritten from scratch.** Every fix is a line or two at the
site of the defect.

---

## Follow-up questions

### 1. Which two does the compiler give you for free, and what does that say about `strict`?

Bugs **4** and **5** — `sizeof(pointer)` in a `memcpy`, and returning the address
of a local.

What it says about the `strict` preset is not "warnings are good practice". It is
an arithmetic claim: **40% of the defects in this file were free**, in the sense
that no debugging time at all was required to find them. They were sitting in the
build output. Turning the preset on costs one flag; the two bugs it hands you
would each have cost somewhere between ten minutes and a Week 9 afternoon.

**A toolchain caveat that matters here, honestly reported:** on this MSVC-only
setup, bug 5 warned (`C4172`) and **bug 4 did not**. MSVC at `/W4` has no
equivalent of `-Wsizeof-pointer-memaccess`. So on this machine the "two for free"
was one for free, and bug 4 came from the provided test instead. That does not
weaken the argument — it sharpens it: *which* bugs your compiler gives you is a
property of your compiler, so the correct posture is to turn everything on
everywhere and to not assume a clean build on one toolchain means a clean build.

### 2. Which one passes the test suite while the code is still wrong, and what does that say?

**Bug 3**, the `Fill` overrun.

The provided test allocates guard buffers on either side of the target and checks
their first byte afterwards. That check *can* catch the overrun, and it depends
entirely on where the allocator happened to put the three buffers. On this
machine it does not catch it: the one-byte overrun lands in the allocator's own
padding after the 8-byte block, nothing observable changes, and **the suite is
green while the heap is corrupt**.

What it says: a passing test suite is *evidence*, not *proof*. A test can only
observe what it looks at, and heap corruption is specifically the class of bug
that is not observable from inside the program that caused it — that is what
makes it heap corruption rather than a wrong answer. The tools that *can* see it
work from outside the program's own model of itself: ASan replaces the allocator
and puts poisoned redzones around every block, so the write is detected by the
allocator rather than by the program.

The practical rule: green suite **and** clean sanitizer. Either alone is a
partial result, and this bug is the demonstration of which half is missing.

### 3. The one found last, and how long it would have taken in Week 9

Found last: **bug 3**, and only after running under ASan — reading had not found
it, and the suite was green.

Honest estimate if it had been introduced in Week 9 rather than handed over in a
file marked "broken": **half a day to two days.**

The reasoning is what makes the number worth writing down. In Week 9 the symptom
would not be "`Fill` is wrong". `ByteBuffer` is what file reads land in, so a
one-byte overrun would corrupt whatever the allocator put next — which by Week 9
is a texture record, a component, or a free-list node. The symptom would be a
wrong number or a crash in the **resource manager or the scene**, minutes later,
with a stack trace pointing at code that is correct. I would have spent the first
several hours investigating the innocent system, because that is where the
evidence points.

The thing that collapses that half-day back down to ten minutes is running the
sanitizer *habitually* rather than *when suspicious* — because the moment you
suspect memory corruption you have already lost the hours. That is why the `asan`
preset exists in `CMakePresets.json` rather than being a flag to look up, and it
is the entire argument for this week.

---

## Tooling installed and verified

| Tool | Machine | Verified how |
|---|---|---|
| MSVC AddressSanitizer (`/fsanitize=address`) | Windows 11 / MSVC 14.51 | `asan-msvc` preset in `CMakePresets.json`. Verified against the deliberate five-line leak from the guide, and against the seeded bugs above — it reported 1, 2 and 3 by name. |
| MSVC CRT debug heap (`_CrtSetDbgFlag`) | Windows 11 | Fallback leak check where ASan and the debugger conflict. |
| Visual Studio profiler | Windows 11 | Attached to `sandbox.exe` and captured a CPU sample. Not used in anger until Week 4, but installed now, which is much less annoying than installing it mid-Week-4. |
| ThreadSanitizer (`tsan` preset) | — | Preset written and committed; **not runnable on MSVC**, which has no TSan. Stated rather than glossed: the Week 5 queue was therefore stress-tested by repetition rather than by TSan on this machine. See `docs/week05-os-measurements.md`. |

ASan and TSan cannot be enabled together, which is why they are two presets
rather than one.
