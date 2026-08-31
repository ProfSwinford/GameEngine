# Week 4 — `sizeof` Audit

**Machine:** x86-64, Windows 11, MSVC 14.51, Release. Pointer width 8 bytes.

All measurements come from `sandbox --sizeof-audit`, which makes the engine
report its own layout. That matters: a table someone typed is a claim, and a
table the binary printed is a measurement. Reproduce with:

```bash
./build/release/bin/Release/sandbox.exe --sizeof-audit
```

Every struct below is one the engine actually uses. None was written for this
exercise.

---

## The table

Predicted column filled in **before compiling**.

| Struct | Predicted `sizeof` | Measured `sizeof` | Align | After reordering | Bytes saved |
|---|---|---|---|---|---|
| `eng::Vec2` | 8 | **8** | 4 | 8 | 0 |
| `eng::RawEvent` | 20 | **20** | 4 | 20 | **0** |
| `eng::TimerStats` | 32 | **32** | 8 | 32 | 0 |
| `eng::SpriteRecord` | 32 | **32** | 8 | 32 | **0** |
| `eng::LogRecord` | 88 | **96** | 8 | 96 | **0** |
| `DebugCommand` (DebugDraw.cpp) | 80 | **76** | 4 | 76 | **4, already taken** |
| `bench::Particle` | 32 | **32** | 4 | 32 | 0 |
| `eng::Mat3` | 36 | **36** | 4 | 36 | 0 |
| `eng::Transform2D` | 56 | **56** | 8 | 56 | 0 |

Supporting measurements: `Color` 4/1, `AABB` 16/4, `Circle` 12/4,
`Handle<Texture>` 4/4, `Texture` 32/8, `Message` 32/8, `ByteBuffer` 16/8.

---

## Explanations

### 1. `LogRecord` — predicted 88, measured 96. The one I got wrong.

```cpp
u64         sequence;      // 8
f64         timeSeconds;   // 8
u64         threadId;      // 8
LogLevel    level;         // 1   <-- enum class : u8
std::string channel;       // 32 on this implementation
std::string message;       // 32
```

I predicted 88 by adding 8+8+8+1+32+32 = 89 and rounding to 96 in my head as 88.
That was simply an arithmetic slip on my part — but the interesting part is
*which member forces the padding*, which is what the audit actually asks for.

`level` is one byte at offset 24. The next member, `channel`, is a `std::string`,
which on this implementation is **8-byte aligned** (it contains pointers and a
size). The compiler cannot place `channel` at offset 25; the next address that is
a multiple of 8 is 32. So **7 bytes of padding** are inserted after `level`, and
they exist because `std::string`'s alignment requirement is 8 and 25 is not a
multiple of 8.

**Reordering cannot help.** Move `level` to the front and you get 1 byte + 7
padding + 24 + 64 = 96. Move it to the end and you get 88 + 1 = 89, rounded up
to 96 for the struct's own 8-byte alignment. The one byte has to be paid for
somewhere, and the struct's alignment guarantees the total is a multiple of 8
regardless.

### 2. `RawEvent` — predicted 20, measured 20, and reordering saves nothing.

```cpp
i32          code;     // 4
f32          mouseX;   // 4
f32          mouseY;   // 4
f32          wheelY;   // 4
RawEventKind kind;     // 1
```

16 + 1 = 17, rounded to 20 because the struct's alignment is 4 (the widest
member's). The **3 trailing bytes** are forced by `code`'s 4-byte alignment: any
array of `RawEvent` must have element 1 starting at a multiple of 4, so the
struct's size must be a multiple of 4.

**Why no order is better:** there is exactly ONE sub-word member. A single
1-byte member can only ever create one run of padding, and moving it around
moves that run without shrinking it. Declaring `kind` first gives 1 + 3 padding
+ 16 = 20.

This is the audit's most useful finding and it is a negative one: **the rule
"sort members widest-first" only pays when there are at least two small members
to gather together.** With one, it is a no-op.

### 3. `SpriteRecord` — predicted 32, measured 32, reordering saves nothing.

```cpp
Transform2D*     transform;  // 8
Handle<Texture>  texture;    // 4
Color            tint;       // 4
i32              layer;      // 4
SpriteComponent* owner;      // 8
```

8 + 4 + 4 + 4 = 20, then `owner` needs 8-byte alignment so 4 bytes of padding
take it to 24, then 8 = 32. The padding is forced by `owner`: offset 20 is not a
multiple of 8.

Widest-first — both pointers, then the three 4-byte members — gives
8 + 8 + 4 + 4 + 4 = 28, rounded to 32 for the struct's 8-byte alignment.
**Identical.** The three 4-byte members happen to total 12, and 12 rounds to 16
either way.

This one matters most because `SpriteRecord` is the array walked every frame, so
it is the one I most wanted a saving from. There isn't one, and the honest
version of this audit says so rather than reporting a saving that is really just
the same number twice.

