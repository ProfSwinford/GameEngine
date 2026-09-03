# Engine2D — Editor Base

The starting point for the course. You get **a finished editor**, **a finished
game runner**, and **an engine that is entirely empty** — every function is
there with the right name and the right arguments, and not one of them does
anything yet. You write them, one at a time, and watch the editor come to life.

---

## Build it

You need **CMake 3.28 or newer**, a **C++23 compiler**, and **Git**. Nothing
else — SDL3, Dear ImGui, nlohmann/json and doctest are downloaded and built
automatically the first time you configure.

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

It compiles and links cleanly. That is worth noticing: **the project is never
broken.** It just does not do anything yet.

> The first configure takes several minutes, because it is compiling SDL from
> source. **It is not stuck** — watch the progress messages.

---

## Run it — and read what happens carefully

```bash
build/debug/bin/editor
```

You get one line, and then it exits:

```
the editor could not start. The messages above name the part that failed.
```

**There are no messages above. That is the first lesson.**

The engine tried to read `config/engine.json` and `LoadBootConfig` returned
false, because you have not written it yet. It tried to say so — and could not,
because `Log::Init` has not been written either. A program that cannot explain
its own failure is the worst possible place to start from.

So the first thing you write is the log. After that, every failure explains
itself, and everything else is downhill.

---

## What is given, and what is yours

| Given to you, finished | Yours to write |
| --- | --- |
| The whole **editor** — panels, docking, Play/Pause/Step, the asset browser, the script compiler | |
| The whole **sandbox** — the game running without the editor | |
| **`engine/include/`** — every header, complete. This is the specification, and it does not change | |
| **`tests/`** — 59 test cases that define what "finished" means | |
| **`assets/`** — scenes, images and sample scripts to aim at | |
| | **Every `.cpp` in `engine/src/`** — all 31 of them |

The headers are the part to read. They explain what each function is for and
why it exists, in far more detail than the `.cpp` files do.

---

## What a skeleton file looks like

Open `engine/src/scene/Scene.cpp`. Every function that belongs there is present,
with the right name and the right arguments, and a sentence saying what it is
for:

```cpp
// Looks an entity up by id, or nullptr when that id is out of date.
Entity* Scene::Get(EntityId /*id*/) {
    return nullptr;
}
```

Your job is to replace the body. The signature and the comment stay.

Every function returns a harmless default — `false`, `0`, `nullptr`, an empty
value — so nothing crashes and nothing pretends to have worked. Delete a
parameter's `/* */` comment markers when you start using it.

---

## The order to write them in

Do **not** work through the files alphabetically, and do not follow the
subsystem start-up order either — it covers only twelve of the thirty-one files,
and two of its orderings exist for shutdown rather than for learning.

The order that works is roughly:

| | Write | You get |
| --- | --- | --- |
| 1 | `Log` · `LogBuffer` · `Config` · `Json` · `FileSystem` · `Subsystem` | The engine starts and says so. **Console and Assets panels work.** |
| 2 | `Window` · `Renderer` · `EventPump` · `SdlHandles` | **A window opens.** |
| 3 | `GameClock` | The frame loop runs. **Toolbar works.** |
| 4 | `Mat3` · `Transform2D` · `Camera` | Coordinates exist. |
| 5 | `Gizmos` | **The first thing you can see** — a grid and the origin arrows. |
| 6 | `Entity` · `Component` · `Scene` (create/get/foreach) | **Hierarchy and Inspector work.** |
| 7 | `Scene::Load` and every `Deserialize` | A `.json` file becomes a world. |
| 8 | `Texture` · `ResourceManager` · `SpriteRenderSystem` | **The game appears on screen.** |
| 9 | `SystemOrder` · `SpinComponent` | Things move. |
| 10 | `InputMap` | Controls. |
| 11 | `Overlap` · `Collider` · `Messaging` · `DeferredOps` | Collisions, and destroying things safely. |
| 12 | `Scene::Save` · `SaveToString` · `LoadFromString` | Saving, and Play/Stop without losing your level. |
| 13 | `ScriptComponent` · `ScriptLibrary` | Students write their own behaviour scripts. |

Three of those are forced rather than chosen: step 4 before step 6, because
`Entity::Transform()` returns a `Transform2D&`; step 6 before step 7, because
until the component factory works a scene file is just text; and step 12 before
step 13, because reloading scripts stops play mode, so a script rebuild loses
the level if saving does not work.

---

## The tests are your checklist

```bash
ctest --preset debug
```

Almost all of them fail right now, and that is the point. Each failing test is a
piece of engine that has not been written yet. Pick the file you are working on,
make its tests pass, move on.

```bash
build/debug/bin/tests --test-case="*Transform*"
```

When all 59 pass, the engine is finished.

---

## You never have to touch CMake

The build finds source files by looking, not by being told: every `.cpp` under
`engine/src/`, `editor/src/` and `sandbox/src/`, at any depth. Add a file,
rename one, move one into a new folder — the next build picks it up.

Two things worth knowing:

- **CMake never fails, even with an empty `engine/src/`.** It says
  `engine: no sources in src/ yet - building an empty library` and generates a
  placeholder. Configuring is never the thing that is broken.
- **But the editor needs the engine to link.** Delete a skeleton file rather
  than filling it in and the project still *compiles*; it fails at the **link**
  step, naming exactly which functions are missing. That is a linker error, not
  a build-script error. Put the file back, or write what it named.

---

## Where everything is

| Folder | What lives there |
| --- | --- |
| `engine/include/` | The specification. Complete, and the best documentation in the project. |
| `engine/src/` | Thirty-one empty files. All of this is your work. |
| `editor/` | The development environment. Complete. |
| `sandbox/` | The game running on its own. Complete. |
| `tests/` | The checklist. |
| `assets/` | Scenes, images and scripts. |
| `config/` | `engine.json` is read at start-up; `engine.example.json` documents every setting. |
| `docs/` | The editor guide and the engine tour. |
| `.build/` | Where the editor compiles scripts to. Generated; not committed. |

---

## Three conventions to know before you write anything

Each is written out in full at the top of the header that owns it, because each
is the kind of decision that costs an afternoon when it only lives in somebody's
head.

- **Matrices** — `math/Mat3.h`. Points are written as rows, so "do A and then B"
  is written `A * B`, and the move part of a transform lives in the bottom row.
- **Overlap** — `math/Overlap.h`. **Touching counts as overlapping**,
  everywhere, in every shape combination.
- **Collision layers** — `physics/Collider.h`. Two colliders are only tested
  when EACH one's list includes the other's layer, so collision events always
  come in pairs.

The world is **y-up**; the screen is y-down. The single place those are
reconciled is `Camera::ViewMatrix`.
