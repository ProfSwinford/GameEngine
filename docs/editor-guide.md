# Using the editor

A tour of the development environment, panel by panel. It is arranged the way
Unity's is, so if you have used that, most of this will already be familiar.

Run it with:

```
build/debug/bin/Debug/editor
```

(or `build/debug/bin/editor` if your build put it there - see the README).

---

## The layout

```
+---------------------------------------------------------------+
|  File   View                                        60.0 FPS  |
+---------------------------------------------------------------+
|  [Play] [Pause] [Step]   Speed ---o---                        |  Toolbar
+-----------+---------------------------------------+-----------+
|           |  [ Scene ] [ Game ]                   |           |
| Hierarchy |                                       | Inspector |
|           |          the world, drawn             |           |
|           |                                       |           |
+-----------+---------------------------------------+-----------+
|  [ Assets ] [ Console ]                                       |
+---------------------------------------------------------------+
```

The **Tile Palette** is not in the picture because it arrives floating the first
time you run a build that has it - drag it onto the Inspector or the Assets row
and it stays there.

Every panel can be dragged, tabbed and resized. The arrangement is remembered
in a file called `imgui.ini`, written into whichever folder you started the
editor from, so it survives a restart. Delete that file to get the default
layout back. The **View** menu reopens anything you close.

---

## Scene view and Game view

These are two views of the **same world**, tabbed together in the middle.

|                | Scene                                   | Game                        |
| -------------- | --------------------------------------- | --------------------------- |
| Camera         | the editor's own - move it freely       | the **game** camera         |
| Helper shapes  | grid, origin arrows, collider outlines  | none                        |
| Move handles   | on the selected entity                  | none                        |
| What it is for | looking at what is *there*              | looking at what *ships*     |

Each one draws the world into its own off-screen picture, which is what makes
two views at once possible at all.

### Scene view controls

| Do this                   | With                                                |
| ------------------------- | --------------------------------------------------- |
| Pan                       | drag with the middle or right mouse button          |
| Zoom                      | the mouse wheel - it zooms **towards the cursor**    |
| Select                    | click it, or click it in the Hierarchy              |
| Frame the selection       | **F**, or the "Frame selection" button              |
| Move it                   | drag the red arm, the green arm, or the centre box  |
| Turn helper shapes on/off | the **Gizmos** button                               |
| Point the game camera here| **Align game camera**                               |

The Game view has its own row of camera fields, because the game camera is
what gets written into the scene file when you save.

The editor starts in **edit mode**: the clock is paused and nothing moves until
you press Play.

---

## Hierarchy

Every entity in the scene, as a tree. Parenting shows as nesting.

| Do this                     | Where                                    |
| --------------------------- | ---------------------------------------- |
| Create an entity            | **+ Create Entity**                      |
| Rename / Duplicate / Destroy| **right-click** an entity                |
| Find something              | the filter box                           |
| Give it a picture           | drag an image from **Assets** onto it    |
| Give it behaviour           | drag a `.cpp` from **Assets** onto it    |

---

## Inspector

Everything on the selected entity, editable. The transform's position and
rotation, the sprite's image and colour, a collider's size and layers, a
script's binding.

Use the **+ ComponentName** buttons at the bottom to add a component.

**Some fields are read-only while the game is running.** That is deliberate:
while a system owns a value, anything you type into it is overwritten on the
very next step. Press Pause first.

---

## Assets

The files on disk, under one root: `assets/`. Images, scenes and your scripts
all live here, in **whatever folder structure suits the game** - `enemies/`,
`player/`, `ui/`. Nothing is enforced.

That works because the editor compiles every `.cpp` and `.h` it finds anywhere
under `assets/`, at any depth. A script runs wherever you decide to put it, so
the things that belong to one part of your game can stay together instead of
being split up by what language they happen to be written in.

**+ Script** writes `<the folder you are looking at>/<Name>.cpp` from a
template, then **compiles and loads it straight away** - so it is ready to
attach by the time the dialog closes. **Build Scripts** rebuilds everything,
though that also happens on its own whenever the editor regains focus.

