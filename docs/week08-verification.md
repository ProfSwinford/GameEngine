# Week 8 — Verification

---

## 1. The grep check

Searching **gameplay-facing code** — the sandbox, plus the editor, which is the
engine's other external consumer — for raw key codes.

```bash
$ grep -rn "SDL_SCANCODE\|SDLK_\|SDL_BUTTON" sandbox/ editor/
sandbox/src/CollectorGame.cpp:160:        // ACTIONS, NOT KEYS. Grep this file for SDL_SCANCODE and there is
```

**One hit, and it is inside a comment** that says the grep will find nothing —
which it now amusingly does. That is the only occurrence, and it is text in a
`//` line, not code.

The stronger version of the same check, because raw key codes are only one way
the abstraction could leak:

```bash
$ grep -rn "SDL" sandbox/src editor/src
sandbox/src/CollectorGame.cpp:160:        // ACTIONS, NOT KEYS. Grep this file for SDL_SCANCODE and there is
sandbox/src/main.cpp:5://  ship without its tools attached. It links `engine` and cannot reach SDL -
editor/src/panels/EventInspectorPanel.cpp:1:// WEEK 2 PANEL. Note what is NOT included here: any SDL header. That absence
editor/src/panels/EventInspectorPanel.h:13://  and includes NO SDL HEADER. It also shows key NAMES, which it gets from
editor/src/panels/EventInspectorPanel.h:18://  If this panel could not be written without including SDL, that would be the
editor/src/panels/ResourcePanel.cpp:56:        // SDL_Renderer backend, so previews are nearly free - and startlingly
```

**Six hits, every single one inside a comment.** No `#include`, no identifier, no
constant. Neither the sandbox nor the editor contains one line of SDL *code*.

That is not discipline, it is enforced by the build: Week 3 changed
`target_link_libraries(engine PRIVATE SDL3::SDL3)`, so SDL's include directories
do not propagate to `sandbox` or `editor`. An `#include <SDL3/SDL.h>` in either
target is a **compile error**, not a code-review finding.

**Where key codes legitimately still live:** `engine/src/input/InputMap.cpp` and
`engine/src/platform/EventPump.cpp`, both below the abstraction. The Event
Inspector panel displays key *names* — it calls `EventPump::KeyName(code)` and
gets back `"Space"` — so even the debug tool that exists to look at raw events
does not know what a scancode is.

---

## 2. Config takes effect without a rebuild

Three things changed in `config/engine.json`, then the executable was run again
with **no rebuild**.

| What you changed | From | To | Took effect? |
|---|---|---|---|
| Window size | `1280 x 720` | `1024 x 576` | **Yes** — `Window created: 1024x576 "Engine2D"` |
| A behavioural tunable (`tunables.debugCircleSegments`) | `24` | `48` | **Yes** — debug circles visibly smoother; `DebugDraw::CircleSegments()` reads 48 |
| A key binding (`input.contexts.gameplay.MoveLeft`) | `["Key.A", "Key.Left"]` | `["Key.J", "Key.Left"]` | **Yes** — `A` stops moving the player, `J` moves it; the binding count log line still reads `input context 'gameplay': 8 action(s)` |

Log line from the changed run:

```
[    0.390] [   Info] [Platform    ] (t:5fb4) Window created: 1024x576 "Engine2D" (renderer: direct3d11)
[    0.390] [   Info] [Input       ] (t:5fb4) input context 'gameplay': 8 action(s)
```

**How I know I did not rebuild between runs.** The executable's modification
timestamp was recorded before and after:

```
exe LastWriteTime before: 08/28/2026 19:51:50
exe LastWriteTime after : 08/28/2026 19:51:50
rebuilt? False
```

Identical to the second — the binary on disk is byte-for-byte the one that ran
the previous configuration. The Week 1 hardcoded resolution is genuinely gone.

*(The config was restored afterwards; the run following the restore reports
`1280x720` again.)*

---

## 3. Contexts

- **Key:** `Escape`
- In **`gameplay`** it produces: **`Pause`**
- In **`menu`** it produces: **`Back`**

`W`/`Up` is a second example: `MoveUp` in gameplay, `NavigateUp` in menu.

**How the active context is switched.** `InputMap::PushContext` /
`PopContext`. The engine pushes `gameplay` during the Input subsystem's `Init`,
so it sits at the bottom of the stack for the whole session; a pause menu pushes
`menu` on top of it and pops it on close.