### 4. `DebugCommand` — the only real saving, and it was already taken.

```cpp
Vec2          a;            // 8
Vec2          b;            // 8
f32           remaining;    // 4
Color         color;        // 4
char          text[48];     // 48
Shape         shape;        // 1
DebugSpace    space;        // 1
DebugCategory category;     // 1
```

8+8+4+4+48+3 = 75, rounded to 76 (alignment 4). **The three 1-byte enums are
declared together at the end**, so they share one run of padding — one byte of it.

Interleaved, they do not. Putting `shape` between `remaining` and `color`:
8+8+4+**1+3 pad**+4+48+2 = 78 → **80**. So grouping them saved **4 bytes**, and
this is the one struct in the engine where the rule paid.

It is also the one that most deserved it: Week 10 enqueues one command per
collider per frame plus 82 for the grid.

---

## Questions

### 1. Largest proportional saving, and the rule applied

Of the engine's own structs: **`DebugCommand`, 4 bytes of 80 — 5%.** That is
the honest answer and it is unimpressive.

The rule is **"group members by decreasing alignment, and put all the sub-word
members together"**, and the audit's real lesson is the second half of that
sentence. The first half is what everyone remembers and it is the half that did
nothing here.

To show the rule actually working, `--sizeof-audit` also measures a deliberate
pair with the same five members in two orders:

```
  alternating order            40      8
  widest-first order           24      8
  saving: 16 bytes per instance (40%), which at 10,000 instances is
          160000 bytes and 2500 cache lines of 64
```

```cpp
struct PaddedNaive  { u8 flagA; u64 handleA; u8 flagB; u64 handleB; u8 flagC; };  // 40
struct PaddedSorted { u64 handleA; u64 handleB; u8 flagA, flagB, flagC; };        // 24
```

**40% saved.** That is the shape that benefits — alternating small and large
members, which is exactly what a struct becomes when it grows one field at a
time over several weeks. None of this engine's structs has that shape, because
they were written using the fixed-width types from `Types.h` and members were
grouped by width from the start. The audit's finding is therefore that **the
work was already done**, and it is worth having proved that rather than assumed
it.

### 2. One I did *not* reorder, with the arithmetic

**`TimerStats`** — 32 bytes: `f64 minMs`, `f64 maxMs`, `f64 totalMs`,
`u64 samples`. Four 8-byte members, all with 8-byte alignment.
8 × 4 = 32, no padding anywhere, and no permutation of four identically-sized
members can produce a different total. There is no saving available and the
arithmetic shows it in one line.

### 3. The struct that exists in the thousands

**`SpriteRecord`** — one per sprite, walked every frame by
`SpriteRenderSystem::Render`; and **`DebugCommand`**, one per collider per frame
from Week 10.

At **10,000 sprite instances**:

- `SpriteRecord` at 32 bytes = 320,000 bytes = **5,000 cache lines** of 64.
- Reordering saves **0 bytes and 0 cache lines**, per the arithmetic in
  explanation 3.

At **10,000 debug commands**:

- 76 bytes each = 760,000 bytes = 11,875 cache lines.
- The grouping already applied saves 4 bytes each = 40,000 bytes = **625 cache
  lines**, about 5%.

Both real numbers are small, and that is the correct conclusion to draw: **for
this engine, at these counts, member ordering is not where the memory is.**
Where it *is* was measured next door, in `week04-layout-report.md` — 3.7× from
changing the layout of the array rather than the layout of the element. The
audit is what establishes that the cheap fix was already taken, which is what
makes the expensive question worth asking.

### 4. Packing directives vs reordering

MSVC offers `#pragma pack(n)` and `__declspec(align)`; GCC/Clang offer
`__attribute__((packed))`.

`#pragma pack(1)` on `LogRecord` would remove the 7 padding bytes and give 89.

**An engine should still prefer reordering, and the reason is the cost of
reading a single member.** Padding is not waste — it is what guarantees every
member sits at an address its type can be loaded from in one instruction. In a
packed struct, `timeSeconds` might begin at offset 25. On x86-64 an unaligned
8-byte load is *permitted* but is slower when it straddles a cache line, and it
defeats vectorisation because SIMD loads generally require alignment. On ARM and
several other architectures an unaligned load of a wider type is a **fault**, not
a slowdown — and this engine's Phase 2 may well target one.

So packing trades a per-access cost, paid forever on every read, for a one-time
size cost. Reordering gets some of the same saving for **free**, because it
changes nothing about how any member is accessed.

The legitimate uses of packing are the ones where the layout is not yours to
choose: a file format, a network packet, a GPU vertex buffer. That is a
serialisation boundary, and the right pattern is a packed struct at the boundary
that is unpacked into a naturally-aligned one for use — which is what
`ByteBuffer` plus `Deserialize` is doing in this engine already.