The compiled result is written to `.build/` beside `assets/`, not into it, so
this panel only ever shows files you wrote.

---

## Console

The log. Everything the engine and your own code write, with filters by level,
by channel, and by text.

Write to it from anywhere:

```cpp
ENGINE_LOG_INFO(eng::Channels::kGame, "health is now {}", health);
ENGINE_LOG_WARN(eng::Channels::kGame, "no texture called '{}'", path);
ENGINE_LOG_ERROR(eng::Channels::kGame, "could not load the level");
```

This is the main tool for finding out what your code is actually doing.

---

## Toolbar: Play, Pause and Step

**Play is safe.** Pressing it takes a snapshot of the scene, and **Stop** puts
the snapshot back - so a play session that moved the player and collected half
the pickups leaves the level exactly as you built it.

If the scene contains something that cannot be saved, Play is **refused**
rather than entered, because a snapshot that cannot be restored is worse than
no Play button.

**Pause and Step together are the best debugging tool in the editor.** Pause,
press Step once, and watch a single entity's position change by exactly one
simulation step in the Inspector while its collider is outlined in the Scene
view. There is no faster way to work out why a collision is not firing.

The speed slider changes how fast game time passes. It does **not** change the
size of a simulation step - see `engine/include/engine/core/GameClock.h`.

When you press Play, the Game view takes the keyboard and shows a green border
to say so. Clicking any other panel gives it back, so you can keep using the
editor while the game runs.

---

## Editing a scene

**File → Open Scene** lists everything in `assets/scenes/`. Dropping a
`.oscene` in there puts it in the menu with no rebuild.

**File → Save Scene**, or **Ctrl+S**. **Save Scene As...** for a new file. The
menu bar shows **UNSAVED** once anything has been changed.

Two things worth knowing:

- **Destroy is deferred.** An entity disappears at the end of the current
  simulation step rather than the instant you click, because the editor obeys
  the same rules as the engine. See
  `engine/include/engine/scene/DeferredOps.h`.
- **There is no undo.** Save before you experiment.

Things the editor deliberately does not do: no rotate or scale handles (move
only), no dragging to reparent in the Hierarchy, and the Assets panel creates
files but does not rename, move or delete them.

---

## Physics

### Triggers and solids send different events

This is the one collider setting that silently changes which hook a script
needs, so the Inspector spells it out under the Trigger checkbox:

```
either side is a trigger   ->  OnTriggerEnter / Stay / Exit
neither side is a trigger  ->  OnCollisionEnter / Stay / Exit
```

A script with `OnCollisionEnter` on a trigger collider never runs. The hook is
real and spelled correctly - it is just not the event being sent. The Console
reports it the first time it happens, naming the entity and the hook to use
instead, because there is nothing else that would tell you.

### Colliders in the Inspector

Five shapes: **Box** (with an edge radius to round the corners), **Circle**,
**Capsule** (pick Vertical or Horizontal), **Polygon** (an editable point list,
up to 8, convex) and **Edge** (a chain of segments, optionally looped, with a
radius to give it thickness).

Every one has:

| | |
| --- | --- |
| **Enabled** | off, it takes part in nothing - no events, no pushing, invisible to queries. This is how a hitbox is live for three frames of an attack |
| **Trigger** | reports overlaps, pushes nothing |
| **Offset** | shifts the shape off the entity's origin, so a character's feet can be the part that collides |
| **Friction / Bounciness** | 0 friction is ice; 0 bounciness is a beanbag and 1 a superball |
| **Density** | only used when a rigidbody has Auto mass ticked |
| **Layer / Collides with** | what it is, and what it wants to hear about |

A **Polygon** collider whose outline is dented says so in red: it collides as
the convex hull of those points rather than as drawn, so the shape on screen
and the shape being tested would otherwise differ with nothing to show why.
Split it into several colliders instead - they are tested independently and the
result is their union.

