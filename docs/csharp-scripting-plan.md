# C# as a second scripting language — a plan to review

**Status: proposal. Nothing here is built.** This is the written plan asked for
before any code, so that the shape of the thing can be argued with while it is
still cheap to change.

The goal is C# **alongside** C++, not instead of it. A `.cs` script and a `.cpp`
script would attach to an entity the same way, appear in the Inspector the same
way, and save into the same `.oscene`.

---

## 1. What this is worth, honestly

Two reasons were given for wanting it, and they are not equally strong any more.

**"For its own sake" — this is the real one.** A managed language for game logic
is a genuine quality-of-life change: no undefined behaviour in gameplay code, an
exception instead of a crash, `List<T>` and LINQ when you want them, and a script
that cannot take the editor down with it. That stands on its own merits and no
analysis here argues against it.

**"It enables variables in the Inspector" — this reason has mostly gone.** When
it was raised, exposing a script's variables needed `OnExpose` written by hand.
It no longer does: a public variable of a supported type now reaches the
Inspector on its own, via a build-step parse of the C++ source
(`engine/tools/ScriptVars.h`). C# would do the same job **better** — real
reflection instead of a whitelist parser, attributes instead of comment markers,
and no declaration shape it can fail to recognise — but it is now an improvement
to something that works, not the unlock for something that does not.

That is worth being clear about, because it changes what the project is buying:
a nicer scripting language, not a missing feature.

### What it costs

| | |
| --- | --- |
| **The binding layer** | The dominant cost, and the only one that never ends. Every engine function a script may call needs a native entry point and a C# wrapper — and so does every function added afterwards, forever. |
| **A second toolchain** | The .NET SDK to compile scripts, and a runtime to host them. Already present here (§2), but a new hard dependency for anyone else cloning the repo. |
| **Two of everything** | Two script templates, two sets of documentation, two places a gameplay bug can live, two debuggers. |
| **Shipping size** | A framework-dependent game needs .NET installed on the player's machine; self-contained adds roughly 70 MB to an export. |

### What it does *not* cost

The existing design absorbs it better than might be expected:

- **`ScriptComponent` is keyed by NAME, not by type.** A component stores
  `"PlayerController"` and looks it up in a registry. A second registry for
  managed scripts slots in beside the first with no change to the scene format.
- **`ScriptFields` is already language-agnostic.** It is a list of
  `{name, type, pointer, tooltip, range, requiredType}`. Reflection fills the
  same list the C++ parser fills, and the Inspector code does not change at all.
- **The `"vars"` JSON works unchanged** — same keys, same shapes, same
  only-what-you-changed rule.
- **`EntityRef` stores a name**, so a reference set in a C# script and one set
  in a C++ script are the same thing in the file.

So this is additive. It does not require unpicking anything.

---

## 2. The toolchain, as it actually is on this machine

Checked rather than assumed:

```
SDKs            8.0.100, 8.0.418, 9.0.102, 10.0.400
Runtimes        Microsoft.NETCore.App up to 10.0.11
hostfxr         C:\Program Files\dotnet\host\fxr\{7.0.20, 8.0.0, 8.0.24, 9.0.1, 10.0.11}
Hosting pack    C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\10.0.11
                  runtimes\win-x64\native\nethost.h
                  runtimes\win-x64\native\hostfxr.h
                  runtimes\win-x64\native\coreclr_delegates.h
                  runtimes\win-x64\native\libnethost.lib
                  runtimes\win-x64\native\nethost.dll
```

**Everything native hosting needs is already installed.** No new download to
start, and the SDK that would compile the scripts is the same one already here.

---

## 3. Which runtime

| | CoreCLR via `nethost`/`hostfxr` | Mono |
| --- | --- | --- |
| Status | The supported way to host .NET today | Still maintained, but its embedding story is the one Unity grew out of |
| Already installed here | **Yes**, including the headers and import library | No — would need obtaining and shipping |
| Interop | Raw function pointers via `[UnmanagedCallersOnly]`; no marshalling layer | A rich C API (`mono_add_internal_call`, `mono_runtime_invoke`) |
| Hot reload | `AssemblyLoadContext(isCollectible: true)` | AppDomain unload |
| Tooling | Current SDK, Roslyn, VS/VS Code managed debugger | Workable, less current |

**Recommendation: CoreCLR.** It is already on the machine, its interop is
function pointers rather than an API to learn, and the tooling is alive. Mono's
advantage is a friendlier embedding API, which matters less when the interop
design (§4) is a flat function table either way.

Pin to **.NET 8** rather than 10 for the first pass — it is LTS, it is installed,
and a hosting bug on the newest runtime is not a problem worth having while the
foundation is being built. Moving up later is a version string.

---

## 4. The interop boundary

