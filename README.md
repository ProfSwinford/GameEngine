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
| `engine`      | The engine, as a SHARED library. Knows nothing about any game.       |
| `editor`      | The development environment - Scene view, Game view, seven panels.   |
| `player`      | A game running on its own, with no editor. Contains no game itself.  |
| `hub`         | The launcher: a list of projects, a list of engine builds.           |
| `tests`       | The unit tests.                                                      |

**The project's scripts are not on that list, and that is the point.**
Every `.cpp` and `.h` under `assets/` is compiled by the **editor**, while it
is running, into a library that both programs load. Adding a script never
rebuilds anything here. To build them without opening the editor:

```bash
build/debug/bin/editor --build-scripts
```

---

## Trying it out

```bash
build/debug/bin/Debug/editor
```

That is the one to start with - and it opens the **launcher**, not the editor.
The editor opens a project it was *named* and nothing else; run with none it
starts the launcher so you can pick one. On a first run the launcher offers the
project it finds beside the engine, which is this repository, so it is one
click to the sample game.

To skip the launcher and open a project straight away:

```bash
build/debug/bin/Debug/editor --project .
```

| Command                        | What it does                                   |
| ------------------------------ | ---------------------------------------------- |
| `editor`                       | open the launcher, to pick a project           |
| `editor --project <folder>`    | open that project directly                     |
| `editor <folder>`              | the same, so a folder can be dragged onto it   |
| `editor --build-scripts`       | compile the project's scripts and exit         |
| `editor --build-game <folder>` | export a standalone game and exit              |
| `editor --help`                | the full list                                  |

See [docs/editor-guide.md](docs/editor-guide.md).

```bash
build/debug/bin/player
```

A game on its own, with no editor. It opens scene 0 of the build list - see the
editor's **Build** panel - falling back to the scene named in
`config/engine.json`.

**It contains no game.** There is no player, no score and no level name
anywhere in `player/`. The scene file places the entities and the scripts under
`assets/scripts/` give them their behaviour, which is why the same executable
is what every game built with this engine ships as.

| Command                      | What it does                                    |
| ---------------------------- | ----------------------------------------------- |
| `player`                     | open scene 0 of the build list                  |
| `player --scene <path>`      | open a different scene                          |
| `player --scene-index <N>`   | open scene N of the build list                  |
| `player --config <path>`     | use a different settings file                   |
| `player --frames <N>`        | run N frames then exit                          |
| `player --help`              | the full list                                   |

The sample game is **Collector**: move with WASD or the arrow keys and touch
the ten red squares before the timer runs out. It is built entirely from a
scene file and four scripts - `Pickup`, `GameRules`, `PlayerMovement` and
`CameraFollow` - so it plays identically whether you press Play in the editor
or launch the player. Attach `Autopilot` to the Player in the Inspector and it
plays itself, by pressing the same named actions the keyboard is bound to
rather than going round the input layer.

---

## Where everything is

| Folder         | What lives there                                                  |
| -------------- | ----------------------------------------------------------------- |
| `engine/`      | The engine, built as a shared library.                            |
| `editor/`      | The development environment. Panels live here.                    |
| `player/`      | A game running on its own — the export template. Contains no game. |
| `assets/`      | Scenes, images **and your scripts**, in whatever folders suit the game. **Committed.** |
| `tests/`       | Unit tests.                                                       |
| `cmake/`       | Build system pieces.                                              |
| `tools/`       | The fresh-clone check script.                                     |
| `config/`      | `engine.json` is read at start-up; `engine.example.json` documents every setting. |
| `docs/`        | The editor guide and the engine tour.                             |
| `.build/`      | Where the editor compiles your scripts to. Generated; not committed. |