Friction and bounciness need something to push, so the panel says so when there
is no `Rigidbody2D` on the entity.

### Rigidbody2D

A collider says what shape something is; a rigidbody says it has mass and may
be moved. The Inspector explains each body type as you pick it, because
choosing the wrong one is the most common physics mistake there is:

| | |
| --- | --- |
| **Dynamic** | gravity and forces move it; collisions push it about |
| **Kinematic** | moves only when told to, and pushes dynamic things out of the way without being pushed back - a lift, a platform, usually a character |
| **Static** | never moves; the same as having no rigidbody at all |

**Gravity scale** is the single most useful number for making a jump feel
right: 0 floats, 1 is normal, 2 falls twice as hard, negative rises. **Freeze
rotation** is nearly always what a character wants - a capsule that tips over
when it bumps a step is not a player, it is a log.

Under "Right now" the panel shows the live velocity and spin. Those are not
settings and are not saved; they are there because "why is it drifting?" is
answered by a velocity that is not quite zero, and there is nowhere else to
see it.

### Seeing what the collision system sees

Collider outlines are drawn in the Scene view, in whatever shape the collider
actually is:

| colour | |
| --- | --- |
| green | not touching anything |
| red | touching something |
| cyan | a trigger |
| grey | disabled |

Turn the whole category off from the Scene view's Gizmos menu. Combined with
the Step button - one simulation step at a time - this is still the fastest way
to work out why a collision is not firing.

---

## Tile Palette

