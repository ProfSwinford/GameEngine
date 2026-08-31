# Engine2D

A 2D game engine, built one week at a time, following
*Game Engine Architecture* 4th ed. Vol. I (Gregory). Phase 1, Weeks 1–10.

## Build

Requires CMake 3.28+, a C++23 compiler, and Git. Nothing else.
SDL3, Dear ImGui, doctest and nlohmann/json are downloaded and built
automatically the first time you configure.

```
cmake --preset debug
cmake --build --preset debug
```

Executables land in `build/debug/bin/`.

> The first configure takes several minutes because it is compiling SDL from
> source. Subsequent configures are fast. **It is not hung** — watch the
> progress output.

### Visual Studio 2026 — the recommended way on Windows

**File → Open → Folder** on the repository root. VS reads `CMakePresets.json`
directly, offers `debug` / `release` / `strict` in the configuration dropdown,
and manages `build/` itself. Nothing else is needed.

**Pick one owner for `build/`.** VS configures into `build/<presetName>`, which
is the same path the command line uses. If VS has the folder open and you also
run `cmake --build` from a terminal, the two will fight over the same CMake
cache — each reconfigures with its own generator and invalidates the other's,
which shows up as spontaneous full rebuilds and "is not a directory" errors.
Use the IDE *or* the terminal for a given tree, or point the terminal somewhere
else with `cmake -S . -B ../my-build`.

### Command line on Windows

**Use a "Developer PowerShell for VS" / Developer Command Prompt, not a plain
shell.** The presets deliberately do not pin a generator, so the same file works
on Windows, Linux and macOS — which means CMake picks the platform default, and
on a current VS install that is **Ninja**. Ninja invokes `cl.exe` directly and
does not set up the MSVC environment the way MSBuild does, so a plain PowerShell
gets:

```
fatal error C1083: Cannot open include file: 'cstddef': No such file or directory
```

That looks like a broken toolchain and is not one — `INCLUDE` is simply unset.

### Running from a build directory outside the tree

The asset root is found by walking up from the executable looking for `assets/`,
which assumes the executable lives inside the repository. When it does not — a
build directory elsewhere, CI, a copied binary — set:

```
ENGINE_ASSET_ROOT=/path/to/GameEngineRepo
```

It names the directory *containing* `assets/`, is checked before the search, and
is logged at boot so there is never any doubt which assets were loaded.

Run the tests:

```
ctest --preset debug
```

## Targets

| Target | What it is |
|---|---|
| `engine` | The static library. Knows nothing about any game or any tool. |
| `sandbox` | The standalone runtime. No ImGui. What the Week 10 gate is built in. |
| `editor` | The IDE — a Scene view, a Game view, and thirteen dockable panels. |
| `gamescripts` | Script behaviours. Linked by both `sandbox` and `editor`. |
| `tests` | 126 doctest cases. |

## Seeing it work

```
sandbox                 # the orbiting three-deep hierarchy from a scene file
sandbox --game          # the Week 10 gate game: WASD/arrows, collect ten, 60s
sandbox --autoplay      # the same game, played to completion by the AI hook
editor                  # the IDE: a Scene view to edit in, a Game view to play in
```

`--game` is **Spec A, Collector**: move with WASD or the arrow keys, touch the
ten red squares before the timer runs out. The counter and clock are drawn with
the Week 6 debug text; `--autoplay` finishes a round in about twelve seconds by
injecting the same named actions the keyboard is bound to, so it exercises the
real input path rather than bypassing it.

## The Scene view and the Game view

The editor is arranged the way Unity's is: **two views of the same world**,
tabbed together in the centre of the dockspace.

| | Scene | Game |
|---|---|---|
| Camera | the editor's own — pan and zoom freely | the **game** camera, what a player sees |
| Debug draw | grid, origin axes, collider outlines, selection | none |
| Gizmos | translate handles on the selection | none |
| Purpose | look at what is *there* | look at what *ships* |

Each renders into its own off-screen `RenderTarget`, which is what makes two
simultaneous views possible at all — drawing straight to the back buffer can
only ever produce one.

The editor starts in **edit mode**: the clock is paused and nothing simulates
until you press Play.

## Editing a scene

Open `editor`, then **File → Open Scene** to pick one (the list is discovered
from `assets/scenes/`, so dropping a `.json` in there is enough).

| Do this | Where |
|---|---|
| Pan | middle- or right-drag in the Scene view |
| Zoom | wheel in the Scene view — **toward the cursor** |
| Select | click it in the Scene view, or in the Hierarchy |
| Frame the selection | **F** with the mouse over the Scene view |
| Move it | drag the gizmo — the red X arm, the green Y arm, or the centre square for both |
| Create an entity | drag a texture from **Assets** into the Scene view, or Hierarchy → **+ Create Entity** |
| Give it a sprite | drag a texture onto it in the Hierarchy or the Inspector |
| Give it behaviour | drag a `.cpp` from **Assets** onto it in the Hierarchy or the Inspector |
| Rename / Duplicate / Destroy | **right-click** an entity in the Hierarchy |
| Edit transform, sprite, collider, spin, script | Inspector — **pause first**, transform fields are read-only while running |
| Add a component | Inspector → the `+ TypeName` buttons at the bottom |
| Save | **File → Save Scene**, or **Ctrl+S**. **Save Scene As…** for a new path |