There is no `scripts/` folder, and that is deliberate. Your behaviour scripts
live in `assets/` next to the scenes and images they belong with — the editor
compiles every `.cpp` and `.h` it finds anywhere under `assets/`, so a script
works wherever you decide to put it.

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
    { "type": "BoxColliderComponent", "halfExtents": [16, 16],
      "layer": "Player", "collidesWith": ["Pickup", "World"] }
  ]
}
```

A sprite's `texture` can be a **`.png`**, a `.bmp` or a `.jpg`. Use `.png` unless
you have a reason not to: it is the only one of the three that stores
transparency, so it is what you need for a sprite that is not a plain
rectangle. Drop the file anywhere under `assets/` and name it the short way, as
above.

Search the engine's source for `"Player"`, or for any position in any scene,
and you will not find it. That is the point: everything about a particular game
lives in data, and the engine only knows how to read it.

---

## Writing your own behaviour

**Assets → + Script** writes a new file from a template into whichever folder
you are browsing. Drag the `.cpp` onto an entity to attach it.

**Write only the hooks you want.** No `virtual`, no `override`, no empty stubs:

```cpp
class MyScript : public eng::ScriptBehaviour {
public:
    void OnFixedUpdate() {
        Transform()->Translate({0, eng::Time::DeltaTime() * 50});
    }
};
ENGINE_REGISTER_SCRIPT(MyScript)   // without this it can never be found
```

That is the same convenience a C# engine gets from reflection, except the
question "does this class have an OnFixedUpdate?" is answered by the
**compiler** while your script builds. There is no run-time lookup, and a
script with no update is never put in an update list at all — it costs nothing
per frame.

**There are two updates, and they run at different rates.** `OnFixedUpdate()`
runs every fixed simulation step — a steady 60 a second on every machine — and
is where movement, physics and anything timed belongs. `OnUpdate()` runs once
per *drawn* frame, which is however fast the machine happens to be, and is for
things tied to the screen rather than to the world. Putting movement in
`OnUpdate` is the mistake the split exists to make visible.

Neither takes an argument. Ask [`eng::Time`](engine/include/engine/core/Time.h),
which has Unity's names and returns the right delta for whichever hook you are
in:

```cpp
Time::DeltaTime()           // the fixed step inside OnFixedUpdate, the frame
                            // time everywhere else — and scaled, so it is 0
                            // while the game is paused
Time::FixedDeltaTime()      // the simulation step, wherever you ask
Time::UnscaledDeltaTime()   // real frame time; ignores pause and slow motion,
                            // which is what a pause menu needs to animate
```

The hooks are `OnStart`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy`,
`OnCollisionEnter/Stay/Exit`, `OnTriggerEnter/Stay/Exit` and `OnExpose`.
**They must be public**, because the
engine calls them from outside your class. Getting a name or a signature wrong
is a compile error that says what to write instead, and a script with no hooks
at all is a compile error too — those checks exist because a misspelled hook
would otherwise be a function nobody calls.

**Declare a public variable and it is in the Inspector** — editable while the
game runs, and saved on each entity separately, so one script drives two
entities that behave differently. Nothing to annotate or register:

```cpp
class Orbiter : public eng::ScriptBehaviour {
public:
    float radius = 90.0f;        // how far out it orbits
    float speed  = 1.2f;         // [0..10] turns per second
    eng::EntityRef centre;       // [TransformComponent] what to circle

    void OnFixedUpdate() { /* read them here, so a change takes effect now */ }
};
```

`float`, `int`, `bool`, `std::string`, `eng::Vec2`, `eng::Color` and
`eng::EntityRef`. The trailing comment becomes the tooltip; a `[low..high]` at
its start makes a slider, and a `[TypeName]` narrows a reference to entities
carrying that component or script. Keep one off screen by making it `private`
or naming it `m_something`.

**This is the one thing the compiler cannot do for us.** `requires` can ask
whether a *named* member exists; nothing in C++ can ask what members exist. So
the editor **reads the script's source** when it builds it — the way Unreal's
header tool does — and generates the registration. It is a whitelist of
declaration shapes rather than a C++ parser, so it misses unusual ones, and
missing them is cheap by design: the generated code wraps every variable in
`if constexpr (requires ...)`, so a wrong guess compiles away instead of
breaking the build, and the Console lists what was exposed and what was
skipped. Write `OnExpose` by hand for anything it cannot cover, and the
hand-written list wins.

**An `EntityRef` points at another entity** — drag one from the Hierarchy onto
the field, then reach what you need on the other side:

```cpp
if (auto* push = body.Find<eng::Rigidbody2DComponent>()) { /* shove it */ }
```

It stores the target's **name**, so it is readable in the scene file, and
renaming an entity in the Hierarchy moves every reference with it. Ask for the
target when you need it rather than keeping it, exactly as with an `EntityId` —
the thing on the other end can be destroyed.

**Only the values you change are saved.** Leave a variable alone and it is
absent from the scene file and takes whatever the code says — so editing that
default line still reaches every entity that never disagreed with it, which is
what somebody changing a default is actually asking for. The Inspector marks
the overridden ones and offers to put the default back. And what the *game*
does to a variable is never written to the level: a health counting down is
run-time state, not a saved setting.

Scripts are **compiled C++**, but the EDITOR compiles them: save a script,
switch back to the editor window, and it rebuilds and reloads by itself. You
never rebuild the editor. That does need a C++ compiler installed - see
[docs/editor-guide.md](docs/editor-guide.md).