The whole design rests on four decisions. Each exists to avoid a specific
failure.

### Native → managed: `[UnmanagedCallersOnly]` function pointers

At startup: `get_hostfxr_path()` → `hostfxr_initialize_for_runtime_config()` →
`hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)` →
one call per managed entry point.

Entry points are `static` methods marked `[UnmanagedCallersOnly]`, which means
**no marshalling at all** — a raw address, called like any C function. The cost
is a discipline: blittable parameters only, and no exception may escape (each
entry point wraps its body in `try/catch` and reports through the log).

Perhaps six entry points in total: `Initialise`, `LoadAssembly`,
`CreateInstance`, `Tick`, `Destroy`, `DescribeFields`.

### Managed → native: a function table handed over once

Not `DllImport`. The host is an `.exe`, so name-based resolution from managed
code is awkward and fragile. Instead the native side fills a POD struct of
function pointers and passes it to `Initialise` once:

```c
struct EngineApi {
    void (*Log)(int channel, int level, const char* utf8);
    void (*TransformSetLocalPosition)(EntityId id, Vec2 p);
    Vec2 (*TransformWorldPosition)(EntityId id);
    float (*TimeFixedDelta)(void);
    /* ... */
};
```

Managed stores it and calls through. Explicit, fast, no name lookup, and
versionable — a mismatched table is one integer check at startup rather than a
crash on the first call.

### Identity: handles, never pointers

- **Entities** cross as `EntityId` — the existing `{index, generation}` pair, a
  blittable struct on both sides. Managed code never holds an `Entity*`. This is
  the same rule `EntityId.h` already argues for, and a GC that can move objects
  and a scene that can destroy them make it more important, not less.
- **Script instances** are held natively as an opaque `intptr` from a
  `GCHandle`. The component owns it and frees it on unbind — the one piece of
  manual lifetime management in the design, and the thing that would leak if it
  were forgotten.
- **Components** are reached by `(EntityId, type name)` through the table, not
  by address.

### Strings: not per frame

Crossing a string allocates. UTF-8 bytes in both directions, and nothing in
`OnFixedUpdate` should be building one. Names used repeatedly (component type
names, action names) get interned to an integer id at load.

---

## 5. The binding surface — the long pole

**The mistake to avoid is binding the engine up front.** The minimum for a
script that actually does something is around thirty functions:

| Area | Functions |
| --- | --- |
| Log | `Log` |
| Time | `DeltaTime`, `FixedDeltaTime`, `UnscaledDeltaTime` |
| Transform | get/set local position, rotation, scale; world position; `SetWorldPosition` |
| Entity | `Name`, `Id`, `FindComponent(typeName)`, `IsAlive` |
| Scene | `Find(name)`, `CreateEntity`, `QueueDestroy` |
| Input | `GetAxis`, `GetAxis2D`, `IsDown`, `WasPressed` |
| Rigidbody2D | get/set velocity, `AddForce`, `SetFreezeRotation` |
| Physics2D | `Raycast` |
| Sprite | texture path, tint, layer |

That set is enough to write `PlayerMovement`, `Orbiter`, `TilemapWalker` and
`CameraFollow` in C#, which is the honest test of whether the language is usable
here. **Everything else is demand-driven** — bound when a script needs it, not
in anticipation.

UI, tilemap, gizmos, messaging and the collider shapes are all deliberately out
of the first pass.

---

## 6. Lifecycle and hot reload

C# is where the current script model gets *simpler*, not more complicated.

**Hooks by reflection.** `type.GetMethod("OnFixedUpdate")` replaces
`MakeScriptHooks` and its hundred lines of concepts. Done once per type at load,
cached in the same `ScriptHooks`-shaped table, so the existing optimisation
survives untouched: a script with no update is still in no update list.

The compile-time error messages in `ENGINE_REGISTER_SCRIPT` — wrong signature,
Unity's spelling, the old `OnUpdate(float)` — become load-time ones. Slightly
later than the C++ version catches them, and reported the same way.

**Hot reload is better than the C++ story.** A collectible
`AssemblyLoadContext`, unloaded and recreated on rebuild:

1. `UnbindForReload` on every component — frees the `GCHandle`s.
2. `alc.Unload()`, then wait for collection.
3. Recompile with `dotnet build`.
4. New ALC, load, `RebindAfterReload`.

Steps 1 and 4 are **the methods that already exist** for the C++ DLL swap. And
there is no CRT to match — which is precisely the thing that segfaults the Debug
editor today when it loads a Release-built `userContent.dll`.

**Compiling** is `dotnet build` on a generated `.csproj` under
`assets/scripts-cs/`. The editor already shells out to a compiler and already
reports its output; this is the same path with a different command. Embedding
Roslyn would be faster and is not worth it yet.