The menu bar shows **UNSAVED** once anything is edited, and the save writes the
same keys the loader reads — `sandbox --save-check` verifies that
`load → save → load` is a fixed point rather than trusting it.

**Two things it will not do.** Destroy goes through the deferred queue (the IDE
is not exempt from the engine's rules), so an entity disappears at the end of
the tick rather than instantly. And **there is no undo** — save before you
experiment.

**What is still missing**, stated plainly rather than left to be discovered:
no rotate or scale gizmo (translate is the only one), no drag-to-reparent in
the Hierarchy, and the Assets panel reads and creates but does not rename,
move or delete.

## Assets and scripts

The **Assets** panel browses files on disk, across two roots:

| | |
|---|---|
| `assets/` | shipped data — textures, scenes, loaded at runtime by virtual path |
| `gamescripts/` | C++ **source** the build compiles. Never shipped, never loaded |

Keeping them separate is deliberate: a browser that showed scripts inside
`assets/` would be teaching that source is data, and the day someone writes a
packaging step that mistake ships the game's source with the game.

**Assets → + Script** writes `gamescripts/<Name>.cpp` from a template with
every lifecycle hook stubbed and commented. Drag that `.cpp` onto an entity to
attach it.

**Scripts are compiled C++, not an interpreted file.** Creating one writes the
file and reconfigures nothing — it runs after a rebuild. The rest of the
workflow still works in the meantime, because **the binding is by name**: a
script that is not compiled into this build still attaches, still saves into
the scene, and shows as `UNRESOLVED` in red in the Inspector. Rebuild, relaunch,
and the same scene file produces a live behaviour with nothing reattached.

```cpp
class MyScript final : public eng::ScriptBehaviour {
    void OnStart() override {}                       // first tick after attach
    void OnUpdate(eng::f32 dt) override {}            // every FIXED step, stage 200
    void OnDestroy() override {}                      // entity going away
    void OnCollisionEnter(eng::EntityHandle o) override {}
};
ENGINE_REGISTER_SCRIPT(MyScript)                      // without this it can never be found
```

`gamescripts/Orbiter.cpp` is the worked example. `sandbox --script-check`
verifies the whole contract, including that an uncompiled script attaches
without crashing and round-trips through a save.

The boot log prints how many scripts linked in — a zero there when
`gamescripts/` is not empty means the whole-archive link is broken, which is
otherwise invisible.

## Playtesting

Press **Play**. The Game view comes forward and takes the keyboard — a green
border says it has it, and clicking any other panel gives it back, so a CVar
can be tuned mid-play without stopping.

**Play is non-destructive.** Pressing it snapshots the scene, and **Stop**
restores that snapshot — a play session that moved the player and destroyed
half the pickups leaves the authored scene exactly as it was. If the scene
contains a component that cannot be serialised, Play is *refused* rather than
entered, because a snapshot that cannot restore is worse than no play button.
`sandbox --playmode-check` verifies the whole contract headlessly, including
that the scene really did change in between.

Play / Pause / Step and the time-scale slider drive the real fixed timestep,
and engine systems (spin, collision, messaging, deferred ops, **scripts**) all
run live — `gamescripts/` is linked into the editor as well as the sandbox, so
a script attached to an entity really executes in the Game view.

The one thing it does **not** run is `CollectorGame`, which lives in the
`sandbox` target that the editor does not link. To play the gate game, run the
sandbox directly — no rebuild needed, and no Visual Studio:

```
sandbox --game
```

Scene edits and CVar changes take effect on the next launch with no rebuild.
Changing gameplay C++ still needs a build.

Run `sandbox --help` for the measurement and verification modes
(`--sizeof-audit`, `--layout-bench`, `--os-measure`, `--random-check`,
`--motion-check`, `--save-check`, `--m3-check`, `--collision-check`,
`--playmode-check`, `--script-check`, `--stress`, `--fail-subsystem`). Each one
produces the evidence pasted into the
corresponding document in `docs/`. `--frames N` caps any run so it exits
cleanly, which is what makes those runs scriptable.

## Layout

| Path | What lives here |
|---|---|
| `engine/` | The engine static library. |
| `sandbox/` | The standalone game runtime. The only place game-shaped code is allowed. |
| `editor/` | The IDE. Panels live here. |
| `gamescripts/` | Script behaviours. Linked by **both** `sandbox` and `editor`, so a script runs in the Game view. The one place the build globs. |
| `tests/` | Unit tests. |
| `cmake/` | Build system modules. |
| `scripts/` | Shell tooling (`fresh-clone-check.sh`). Not game scripts — those are `gamescripts/`. |
| `config/` | `engine.json` is read at boot; `engine.example.json` is the documented reference. |
| `assets/` | Scenes and textures, loaded at runtime. **Committed** — see `.gitignore`. |
| `docs/` | Written deliverables — measurement tables, reports, milestone evidence. |

## Architecture notes

*(Week 1 deliverable, rewritten at the end of Week 10. The Week 1 version is
preserved below it, because the comparison is the point.)*

### As of Week 10

Against the layered runtime architecture diagram in Ch. 1.5, this engine now has
a recognisable slice of the lower and middle layers, and essentially nothing
above them.

**Solid:** the *Platform Independence Layer* (`platform/` — SDL is behind an
interface that mentions no SDL type, enforced by a `PRIVATE` link rather than by
discipline), the *Core Systems* layer (assertions, logging with named channels
and multiple sinks, a timer registry, hashed string ids, a config and CVar
system, RAII wrappers, a job system, and the stack and pool allocators from
Ch. 6.2), and *Resource Management* (Ch. 7 — a handle-based, reference-counted
manager with generation-checked stale detection). The *Game Object Model* exists
as composition rather than inheritance: entities are handles, components
register themselves with the systems that update them, and the set of components
on an entity is data.

**Partial:** *Low-Level Renderer* is `SDL_Renderer` plus a sprite pass and a
debug-draw queue — there is no material system, no shader abstraction, no
render-target management. *Collision & Physics* is detection only: overlap tests,
layers, masks and enter/stay/exit events, with **no resolution and no dynamics**.

**Empty:** audio, animation, AI, networking, scripting, particles, front-end/HUD
beyond debug text, and the whole *Gameplay Foundations* layer above the object
model. The C# gameplay layer that Phase 2 adds has been designed *for* —
components are addressable by string id and entities by integer handle, so
nothing crosses the boundary that a garbage collector could move — but none of
it is built.

The thing that changed most between Week 1 and now is not the number of boxes
filled in; it is that the **boundaries between them are enforced by the build**
rather than by intention. SDL is `PRIVATE`, so `sandbox` and `editor`
*cannot* reach it. JSON is `PRIVATE`, so no public header knows what parses a
scene file. The Week 10 gate — implementing an unseen game spec in `sandbox`
with `git diff --stat HEAD -- engine/` showing zero changes — is the test of
whether that boundary is real, and it passed.

### As of Week 1 *(preserved, unedited)*

> Almost nothing exists yet. Of the Ch. 1.5 layers, only the very bottom is
> present: a *Platform Independence Layer* consisting of one `Window` class that
> holds two raw SDL pointers, and a *Core Systems* layer consisting of a header
> of fixed-width type aliases. Everything above — resources, the renderer, the
> game object model, collision, gameplay foundations — is empty, and the
> `sandbox` target contains a loop that polls, clears and presents and nothing
> else.
>
> The one architectural decision that has been made is the split into an
> `engine` library and a `sandbox` executable, which currently costs more than
> it buys.

## Milestones

| | Week | Evidence |
|---|---|---|
| M0 | 3 | [docs/week03-shutdown-log.md](docs/week03-shutdown-log.md) |
| M1 | 6 | [docs/week06-milestone1.md](docs/week06-milestone1.md) |
| M2 | 7 | [docs/week07-milestone2.md](docs/week07-milestone2.md) |
| M3 | 9 | [docs/week09-milestone3.md](docs/week09-milestone3.md) |
| M4 | 10 | [docs/week10-milestone4.md](docs/week10-milestone4.md) |

Post-Week-10 editor work is written up in two documents:

- [docs/editor-scene-and-game-views.md](docs/editor-scene-and-game-views.md) —
  the render-target split, the Play/Stop snapshot contract, and the two focus
  bugs that a docked background tab produces.
- [docs/editor-assets-and-scripts.md](docs/editor-assets-and-scripts.md) —
  the asset browser, drag-and-drop, the by-name script binding, and the gizmo
  bug that came from guarding against state produced later in the same frame.

## Conventions worth knowing before reading the code

Three decisions are recorded at the top of the file that owns them, because each
is the kind that costs an afternoon when it lives in someone's head:

- **Matrices** — row-major storage, **row vectors** (`v * M`), so "apply A then
  B" is written `A * B` and translation lives in the bottom row.
  `engine/include/engine/math/Mat3.h`.
- **Overlap** — **touching counts as overlapping**, consistently, in all four
  shape combinations and in `Contains`.
  `engine/include/engine/math/Overlap.h`.
- **Collision layers** — a pair is tested only if **both** masks include the
  other's layer, so events always come in pairs.
  `engine/include/engine/physics/Collider.h`.

World space is **y-up**; the screen is y-down. The single place those are
reconciled is `Camera::ViewMatrix`.