`assets/scripts/Orbiter.cpp` is a worked example, and
[`engine/include/engine/scene/ScriptFields.h`](engine/include/engine/scene/ScriptFields.h)
explains the trade-offs above.

---

## Physics

Five collider shapes, and every one of them is rotated, scaled and parented
properly by its entity's transform:

| | |
| --- | --- |
| `BoxColliderComponent` | a rectangle, with an optional `edgeRadius` to round the corners |
| `CircleColliderComponent` | a centre and a radius |
| `CapsuleColliderComponent` | a rectangle with round ends — what a character usually wants, because a box catches on every seam in the floor |
| `PolygonColliderComponent` | up to 8 points, convex |
| `EdgeColliderComponent` | a chain of segments — ground with no underside to clip through |

Internally they all become the same thing: a **`ConvexShape`**, which is up to
eight points plus a radius. A circle is one point with a radius, a capsule is
two, a box is four with none. That is why there is one collision routine rather
than a table of box-versus-circle, circle-versus-capsule special cases growing
as the square of the shape count — and why adding a sixth shape costs a
converter rather than five new intersection tests.

**A collider says what shape something is. A `Rigidbody2DComponent` says it has
mass and may be moved.** Those are separate for the same reason they are in
Unity: a wall needs a shape and must never move.

```json
{ "type": "Rigidbody2DComponent", "bodyType": "Dynamic",
  "mass": 1.0, "gravityScale": 1.0, "freezeRotation": true }
```

| body type | |
| --- | --- |
| `Dynamic` | gravity and forces move it; collisions push it about |
| `Kinematic` | moves only when told to, and pushes dynamic things out of the way without being pushed back — a lift, a platform, usually a character |
| `Static` | never moves; the same as having no rigidbody, said out loud |

**Without a rigidbody on at least one side, nothing is pushed.** Two solid
colliders still report the contact — knowing two static things overlap is
useful while building a level — but neither moves, because neither has anything
that could be moved. That is Unity's rule, and it is almost always the answer
to "my player walks through walls".

### Triggers and solids are different events

```
either side is a trigger   ->  OnTriggerEnter / Stay / Exit
neither side is a trigger  ->  OnCollisionEnter / Stay / Exit
```

A script with `OnCollisionEnter` on a trigger collider is **never called** —
the hook is real and correctly spelled, just not the one being sent. Because
that is silent, the engine reports it in the Console the first time it happens,
naming the entity and the hook to use instead.

A collision hook may take either the other entity or the whole contact:

```cpp
void OnCollisionEnter(eng::EntityId other) { }                  // simple
void OnCollisionEnter(const eng::Collision2D& hit) {            // full
    if (hit.normal.y > 0.7f) { m_onGround = true; }             // came from below
    PlayThud(hit.relativeVelocity.Length());                    // how hard
}
```

`hit.normal` always points **from the other collider toward the receiver**, so
both sides of a collision read correctly in their own handler.

### Asking the world questions

Collision events tell you what touched you. `Physics2D` is the other direction
— what is over there, before you reach it, which is how anything responsive is
written:

```cpp
// Is there ground under my feet?
eng::QueryFilter filter;
filter.ignore = OwnerId();                     // don't find myself
const auto hit = eng::Physics2D::Raycast(position, {0, -1}, 40.0f, filter);
if (hit) { m_onGround = true; }

// What is in blast range?
eng::ColliderComponent* caught[16];
const int n = eng::Physics2D::OverlapCircle(position, 120.0f, caught, 16);
```

`Raycast`, `RaycastAll`, `OverlapPoint`, `OverlapCircle`, `OverlapBox`,
`OverlapCapsule`, `OverlapShape`, `CircleCast`, `BoxCast` and `Distance`. Every
one takes a `QueryFilter` for the layer, whether triggers count, and an entity
to ignore. A disabled collider is invisible to all of them.

`CircleCast` is **exact rather than stepped**, and the reason is the shared
shape representation: sliding a circle of radius r into something stops exactly
where a thin ray hits that same shape grown outward by r, and growing a
`ConvexShape` is adding one number to it.

Gravity and the solver live in `config/engine.json`. Note the units — Unity's
`-9.81` is metres per second squared, and a 2D game works in **pixels**, so
this project uses `-981`:

```json
"physics": { "gravity": [0.0, -981.0], "solverIterations": 4 }
```

---

## Tilemaps