A tilemap is a grid of tiles that work out what they look like from the tiles
around them: paint a line of stone and the ends round themselves off, paint a
block and the middle fills in. The reasoning is in
[the README](../README.md#tilemaps); this is how to use it.

### Making a tile

A tile is a `.otile` file, and **+ Tile** in the Assets panel writes one.

1. Select the sheet in the Assets panel, then press **+ Tile** — its path is
   filled in for you. (You can also type the path, or pick from a dropdown of
   the images in the folder you are browsing.)
2. The dialog reports what the engine makes of the sheet — *"32px tiles, 4
   frame(s)"* — and shows the five bands down the side with their names, so a
   sheet whose bands are in the wrong order is visible before you commit to it.
3. If the sheet is the wrong shape it says so, in the loader's own words, and
   **Create** stays greyed out.

The check in the dialog *is* the loader's check: it builds the `.otile` it is
about to write and hands it to the same code that reads one at runtime. There
is no second opinion to disagree with it.

The file it writes states only the sheet and the collision setting. **Tile size
and frame count are deliberately left out** — they are measured from the image,
so writing them down would be a second description of something the file
already carries.

A sheet must be `tileSize` wide (times the number of frames) and `tileSize × 5`
tall, with an even `tileSize`. Nothing else is required.

### Editing a tile

Double-click a `.otile` in the Assets panel — or choose it in the palette and
open **Edit tile**. Everything is a widget: collision, frame mode, animation
speeds, what it joins up with, the editor tint.

Two things worth knowing:

**It edits the file, not a copy of it.** The widgets change keys in the `.otile`
as it was read, and saving writes that back out — so `_comment` lines and
anything else the panel does not know about survive. Keys come out in
alphabetical order, the same as a saved scene.

**The preview is of your unsaved edits.** The panel re-reads its own buffer
every frame, so a wrong sheet path turns red as you type it rather than after
you save.

**Show all 47 shapes** draws every arrangement of neighbours the sheet can
produce, assembled from its quarters exactly as the renderer assembles them.
This is the quickest way to find a sheet that is subtly wrong: hover any of
them to see which band each quarter came from. For an animated sheet, the
**Preview frame** slider picks the column.

Saving re-reads the tile in place, so every map already using it updates — and
their colliders rebuild, because changing `collision` changes which cells are
solid without any cell having changed.

### Painting

1. Open **Tile Palette** (it is in the **View** menu if you closed it).
2. If the scene has no tilemap, press **New Tilemap entity**. That gives you a
   `TilemapComponent` and a `TilemapColliderComponent` on one entity.
3. Click a tile in the grid. The tool switches to **Paint** automatically.
4. Paint in the Scene view.

| Tool          | What it does                                               |
| ------------- | ---------------------------------------------------------- |
| **Select**    | the Scene view's ordinary behaviour - the default          |
| **Paint**     | left-drag lays tiles down, right-drag erases               |
| **Erase**     | left-drag clears cells                                     |
| **Rectangle** | drag a box, applied when you let go; right-drag clears one |
| **Fill**      | floods the connected area of whatever you click            |
| **Pick**      | takes the tile under the cursor as the chosen one          |

While a tool other than Select is active the Scene view belongs to the brush:
clicking does not select, and right-drag erases rather than pans. **Middle-drag
still pans**, so there is always a way to move around without putting the brush
down. The current tool is written across the bottom of the view.

**There is no undo.** Erase is how a stroke is taken back. This is worth
knowing before a long session rather than after one.

### Why nothing appears

The Tile Palette reports `N cell(s) drawn, M skipped` for the last frame, and
the two add up to every cell in the scene's maps. So:

- **drawn 0, skipped many** - the camera is not looking at the map.
- **drawn 0, skipped 0** - nothing is painted, or the map's `layer` puts it
  behind something that covers it.
- **a palette entry in red in the Inspector** - that `.otile` would not load,
  and every cell using it is occupied but invisible. The Console says why.

### Making it solid

Cells are solid when their **tile** says so - `"collision": "Grid"` in the
`.otile` - and only when a `TilemapColliderComponent` is on the entity. So a
background of grass costs nothing and the stone in the same map is a wall.

The Inspector shows how many solid cells were merged into how many boxes.
A long flat floor should come out as **one** box: where two boxes meet there is
an edge, and a body sliding along the floor can catch on it.

After editing a `.otile` by hand - changing `collision`, say - press **Reload
tiles** in the Inspector. Painting rebuilds the collider by itself, but
changing a tile from decoration to solid changes which cells are solid without
any cell having changed, so nothing would otherwise notice.

### Connector tiles

A connector is invisible at runtime but counts as present to its neighbours.
Two uses:

- A line along the edge of a map makes the ground **run off the screen**
  instead of growing a finished border.
- A line down the facing edges of two maps **hides the seam** between them.

They show as faint magenta in the Scene view and nowhere else.
`assets/scenes/tilemap.oscene` has a row of them under the floor - turn them
off and the bottom of the level grows a lip.

---

## Writing a script

**Assets → + Script** writes a new file from a template.

**The editor compiles it for you.** Write the script in whatever text editor
you like, save it, and switch back to the editor window - it notices, rebuilds,
and reloads. You never rebuild the editor itself, and you do not need this
project's source code to do any of it.

There is a **Build Scripts** button in the Assets panel too, for building
without alt-tabbing - which is what you want after fixing a compile error.

What happens on a rebuild:

1. every running script object is destroyed
2. the old library is unloaded
3. every `.cpp` and script `.h` under `assets/` is compiled into
   `.build/userContent.dll`
4. that is loaded, and every script in the scene is reattached **by name**

So a scene you are editing keeps working across a rebuild with nothing
reattached by hand. Play mode is stopped first, the way Unity does, because a
running play session cannot survive its own code being replaced.

If the build fails, the compiler's messages appear in the **Console**, and
**SCRIPTS DID NOT BUILD** appears in the menu bar - your scripts have stopped
running until it is fixed. Fix the error and press **Build Scripts**.

**This needs a C++ compiler on your machine.** That is unavoidable: these are
real compiled C++ scripts. On Windows install Visual Studio or the standalone
Build Tools; on macOS the Xcode command line tools; on Linux `g++` or
`clang++`. The editor says which compiler it found in the Console at start-up,
and says so plainly if it found none.

The connection between an entity and its script is **by name**, so a script
that has not compiled yet still attaches to an entity, still saves into the
scene, and shows as **NOT FOUND** in red in the Inspector until it builds.

If you want to know exactly what the compiler was told, open
`.build/build.bat` - the editor writes it out, and you can run it yourself in a
terminal.

### Write only the hooks you want

There is no `virtual` and no `override`. Write the lifecycle functions you
care about and leave out the rest:

```cpp
class MyScript : public eng::ScriptBehaviour {
public:
    void OnStart() {}                          // first update after attaching
    void OnFixedUpdate() {}                    // every simulation step
    void OnUpdate() {}                         // every drawn frame
    void OnDestroy() {}                        // the entity is going away
    void OnCollisionEnter(eng::EntityId other) {}   // solid contact
    void OnTriggerEnter(eng::EntityId other) {}     // trigger contact

    void OnExpose(eng::ScriptFields&) {}            // variables for the Inspector
};
ENGINE_REGISTER_SCRIPT(MyScript)   // without this it can never be found
```

The engine works out which of those you wrote while the script is compiling, so
a script with no update is never asked to update — it costs nothing per frame
rather than a call into an empty function sixty times a second.

### Variables in the Inspector

A script can offer up the numbers worth tuning. They appear in the Inspector,
can be dragged **while the game is running**, and are saved on each entity
separately — so one script can drive two entities that behave differently:

```cpp
class Orbiter : public eng::ScriptBehaviour {
public:
    float radius = 90.0f;
    float speed  = 1.2f;

    void OnExpose(eng::ScriptFields& fields) {
        fields.Var("radius", radius, "how far out it orbits");
        fields.Range("speed", speed, 0.0f, 10.0f);   // a slider, not a drag box
    }

    void OnFixedUpdate() { /* read radius and speed here */ }
};
```

`Var` draws a drag box; `Range` draws a slider with ends. Use `Range` only when
the range is part of what the number means — a 0-to-1 blend, a percentage. A
distance has no upper limit and a slider would invent one.

Six types, and only ordinary members:

| | drawn as |
| --- | --- |
| `float`, `int` | a drag box, or a slider with `Range` |
| `bool` | a checkbox |
| `std::string` | a text box |
| `eng::Vec2` | two numbers |
| `eng::Color` | a colour picker |

A `static constexpr` constant **cannot** be one of these, and the compiler says
so by name: a constant cannot be changed, so the Inspector could only show it.
Make it an ordinary member with its default written where the constant was.

**Only the values you change are saved.** A variable you leave alone is absent
from the scene file and takes whatever the code says — so editing that default
line still reaches every entity that never disagreed with it. The Inspector
puts a `revert` button beside the ones this entity has its own value for, which
is both how you can tell and how you undo it.

Three things follow from that, and they are all deliberate:

- **The name is the key in the scene file**, and it is shown verbatim rather
  than tidied into "Max Speed". Renaming it orphans whatever was saved under
  the old spelling; the Inspector lists values nothing claims and offers to
  drop them, rather than deleting them for you.
- **What the game does to a variable is not saved.** A health counting down
  during play is run-time state, and a level saved afterwards would otherwise
  come back with the player on three health. The Inspector shows the live
  value, because watching a number move is worth having; saving writes what
  you set.
- **A script that is not compiled in keeps its values untouched.** Attach
  `PlayerController` in a build without it and the component holds its
  settings, writes them back out unchanged, and hands them over on the build
  that has the class.

Read the variable where you use it rather than copying it in `OnStart` — that
is what makes dragging the slider take effect immediately, because nothing
cached it. `assets/scripts/Orbiter.cpp` and `TilemapWalker.cpp` are the worked
examples.

### The two updates

They run at different rates, and which one you pick is a real decision:

| | runs | use it for |
| --- | --- | --- |
| `OnFixedUpdate()` | every fixed simulation step — 60 a second on every machine | movement, physics, timers, anything that must behave the same everywhere |
| `OnUpdate()` | once per **drawn** frame — however fast this machine is | fades, menus, transitions, anything tied to the screen |

Neither takes an argument. Ask `eng::Time`:

```cpp
Time::DeltaTime()           // the right one for where you are: the fixed step
                            // inside OnFixedUpdate, the frame time elsewhere
Time::FixedDeltaTime()      // always the simulation step
Time::UnscaledDeltaTime()   // real frame time, ignoring pause and slow motion
Time::ElapsedTime()         // game time; stops while paused
```

`Time::DeltaTime()` is **scaled**, so at a time scale of zero it is 0 and
anything driven by it stops dead — while `OnUpdate` keeps being called and
`Time::UnscaledDeltaTime()` keeps returning real time. That is what lets a
pause menu animate on top of a frozen game, and it is why the two updates are
separate things rather than one.

**The hooks must be public.** The engine calls them from outside your class, so
a private one is invisible to it.

The one cost of finding hooks by name is that a misspelled one is not an
error — it is a function nobody calls. Several things push back on that, and
all of them are compile errors:

| You wrote | What you get |
| --- | --- |
| `Update`, `Start`, `Awake` | *"This engine calls it OnUpdate(), not Update."* |
| `FixedUpdate` | *"This engine calls it OnFixedUpdate(), not FixedUpdate."* |
| `LateUpdate` | *"This engine has no LateUpdate"* - the camera already runs after everything it follows |
| `OnCollisionEnter2D`, `OnTriggerEnter2D` | *"Drop the 2D"* - the whole engine is 2D, so there is no 3D version to tell it apart from |
| `void OnUpdate(float dt)` | *"OnUpdate(float) is gone..."* — the signature this engine used to have, pointing at whichever of the two you meant |
| no recognisable hooks at all | *"...has none of the lifecycle functions. Check the SPELLING, and that they are PUBLIC."* |

`assets/scripts/Orbiter.cpp` is a worked example. Read it before writing your
own.

### When a script does nothing

The Console lists every script it loaded **together with the hooks it found in
it**, each time they are built:

```
script 'Orbiter' - OnStart, OnFixedUpdate, OnCollisionEnter
script 'Chaser'  - OnCollisionEnter
```

If your script is listed with hooks you did not expect, the spelling is where
to look. If it is not listed at all, either it did not compile - look further
up the Console - or the file is missing its `ENGINE_REGISTER_SCRIPT` line,
which the editor also warns about by name. The Inspector shows the same hook
list for the selected entity's script.

### When the scripts will not link

```
Autopilot.obj : error LNK2019: unresolved external symbol
  "class eng::ColliderComponent * __cdecl eng::FindCollider(class eng::Entity &)"
```

The editor reports this as "the scripts did not compile", but unresolved
**`eng::`** symbols nearly always mean the engine library is out of date rather
than the script being wrong. The header declares the function, so the script
compiled perfectly well; the library it links against was simply built before
that function existed.

**Debug and Release each have their own `engine.lib`**, so building one leaves
the other behind. Changing the engine and then playtesting in the configuration
you did not rebuild is the usual way to arrive here. Rebuild the engine in the
configuration you are actually running:

```bash
cmake --build build/debug
```

and build the scripts again. The editor prints a note above the compiler output
when it can see that the library is older than the headers — it is comparing
two dates, so treat it as a likely cause rather than a verdict.

Unresolved symbols that are **not** `eng::` are an ordinary mistake: something
declared in one of your own headers and never defined.

### If you rename a class

Renaming the class inside a file is invisible to the scene, which refers to it
by the old name. The editor checks for this after every build: when a file that
defined one class now defines exactly one differently-named class, it treats it
as a rename, moves every attached component across, and says so:

```
'enemies/Chaser.cpp' renamed its script class from 'Chaser' to 'Pursuer'
 - 3 attached component(s) moved across. Save the scene to keep the change.
```

Nothing is written to disk, so an unwanted move is undone by not saving. When
the change is less clear-cut - a class removed with no obvious replacement -
it is reported with the number of entities affected and left alone, because
guessing would be worse.