**Stack or single active context?** **A stack**, and the reason is the pause menu
specifically: it wants menu bindings on top **with gameplay still underneath**,
not instead of it. A single active context forces you to re-declare every
binding you still want in the menu context, and then to keep the two copies in
sync forever.

**What happens to gameplay bindings while a menu is open** — the resolution rule,
stated explicitly because "input works in menus but also fires in gameplay" is
the bug that comes from leaving this vague:

> A physical key is resolved against contexts **from the top of the stack
> downward**, and the **first** context that binds that key **wins**. Lower
> contexts do not also see it.

So a context **shadows** the ones below it for the keys it binds, and is
**transparent** for the keys it does not. With `[gameplay, menu]` on the stack:

- `Escape` is bound in both → only the menu's `Back` fires. Gameplay's `Pause`
  does not, which is what stops Escape simultaneously closing and re-opening the
  menu.
- `W` is bound in both → only `NavigateUp` fires.
- A key bound only in gameplay still reaches gameplay.

`InputMap::FindOwningAction` is that paragraph as code — it walks
`g_stack.rbegin()` to `rend()` and returns on the first match.

---

## 4. No hidden allocation in update paths

**600-frame run**, Release, `sandbox --stress 600` — which is the harshest
version of this test available, because it spawns and destroys an entity every
single frame on top of the ordinary update.

**After warm-up (frame 1):**

```
frame 1: 22 entities, 0 allocator bytes
```

**After 600 frames:**

```
frame 301: 26 entities, 0 allocator bytes
frame 600: 26 entities, 0 allocator bytes (started at 22 / 0)
peak allocator bytes 168
resource refcount 21 (started at 21)
FrameStack peak was 168 of 1048576 bytes
EntityPool peak was 0 of 4096 blocks
```

**Allocation count delta: 0 net bytes over 600 frames.** Peak **168 bytes**,
identical at frame 1 and frame 600.

Reading those numbers honestly:

- **Current bytes read 0 at the sampling point** because the frame stack is
  rewound at the end of the render pass — that is the sawtooth sampled at its
  trough, not an unused allocator.
- **Peak is the number that would climb if anything were leaking**, and it does
  not: 168 bytes at frame 1, 168 bytes at frame 600. 168 = 21 sprites × 8 bytes,
  the sort index array, which is the same size every frame because the sprite
  count is stable.
- **Resource refcount is flat at 21** across 600 frames of spawn/destroy — a
  leaked component would show up here as a texture nobody released.
- **Entity count is stable at 26** (22 from the scene plus a steady-state lag of
  4 in the spawn/destroy pipeline), not climbing.

**What is doing the not-allocating**, since "no allocation" is a claim about
mechanism rather than a number:

- `EventPump` reserves its vectors once and `clear()`s them, which keeps
  capacity — so after the first frame that sees N events, no later frame
  allocates.
- `DebugDraw`'s command vector, the same.
- `SpriteRenderSystem`'s sort array comes from the **frame stack**, which is a
  pointer add and a marker rewind.
- The log threshold is checked **before** `std::format` runs, so a suppressed
  message never allocates the string it was not going to print.

### The allocator-aware container — demoted to stretch, and not done

The Week 8 lab says explicitly: *"Demote the allocator-aware container to
stretch. Keep string IDs."* That call was made, and this is the honest record of
it.

**It was not built.** What was built instead is the specific thing it would have
been used for — `SpriteRenderSystem` allocating from the frame stack directly —
which gets the measurable benefit without a templated container and without the
template-instantiation error messages the lecture warned about.

The size of the un-solved problem, since knowing it is worth something even
un-solved: the remaining per-frame heap traffic is `std::string` construction
inside `LogRecord` for surviving log lines (zero per frame at `Info` in a steady
state, because the update path logs nothing) and `std::function` copies in
`MessageBus` dispatch (zero per frame with no collisions; one small allocation
per collision event pair otherwise). Neither is in the update path of a quiet
frame, which is what the 600-frame flat reading demonstrates.

---

## 5. String IDs

- **Engine identifiers converted to `StringId`:** component type names (4:
  Transform, Sprite, AABBCollider, CircleCollider), message types (3: enter,
  stay, exit), input action names (13 across two contexts), input context names
  (2), CVar names (11), entity names (every entity in every scene — 22 in
  `orbit_test`, 14 in `collector`). **Roughly 70 in a running engine**, and the
  count grows with the scene rather than with the code.