---

## 7. The Inspector, by reflection

This is the part that is straightforwardly nicer than the C++ route.

```csharp
public class Orbiter : ScriptBehaviour {
    public float radius = 90f;                    // tooltip from [Tooltip]
    [Range(0, 10)] public float speed = 1.2f;
    [Tooltip("what to circle")] public EntityRef centre;
    [HideInInspector] public float notShown;

    void OnFixedUpdate() { /* ... */ }
}
```

`type.GetFields(Public | Instance)` gives the list with no parser, no whitelist
and nothing to misread. Attributes replace the comment markers C++ needs:

| C++ today | C# |
| --- | --- |
| `// [0..10] how fast` | `[Range(0, 10)] [Tooltip("how fast")]` |
| `// [Rigidbody2DComponent] what to push` | `[RequireComponent("Rigidbody2DComponent")]` |
| `private` or `m_` prefix | `private`, or `[HideInInspector]` |
| `[SerializeField]` has no equivalent | expose a private field deliberately |

Values are pushed into the **existing** `ScriptFields` list through the table, so
the Inspector, the `"vars"` serialisation, the override/revert logic and the
rename fix-up all work with no changes. One field per call at bind time, not per
frame.

---

## 8. Stages, each with a stopping point

Ordered so that the riskiest unknown is settled first and each stage is worth
having on its own.

| # | Stage | Done when |
| --- | --- | --- |
| 0 | Spike: host the runtime from a scratch `.exe`, call one managed method, call back | A number printed from C# through the engine's log, and **the per-call cost measured** |
| 1 | `CSharpSubsystem` in the engine: init, shutdown, load an assembly, in the subsystem list | `editor --frames 60` brings the runtime up and down cleanly |
| 2 | The function table, blittable `Vec2`/`EntityId`, `Log` + `Time` bound | A C# method logs the fixed delta every step |
| 3 | Managed `ScriptBehaviour`, hooks by reflection, `CSharpScriptComponent` | A `.cs` script attaches from the Inspector, runs, and shows UNRESOLVED in red when it is not compiled in |
| 4 | Transform, Entity, Scene, Input, Rigidbody2D, Physics2D bound (§5) | `Orbiter.cs` and `TilemapWalker.cs` behave the same as their C++ twins |
| 5 | Inspector by reflection into `ScriptFields`, attributes, `"vars"` round-trip | A C# script's fields edit, save, reload and revert exactly as a C++ script's do |
| 6 | Hot reload via collectible ALC | Edit a `.cs`, alt-tab, and the change is live with the scene intact |
| 7 | Export packaging; demand-driven bindings | An exported game runs its C# scripts on a machine with no SDK |

**Stage 0 is the one that decides whether to continue.** If the per-call cost or
the hosting ergonomics are bad, that is the moment to find out — before any
binding exists to throw away.

---

## 9. Risks, and what would make me stop

- **Per-call overhead.** `[UnmanagedCallersOnly]` should be a handful of
  nanoseconds, but it is measured at stage 0 rather than assumed. A script doing
  twenty engine calls per step across fifty entities is 60 000 crossings a
  second; if that is not comfortably free, the design needs batching and the
  plan needs revisiting.
- **GC pauses.** Gen0 collections on a workstation GC are sub-millisecond, which
  is fine for this engine — *provided* gameplay code does not allocate per
  frame. That is a documentation and review problem, not a technical one, but it
  is a real one.
- **Binding drift.** Two languages reaching the same engine is two chances for
  them to disagree. Mitigation: the C# wrappers are thin and mechanical, and
  anything with real logic stays in the engine where both call into it.
- **Scope.** This is weeks, not days, and most of it is stages 4 and 7. Stopping
  after stage 5 would leave something genuinely usable in the editor but not
  shippable.

---

## 10. Explicitly not in this plan

- Replacing C++ scripting, or deprecating it.
- Binding the whole engine API.
- Visual scripting, or a REPL.
- NativeAOT for the managed side (it would rule out reflection and hot reload,
  which are the two reasons for doing this).
- Any change to the `.oscene` format.

---

## 11. Questions worth answering before stage 1

1. **Framework-dependent or self-contained exports?** Self-contained costs ~70 MB
   and asks nothing of the player; framework-dependent is small and requires
   .NET on their machine. Affects stage 7 only, but it is better decided early.
2. **Where do C# scripts live?** `assets/scripts-cs/`, or mixed into
   `assets/scripts/` beside the `.cpp` files? Mixed reads better in the Assets
   panel; separate is simpler for the `.csproj`.
3. **One assembly or one per folder?** One is simpler and reloads as a unit.
4. **Is stage 0's measurement a gate?** That is, if the per-call cost is
   disappointing, is that a reason to stop — or is the language worth it anyway?
