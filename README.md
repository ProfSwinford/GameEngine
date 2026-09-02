# Engine2D — Editor Base

The starting point for the course. You get **a finished editor**, **a finished
game runner**, and **an engine that is mostly empty** — and you fill the engine
in, one file at a time, watching the editor come to life as you go.

It is deliberately readable. Every file starts with a comment explaining what it
does and why it is built that way, and there is nothing clever in it that does
not earn its place.

---

## Start here: build it and run it

You need **CMake 3.28 or newer**, a **C++23 compiler**, and **Git**. Nothing
else — SDL3, Dear ImGui, nlohmann/json and doctest are downloaded and built
automatically the first time you configure.

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

```bash
build/debug/bin/editor
```

**The editor opens.** The menus work, the panels are there, the Console shows
the engine starting up. The Console also says this:

```
the starting scene 'scenes/orbit_test.json' did not load:
Scene::Load is not implemented yet - this is a shell of the engine
```

That is not a bug. That is your first assignment.

> The first configure takes several minutes, because it is compiling SDL from
> source. **It is not stuck** — watch the progress messages.

---

## What is given, and what is yours

| Given to you, finished | Yours to write |
| --- | --- |
| The whole **editor** — panels, docking, Play/Pause/Step, the asset browser, the script compiler | |
| The whole **sandbox** — the game running without the editor | |
| **`engine/include/`** — every header, complete. This is the interface, and it does not change | |
| `Engine.cpp` — start-up order and the frame loop | |
| `core/` — the log, settings, the clock, JSON, subsystem ordering | |
| `fs/`, `platform/`, `render/Renderer.cpp` — files, the window, drawing | |
| | **`scene/`** — entities, components, scenes, messaging, deferred create/destroy, system order, scripts |
| | **`physics/Collider.cpp`** — layers, overlap events |
| | **`math/`** — `Mat3`, `Transform2D`, `Overlap`, `Random` |
| | **`input/InputMap.cpp`** — key presses into named actions |
| | **`render/Camera.cpp`**, **`render/Gizmos.cpp`** |
| | **`resource/ResourceManager.cpp`** — loading images |

The parts you are given are the ones that make a window appear. You cannot see
the effect of writing `Scene::Load` if there is nothing to see it in.

---

## How a shell file works

Open `engine/src/scene/Scene.cpp`. Every function that belongs there is already
present, with the right name and the right arguments — it just does nothing yet:

```cpp
// TODO: the slot-and-generation scheme described in Scene.h.
EntityId Scene::CreateEntity(std::string_view /*name*/) { return EntityId{}; }
```

So the whole project always compiles, links and runs. Nothing is ever in a
half-broken state where you cannot try it out.

**The header is the specification.** `Scene.h` explains what every one of those
functions is for and why it exists, in far more detail than the `.cpp` does.
Read the header first, then fill in the body.

You never create or delete a file to make this work — but if you want to, you
can. See below.

---

## You never have to touch CMake

The build finds source files by looking, not by being told:

- every `.cpp` under `engine/src/`, at any depth
- every `.cpp` under `editor/src/` and `sandbox/src/`

Add a file, rename one, move one into a new folder, delete one — the next build
picks it up. There is no list to keep in step, on purpose: the thing being
learned here is the engine, and stopping to maintain a build script teaches
nothing about either.

Two things worth knowing:

- **CMake never fails, even with an empty `engine/src/`.** It says
  `engine: no sources in src/ yet - building an empty library` and generates a
  placeholder so the library still exists. Configuring is never the thing that
  is broken.
- **But the editor needs the engine to link.** Delete a shell file rather than
  filling it in and the project still *compiles*; it fails at the **link**
  step, naming exactly which functions are missing — `unresolved external
  symbol ... eng::Scene::Load ...`. That is a linker error, not a build-script
  error. Put the file back, or write the functions it named.

So: keep the shell files and fill them in. They exist precisely so that the
project always gets as far as running.

---

## The tests are your checklist

```bash
ctest --preset debug
```

Right now that fails, and the failures are the point:

```
[doctest] test cases: 59 | 32 passed | 27 failed
```

Each failing test is a piece of the engine that has not been written yet. Run
the test file for whatever you are working on, make it pass, move on. When all
59 pass, the engine is finished.

```bash
build/debug/bin/tests --test-case="*Transform*"
```

---

## Where everything is

| Folder | What lives there |
| --- | --- |
| `engine/include/` | The interface. Complete, and the best documentation in the project. |
| `engine/src/` | The implementations. Mostly shells — this is your work. |
| `editor/` | The development environment. Complete. |
| `sandbox/` | The game running on its own. Complete. |
| `tests/` | The checklist. |
| `assets/` | Scenes, images **and your scripts**, in whatever folders suit the game. |
| `config/` | `engine.json` is read at start-up; `engine.example.json` documents every setting. |
| `docs/` | The editor guide and the engine tour. |
| `.build/` | Where the editor compiles your scripts to. Generated; not committed. |

---

## Where to start reading

[docs/engine-tour.md](docs/engine-tour.md) is a map of the whole engine with a
"if you want to understand X, open Y" table. [docs/editor-guide.md](docs/editor-guide.md)
is the editor, panel by panel.

These three headers explain the most, and all three are complete:

- `engine/include/engine/scene/Entity.h` — what a game object actually is, and
  why it is a bag of components rather than a family tree of classes.
- `engine/include/engine/core/GameClock.h` — why the game is simulated at a
  fixed rate and drawn at a different one.
- `engine/include/engine/scene/DeferredOps.h` — why creating and destroying
  things is queued, and what goes wrong when it is not.

---

## Three conventions worth knowing before you write anything

Each is written out in full at the top of the file that owns it, because each
is the kind of decision that costs an afternoon when it only lives in
somebody's head.

- **Matrices** — `math/Mat3.h`. Points are written as rows, so "do A and then
  B" is written `A * B`, and the move part of a transform lives in the bottom
  row.
- **Overlap** — `math/Overlap.h`. **Touching counts as overlapping**,
  everywhere, in every shape combination.
- **Collision layers** — `physics/Collider.h`. Two colliders are only tested
  when EACH one's list includes the other's layer, so collision events always
  come in pairs.

The world is **y-up**; the screen is y-down. The single place those are
reconciled is `Camera::ViewMatrix`.