Drawing a wall out of tiles normally means drawing forty-seven of them: a
straight run, the two ends, four outside corners, four inside corners, a lone
block, a cross junction, and so on down the list. This engine produces all
forty-seven from **five**, using the scheme and the sheet format of
[Fang Auto Tile](https://github.com/ruccho/FangAutoTile) — which in turn is the
WOLF RPG Editor format, so art drawn for either works here unchanged.

A sheet is **one tile wide and five tiles tall**:

| Band | What it holds |
| ---- | ------------- |
| 1    | outside corner — convex, nothing beside it |
| 2    | vertical edge — a left or right wall |
| 3    | horizontal edge — a top or bottom edge |
| 4    | inside corner — concave, tucked in |
| 5    | fill — solid interior |

The trick is that **a tile is drawn as four quarters, not one picture**. Asking
"which of 47 pictures is this?" needs 47 pictures; asking "which of 5 is this
*quarter*?" four times needs 5. It works because a corner of a tile can only
see three of the eight neighbours — the two beside it and the one diagonally
past it — and those eight arrangements collapse into those five shapes.

Nothing is baked. Fang generates a 47-tile texture up front because Unity's
tilemap wants one finished sprite per tile; this engine draws from a source
rectangle anyway, so the quarter to draw is four numbers into the sheet you
already loaded.

**`+ Tile` in the Assets panel writes one for you** - select a sheet, press it,
and the dialog validates the image with the loader's own code before writing
anything. Double-click a `.otile` to edit it in the Tile Palette, which can
also draw all 47 shapes the sheet produces so a mistake in the art is visible
at once.

Underneath it is a text file, and the smallest useful one is a single line:

```json
{ "sheet": "textures/tiles/grass.png" }
```

because the tile size and frame count are **measured from the image** — a sheet
is five tiles tall by definition. Stating them again would be a second
description of a fact the file already carries.

```json
{
  "sheet": "textures/tiles/stone.png",
  "collision": "Grid",
  "frameMode": "Animation",
  "animationMinSpeed": 3.0,
  "animationMaxSpeed": 5.0,
  "connectsTo": ["tiles/dirt.otile"]
}
```

- **Extra frames go sideways**, for `Animation` or `Random`. Each cell gets its
  own speed and starting point, so a lake ripples instead of pulsing in unison;
  `Random` picks from the cell's coordinates, so it is the same every run.
- **`connectsTo` is one-way**, as in Fang. Grass connecting to dirt rounds
  itself off against it; dirt still ends cleanly where it meets grass. Name
  each other in both files for a mutual join.
- **A connector tile** (`"connector": true`) is invisible but counts as present
  to everything around it. A line of them along the edge of a map makes the
  ground run off the screen instead of growing a finished border, and a line
  down the facing edges of two maps hides the seam between them.

### Painting

Open the **Tile Palette**, choose a tile and a tool, and paint in the Scene
view. Paint, Erase, Rectangle, Fill and Pick; left-drag paints and right-drag
erases, middle-drag still pans. The default tool is Select, so the Scene view
behaves exactly as it always did until you choose otherwise — and the current
tool is shown over the view, because a mode you cannot see is a mode you forget
you are in. **`Ctrl+Z` undoes a whole stroke** - see
[the editor guide](docs/editor-guide.md#undo).

### Making it solid

Add a `TilemapColliderComponent` beside the `TilemapComponent`, and cells whose
tile says `"collision": "Grid"` become solid. It watches the map and rebuilds
itself when you paint, so a wall you draw is a wall.

The solid cells are **merged into as few rectangles as will cover them** —
typically ten to a hundred to one, and the Inspector shows the ratio. That is
not only about the broad phase: where two boxes meet there is an edge, and a
body sliding along a floor can catch on it. A flat floor merged into one box
has no joins to catch on.

```
assets/tiles/stone.otile          a solid material
assets/tiles/water.otile          four animated frames, walked through
assets/tiles/connector.otile      invisible, joins everything
assets/scenes/tilemap.oscene      a level to walk around with WASD and Space
```

### What is not here

Fang's slopes, its texture packer, its sub-channels (normal and emission maps)
and its override tiles. The packer and sub-channels exist because Unity bakes a
texture and this engine does not; slopes need a collider traced per tile, which
is the one shape that cannot be merged with its neighbours. Per-tile collision
shapes are not here either — a cell is solid over its whole square or not at
all.

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
- **Overlap** - `engine/include/engine/math/Overlap.h`. Every shape becomes one
  `ConvexShape` - points plus a radius - so there is one collision routine
  rather than a table of pairings. **Touching counts as
  overlapping**, everywhere, in every shape combination.
- **Collision layers** - `engine/include/engine/physics/Collider.h`. Two
  colliders are only tested when EACH one's list includes the other's layer, so
  collision events always come in pairs.

The world is **y-up**; the screen is y-down. The single place those are
reconciled is `Camera::ViewMatrix`.