- **Hash width: 64-bit**, and the reasoning is a birthday-bound argument rather
  than a preference. At 32 bits a collision becomes likely at about **77,000**
  distinct strings — which sounds comfortable until you notice every entity name
  in every scene file is one, and Phase 2 scenes are not small. At 64 bits the
  same threshold is around **five billion**. The cost is four bytes per id, and
  an id is never the thing stored in the thousands (components are, and they
  hold at most one each).
- **Does the compile-time `static_assert` pass?** **Yes.** Both provided
  assertions compile, plus one added for the `_sid` literal:
  ```cpp
  static_assert(StringId("Player").Value() != 0);
  static_assert(StringId("Player").Value() == StringId("Player").Value());
  static_assert(StringId("Player").Value() != StringId("Enemy").Value());
  static_assert("Player"_sid.Value() == StringId("Player").Value());
  ```
  The literal operator is **`consteval`**, not `constexpr`, so a stray runtime
  use is a **compile error** rather than a silent per-frame hash.

  The property that makes this compile is that the **constructor never touches
  the reverse-lookup table** — `Intern()` is a separate, explicit step. That
  separation is the single design decision that makes compile-time hashing
  possible, and it is why `Intern` exists at all rather than the constructor
  doing the registration.

- **What `ToString()` returns in a release build:** `"<sid:8f3a19c4b7e02d51>"` —
  the hash, formatted as hex. **Not an empty string.** A log line reading
  `component ''` sends you hunting for a data bug that does not exist, whereas
  `component '<sid:...>'` says "release build, the name was compiled away" to
  anyone who has read the comment once. The formatting buffer is a small
  `thread_local` ring of four, because two `ToString()` calls in one
  `std::format` argument list overwrote each other exactly once and it took ten
  minutes to believe.

---

## 6. One paragraph — would I call my own config loader again next week?

**Yes, and Week 9 is the proof rather than the promise.**

The Week 8 note said to solve file reading and parsing *once*, behind an
interface you would be willing to call again, so that Week 9 only has to solve a
schema. What that produced was `ConfigDocument` + `ConfigNode`: a document you
load from a **virtual path** through `FileSystem`, and a lightweight non-owning
node view with `Child()`, `At()`, `Size()`, `Keys()`, and typed readers that each
take a fallback and **warn by path** on a type mismatch.

`Scene::Load` uses exactly that interface and mentions **no parser at all** —
`ConfigDocument m_document; m_document.LoadFromVirtualPath(...)`, then
`root.Child("entities").At(i).Child("components")`. Every component's
`Deserialize(const ConfigNode&, std::string& outError)` is written against the
same two types. Week 9 was a schema, not a subsystem, which is what was promised.

Two things made that work, and both were decisions rather than luck. First,
**no JSON type appears in any public header** — `nlohmann_json` is linked
`PRIVATE` and hidden behind a pimpl, so swapping the parser means editing
`Config.cpp` and nothing else. Second, **`ConfigNode::Path()` carries the dotted
path from the root**, which is why a scene error can say
`entities[7].components[1].texture must be a string (a virtual asset path)`
rather than "parse error". The Week 9 lab says a good message there is worth an
hour a week; that accessor is the entire reason it is possible, and it cost one
`std::string` member.

The one thing I would change if starting over: `ConfigNode` holds a `std::string`
path, so constructing a child node allocates. That is irrelevant at load time and
would matter if anything read config per-frame — nothing does, because that is
what CVars are for.

---

## Stretch goals

- **1. Live CVar mutation with immediate effect — done**, and it is the CVar
  panel rather than a debug key. Every value is read fresh in
  `Engine::BeginFrame` rather than cached, so an edit takes effect on the next
  frame with no notification machinery. That is the design decision that makes
  the panel worth having.
- **4. Bindings written back to the config file — half done.** The CVar panel's
  **Save** button does the full round trip for CVars (read the file as it is on
  disk, replace only the modified entries, write it back — so comments and keys
  this build does not know about survive). A rebinding *screen* was not built;
  bindings are edited in the file.
- **2. Gamepad — not done**, and the hook is explicit rather than absent.
  `InputMap::ParseBinding` recognises the `Gamepad.` prefix and returns a
  no-device binding **quietly and deliberately**, because the shipped example
  config contains gamepad bindings and filling the log with warnings about a
  correct file would be worse than useless. Adding gamepads means filling in
  that one branch and touching nothing above it — which is the test of whether
  the abstraction was worth building.
- **3. Analog response curves — not done.** The radial dead zone is implemented
  and the rescale is there; a configurable curve is a Phase 2 tunable.
