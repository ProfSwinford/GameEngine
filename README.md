# Engine2D

A small 2D game engine with a Unity-style editor, written in C++ for learning
how a game engine actually works.

It is deliberately readable. Every file starts with a comment explaining what
it does and why it is built that way, and there is nothing clever in it that
does not earn its place.

---

## Building it

You need **CMake 3.28 or newer**, a **C++23 compiler**, and **Git**. Nothing
else - SDL3, Dear ImGui, nlohmann/json and doctest are downloaded and built
automatically the first time you configure.

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

The programs end up in `build/debug/bin/`. On Windows with Visual Studio they
land one level deeper, in `build/debug/bin/Debug/`, because Visual Studio keeps
its Debug and Release output side by side.

> The first configure takes several minutes, because it is compiling SDL from
> source. **It is not stuck** - watch the progress messages.

### Visual Studio on Windows (the easiest way)

**File → Open → Folder** on this folder. Visual Studio reads `CMakePresets.json`
by itself, offers `debug` / `release` / `strict` in the configuration dropdown,
and manages the `build/` folder for you. Nothing else is needed.

**Pick one owner for `build/`.** Visual Studio builds into `build/<preset>`,
which is the same folder the command line uses. If both are open at once they
will fight over it, which shows up as spontaneous full rebuilds. Use the IDE
*or* the terminal for a given copy of the project.

### The command line on Windows

Use a **"Developer PowerShell for VS"** or a Developer Command Prompt, not an
ordinary terminal. Without it you get:

```
fatal error C1083: Cannot open include file: 'cstddef': No such file or directory
```

which looks like a broken compiler and is not one - the compiler simply has not
been told where its own headers are.

### Running the tests

```bash
ctest --preset debug
```

---

## What gets built

| Target        | What it is                                                          |
| ------------- | ------------------------------------------------------------------- |
| `engine`      | The engine library. Knows nothing about any particular game.         |
| `editor`      | The development environment - Scene view, Game view, seven panels.   |
| `sandbox`     | The game running on its own, with no editor.                         |
| `gamescripts` | Behaviour scripts. Used by **both** programs above.                  |
| `tests`       | The unit tests.                                                      |

---

## Trying it out

```bash
build/debug/bin/Debug/editor
```

That is the one to start with. See [docs/editor-guide.md](docs/editor-guide.md).

```bash
build/debug/bin/Debug/sandbox
```

The game on its own. It opens the scene named in `config/engine.json` - an
orbiting three-level hierarchy built entirely by the data file.

| Command                       | What it does                                    |
| ----------------------------- | ----------------------------------------------- |
| `sandbox`                     | open the scene from `config/engine.json`        |
| `sandbox --game`              | play the sample game                            |
| `sandbox --autoplay`          | the same game, played automatically             |
| `sandbox --scene <path>`      | open a different scene                          |
| `sandbox --frames <N>`        | run N frames then exit                          |
| `sandbox --help`              | the full list                                   |

`--game` is **Collector**: move with WASD or the arrow keys and touch the ten
red squares before the timer runs out. `--autoplay` finishes a round in about
twelve seconds by pressing the same named actions the keyboard is bound to - so
it exercises the real input path rather than going round it.

---

## Where everything is

| Folder         | What lives there                                                  |
| -------------- | ----------------------------------------------------------------- |
| `engine/`      | The engine library.                                               |
| `editor/`      | The development environment. Panels live here.                    |
| `sandbox/`     | The game. The only place game-shaped code belongs.                |
| `gamescripts/` | Behaviour scripts, used by both programs.                         |
| `tests/`       | Unit tests.                                                       |
| `cmake/`       | Build system pieces.                                              |
| `config/`      | `engine.json` is read at start-up; `engine.example.json` documents every setting. |
| `assets/`      | Scenes and images, loaded while the game runs. **Committed.**     |
| `docs/`        | The editor guide and the engine tour.                             |

---

## Changing things without touching C++

A surprising amount of this project is data rather than code.

**The window size, the log level and the key bindings** are in
`config/engine.json`. Change a value, save, run again.

**Levels** are in `assets/scenes/*.json`. An entity is a name and a list of
components:

```json
{
  "name": "Player",
  "components": [
    { "type": "TransformComponent", "position": [0, 0] },
    { "type": "SpriteComponent", "texture": "textures/checker_green.bmp" },
    { "type": "AABBColliderComponent", "halfExtents": [16, 16],
      "layer": "Player", "collidesWith": ["Pickup", "World"] }
  ]
}
```

Search the engine's source for `"Player"`, or for any position in any scene,
and you will not find it. That is the point: everything about a particular game
lives in data, and the engine only knows how to read it.

---

## Writing your own behaviour

**Assets → + Script** in the editor writes a new file from a template with
every lifecycle hook explained. Drag the `.cpp` onto an entity to attach it.

```cpp
class MyScript final : public eng::ScriptBehaviour {
    void OnStart() override {}
    void OnUpdate(float dt) override {}
    void OnDestroy() override {}
    void OnCollisionEnter(eng::EntityId other) override {}
};
ENGINE_REGISTER_SCRIPT(MyScript)   // without this it can never be found
```

Scripts are **compiled C++**, so a new one runs after you rebuild. Everything
else works immediately, because the connection between an entity and its script
is made by NAME - see [docs/editor-guide.md](docs/editor-guide.md).

`gamescripts/Orbiter.cpp` is a worked example.

---

## Where to start reading

[docs/engine-tour.md](docs/engine-tour.md) is a map of the whole engine with a
"if you want to understand X, open Y" table.

If you would rather just start opening files, these three are the ones that
explain the most:

- `engine/include/engine/scene/Entity.h` - what a game object actually is, and
  why it is a bag of components rather than a family tree of classes.
- `engine/include/engine/core/GameClock.h` - why the game is simulated at a
  fixed rate and drawn at a different one.
- `engine/include/engine/scene/DeferredOps.h` - why creating and destroying
  things is queued, and what goes wrong when it is not.

---

## Three conventions worth knowing before reading the code

Each of these is written out in full at the top of the file that owns it,
because each is the kind of decision that costs an afternoon when it only
lives in somebody's head.

- **Matrices** - `engine/include/engine/math/Mat3.h`. Points are written as
  rows, so "do A and then B" is written `A * B`, and the move part of a
  transform lives in the bottom row.
- **Overlap** - `engine/include/engine/math/Overlap.h`. **Touching counts as
  overlapping**, everywhere, in every shape combination.
- **Collision layers** - `engine/include/engine/physics/Collider.h`. Two
  colliders are only tested when EACH one's list includes the other's layer, so
  collision events always come in pairs.

The world is **y-up**; the screen is y-down. The single place those are
reconciled is `Camera::ViewMatrix`.
