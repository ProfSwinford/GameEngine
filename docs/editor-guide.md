# Using the editor

A tour of the development environment, panel by panel. It is arranged the way
Unity's is, so if you have used that, most of this will already be familiar.

Run it with:

```
build/debug/bin/Debug/editor
```

(or `build/debug/bin/editor` if your build put it there - see the README).

That opens the **launcher**, not the editor: the editor opens a project it was
*named* and nothing else. On a first run the launcher offers the project it
finds beside the engine - this repository - so it is one click to the sample
game. `editor --project <folder>` skips the launcher entirely.

---

## Projects

**A project is a folder.** It holds `assets/` (scenes, scripts, textures),
`config/engine.json` (the settings), and two folders the editor generates:
`.build/` for compiled scripts and `.editor/` for your place in the work. Only
the first two are worth committing; a new project's `.gitignore` says so.

**The settings file is not optional furniture.** Every setting has a default,
so a project without one starts — but two of those defaults are wrong for any
real game, and the symptoms look like bugs somewhere else entirely. See [If a
project runs in slow motion, or ignores the
keyboard](#if-a-project-runs-in-slow-motion-or-ignores-the-keyboard).

**It can live anywhere.** The editor does not have to be installed beside it:

```bash
editor --project D:/games/orbit
```

**With no folder named, the editor opens the launcher rather than guessing.**
It used to search upward from its own location and open the first project it
found. That is pleasant exactly once - the first time you run it out of a build
tree that happens to sit inside a project - and misleading every time after.
Once a launcher can point the editor at any folder on the machine, "the nearest
project above the executable" is not a default, it is a stale reference that
loads itself: a cold start would compile that project's scripts and write its
`.editor/session.json` without anybody asking for it.

Three things count as naming a project: `--project <folder>`, a folder as the
first argument (which is what dragging a project folder onto the editor does),
and the `ENGINE_ASSET_ROOT` environment variable - and that last one has to
hold a project, or the launcher opens and says so.

`--build-scripts` and `--build-game` are the exception and do still search
upwards, because an automated build has nobody to pick.

**File → New Project...** picks a folder, asks for a name, and creates the
skeleton. The new project has **no scene** - **File → New Scene** (`Ctrl+N`)
starts one, then add an object and **Save Scene As...**. That is deliberate:
shipping a starter scene would mean the project creator knowing the scene
format, which is a second place to update whenever the format changes and the
first place to forget.

Its `config/engine.json` arrives with a `physics` section (gravity in **pixels**
— see below) and an `input` section binding WASD, the arrow keys, Space and the
mouse, so the project is playable from the first scene you draw. Both are
ordinary settings: rename the actions, change the gravity, delete what you do
not want.

### If a project runs in slow motion, or ignores the keyboard

Both symptoms are the same missing thing, and a project created before this
engine version has it: **`config/engine.json` with no `physics` or `input`
section**. Open the Console at start-up and it says so, once, for each.

**Gravity is in your game's units, and this engine works in pixels.** The
built-in default is Unity's `-9.81`, which is *metres* per second squared — and
at roughly a hundred pixels to the metre that is a barely visible drift rather
than gravity. A project that does not override it accelerates everything at a
**hundredth** of the intended rate. Fall time goes as 1/√g, so things fall,
jump and arc about **ten times too slowly**: the game looks like it is running
in slow motion, while in fact the simulation is running at full speed on numbers
that are wrong by two orders of magnitude. Add:

```json
  "physics": {
    "gravity": [0.0, -981.0],
    "solverIterations": 4
  }
```

**With no `input` section nothing is bound**, so every `IsPressed` and
`GetAxis2D` answers no, for ever. The scripts compile, load and run — the
Console says so, and the Inspector shows them attached — and the game does not
respond to the keyboard, which is indistinguishable from scripts that are
broken. The Game view says so across the bottom while you are playing. Copy the
`input` block from `config/engine.example.json`, or from any project created by
a current editor.

Neither of these is a frame-rate problem, and the Toolbar is where to check
that: it prints game time against real time, and the number of simulation steps
in the last frame. If game time is keeping up with real time, nothing is
dropping frames — and if the **Speed** slider beside it is not at `1x`, the
Game view now says that too.

**File → Open Project...** picks a folder. Picking one *inside* a project opens
the project, so you can point it at `assets/scenes/` and it will do the obvious
thing. **File → Recent Projects** is the same list the launcher shows.

**Opening a project restarts the editor**, the way Unity does. It is not
laziness - `editor/src/ProjectMenu.h` explains at length what re-rooting a
running engine would cost and why the risk is not worth one second of
start-up. Nothing is lost: `.editor/session.json` remembers the open scene, the
selection and the Scene view's camera, and `.editor/imgui.ini` remembers the
panel layout, so reopening a project puts you back where you were. If there is
unsaved work you are asked first, and the dialog names the file.

**`project.oproj` is optional.** A folder with an `assets/` folder in it is a
project whether or not it has one - which is why this repository needs nothing
added to it. The manifest only gives a project a name of its own, so that two
folders both called `game` are distinguishable in a list. **File → Add a
Project File** writes one when you want it; nothing is ever written into your
folder uninvited.

---

## The launcher

`hub` is a separate, small program: a list of projects, a list of engine
builds, and a button that starts the right editor on the right project.

It exists for the one thing the editor cannot do - choose *which engine* to
open a project with. An editor is built against one `engine.dll` and has
already loaded it by the time it could offer a choice.

An "engine" here is just a folder containing an editor and an
`engine-version.json`, so your own `build/debug/bin` and `build/release/bin`
already qualify and the one the launcher ships beside is always available. It
does **not** download or install engine versions; that needs a release server
and an install layout this project does not have yet.

**It says when a project's scripts were built by a different engine.** Because
project and engine are two independent choices here, it is easy to pick the
Release engine for a project the Debug one compiled — and the two are not
interchangeable: the C runtime differs, so loading those scripts would corrupt
memory rather than fail. The engine refuses them, which means every script in
every scene reads **NOT FOUND**, which looks exactly like scripts that are
broken.

So the row says so *before* you click, in amber: *"scripts were built by 1.0.0
Debug — the editor will rebuild them for 1.0.0 Release on opening"*. It is a
wait, not an error — the editor fixes it by recompiling, which takes a few
seconds — and it is deliberately **not** phrased as a problem, because this is
the ordinary state the first time a project moves between engines and a warning
that cried wolf would be ignored within a day.

It compares `.build/buildconfig.txt`, which is the record the engine's own ABI
check reads — not the engine version in `project.oproj`, which says which engine
*created* the project and is a different fact entirely. An engine folder with no
`engine-version.json` says nothing rather than guessing.

The launcher closes when it starts an editor. **File → Close Project** opens it
again, so closing a project lands you somewhere you can open another one.

**The editor opens it too**, whenever it is run without a project. That is what
stops a cold start loading whichever project happens to sit above the
executable, and it means there is one way into a project rather than two.

On a first run, when the project list is still empty, the launcher offers the
project it finds beside the engine - the same folder the editor used to open on
its own, shown as a row with a button rather than done to you. One click
instead of none, and nobody is surprised by which project appeared. Once there
is a list, that row goes away: the folder is either in the list or was left out
on purpose.

If the launcher is missing - a hand-copied install with no `hub.exe` beside the
editor - the editor says so and names both ways forward rather than falling
back to searching. Building normally cannot produce that, because the `editor`
target depends on the `hub` target.

### Why your scripts rebuild after an engine change

`engine-version.json` carries a `scriptAbi` number, and the editor records it
alongside the C runtime flavour when it compiles your scripts. Change engines
and every script is recompiled.

That is not caution for its own sake. Your scripts are DLLs loaded into the
editor, passing `std::string` and `std::vector` across the boundary, so they
have to be built against the same headers as the engine loading them. Two
engines in different folders were already caught, because the library path is
part of the record - but an engine *replaced in place*, which is what
installing a new version over an old one is, left every object looking current
while having been compiled against different headers. That failure does not
announce itself: the library loads, the Console says the build succeeded, and
the program dies on the first allocation that crosses the heap boundary. The
check is a comparison rather than a date, so going *back* to an older engine
rebuilds too.

### "the compiled scripts were built by ..., but this is ..."

If you see that line, the scripts in `.build/` were compiled for a different
engine than the one now trying to load them. **They have not been loaded**, so
every script shows as NOT FOUND until they are rebuilt. Nothing is broken; run
`editor --build-scripts`, or just open the editor, which rebuilds on its own.

The commonest way to get there is running `player` after the *other* build
configuration compiled the scripts — `build/debug` and `build/release` share one
`.build/` folder, so whichever editor built last wins.

**The editor fixes this by itself** and the player cannot, which is the whole
reason the message exists. The editor has a compiler, notices the mismatch and
recompiles. The player has no compiler, so its only options were to load the
scripts and corrupt memory, or to refuse and say so. It used to do the first:
an access violation, no log output at all, and — because only some scenes
allocate across the boundary early enough — it looked like a bad *scene* rather
than a bad build.

An exported game never sees this. It ships one `userContent.dll` built beside
the `engine.dll` that went with it, so they match by construction and there is
nothing to check.

### One exception to "the last successful build still runs"

Normally a script that will not compile keeps its last working library, so the
rest of the game carries on while you fix it. That is the point of one library
per script.

**Changing engine or configuration breaks that promise, deliberately.** The kept
library was built for a *different* engine, so keeping it would load exactly the
mismatched library this all exists to prevent — and it would do so inside the
editor, one module among ten correct ones. So when the build stamp changes, the
old libraries are discarded along with the objects, and a script that then fails
to compile says so:

```
scripts/Orbiter.cpp did not compile, and the build it had was made for a
different engine - it was discarded rather than loaded, so its scripts will
show as NOT FOUND until this compiles
```

That is a separate message from "has never been built", because the two are
different situations and only one of them means you have lost nothing.

---

## Keyboard shortcuts

| Key            | What it does                                            |
| -------------- | ------------------------------------------------------- |
| `Ctrl+Z`       | Undo                                                    |
| `Ctrl+Y`       | Redo (`Ctrl+Shift+Z` works too)                         |
| `Ctrl+S`       | Save the scene - or Save As, if it has no file yet      |
| `Ctrl+Shift+S` | Save Scene As...                                        |
| `Ctrl+N`       | New Scene                                               |
| `Ctrl+O`       | Open Scene...                                           |
| `Ctrl+D`       | Duplicate the selection                                 |
| `Ctrl+A`       | Select every entity                                     |
| `Ctrl+Shift+A` | Deselect everything                                     |
| `Ctrl+P`       | Play / Stop                                             |
| `Ctrl+Shift+P` | Pause / Resume                                          |
| `Ctrl+Alt+P`   | One simulation step                                     |
| `Del`          | Delete - entities, or a file in the Assets panel        |
| `F2`           | Rename - the selected entity, or a file                 |
| `F`            | Frame the selection, while the Scene view is hovered    |

Everything from `Ctrl+Z` to `Ctrl+Shift+A` is in the **File** or **Edit** menu
with the key printed beside it, which is where you would expect to find it. The
three play chords are on the toolbar buttons' tooltips instead, since Play,
Pause and Step are buttons rather than menu items.

The last three have **no modifier on purpose**, which is why they are not in
the first group: what they mean depends on which panel has the keyboard. `Del`
removes the selected entities in the Hierarchy and the Scene view, and a *file
from the disk* in the Assets panel - two different and equally correct answers
to the same key, chosen by focus. A global shortcut cannot make that choice, so
those keys live next to the thing they act on.

There is no `Ctrl+Q`. Closing the editor does not yet ask about an unsaved
scene - only switching *project* does - so a single keystroke that quit would
be a single keystroke that discarded an hour. The keystroke can come after the
question.

### Undo

**`Ctrl+Z` takes back whatever you last did to the scene**, and it does not
have a list of things it knows how to reverse. It works by snapshotting the
whole scene - the same mechanism that makes **Play** non-destructive - so
anything that is part of the scene is covered:

- moving, rotating and scaling, including a gizmo drag
- every field of every component in the Inspector, and script variables
- creating, deleting, renaming, reparenting and duplicating entities
- **painting tiles**, including *Clear every cell* - a whole brush stroke is
  one step, not one step per cell

**One step is one action.** A drag across the screen is a single `Ctrl+Z`, not
a hundred - the snapshot is taken when you let go, not while you are holding
on. The same goes for typing a value: the step is recorded when you click away
or press Enter, so `Ctrl+Z` does not walk back a letter at a time.

**It remembers 64 steps**, and the oldest is dropped rather than refusing to
record a new one.

**What it does not cover**, and these are honest limits rather than bugs:

- **Anything that is not the scene.** Files on disk above all: deleting an
  asset in the **Assets** panel is gone, which is why that is the one dialog
  in the editor that always asks. Editing a `.otile` is the same - a tile asset
  is its own file.
- **Applying to a prefab.** *Apply* writes the `.oprefab` file, and a file is
  not in the scene. `Ctrl+Z` afterwards takes back the scene-side part and
  leaves the prefab changed, which is the same deal as saving. *Revert* is
  entirely inside the scene, so that one undoes normally. See
  [Prefabs](#prefabs).
- **A prefab edited outside the editor.** When you alt-tab back, every instance
  of it rebuilds - and the undo history is cleared, for the same reason a scene
  change clears it. A step back into a scene holding the prefab's old contents
  would read as a pile of deliberate overrides and quietly detach the instance
  from it.
- **The scene view camera.** Undo deliberately does not move your viewport,
  because you are looking at the thing you are undoing.
- **Align game camera**, when the scene has no camera entity. With no camera
  entity there is nothing in the scene to change, so it writes to the engine's
  camera directly and no snapshot can see it.
- **Play sessions.** Undo is unavailable while playing, because **Stop**
  already puts the scene back - the menu item says so if you hover it.
- **A scene change.** Opening a scene, starting a new one or closing one clears
  the history, because stepping back from one scene into another is not what
  undo means anywhere. That is also why those three now **ask first** when
  there is unsaved work - every route to them, not just the menu items.
  **Reload Scene** is the one exception, because discarding is what reloading
  means.

The menu item's tooltip tells you how many steps are left.

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
| Place a prefab under it     | drag a `.oprefab` from **Assets** onto it |
| Save it as a prefab         | drag it onto **Assets**, or right-click → **Create Prefab** |

Rows drawn in **blue** belong to a prefab instance; right-clicking one offers
**Apply**, **Revert**, **Unpack** and **Select Prefab Asset**. See
[Prefabs](#prefabs).

Rows drawn **greyed out** are switched off, and so is everything under them.
Hover one to be told whether it is off itself or because a parent is - the
Inspector's checkbox is where you change it, and
[the Inspector](#the-checkbox-and-the-tag-box-under-the-name) says what being
off actually means. Greyed beats blue: a prefab member that is switched off is
not doing anything, and that is the more urgent fact about it.

---

## Inspector

Everything on the selected entity, editable. The transform's position and
rotation, the sprite's image and colour, a collider's size and layers, a
script's binding.

Use the **+ ComponentName** buttons at the bottom to add a component.

### Pointing a component at an image

A component that uses a picture — **Sprite**, **Sprite Animation**, **UI
Image** — shows it as a **slot**: a button carrying the image's *name*, not its
path. `textures/Sprites/Blue/hero.png` reads as **hero.png**, and one frame of
a sliced sheet reads as **hero.png#walk** — the sheet's name stays, because a
project with four sheets has four animations called `walk`.

Three ways to fill one:

- **Drag an image onto the slot** from the Assets panel. That *replaces* what
  the slot holds. Dropping anywhere else on the Inspector still means "give
  this entity the asset", which may add a component instead — the slot under
  your cursor wins, which is ImGui's own rule rather than luck.
- **Click it** for a list of every image in the project, with a filter box.
  Sliced sheets contribute their frames; a Sprite Animation slot lists the
  animations instead, because an animation is a thing stored *inside* a sheet.
- **(none)** at the top of that list clears it.

The full path is in the tooltip, for the one moment you want it. A slot whose
image is set and **not loading** is red, which is the one thing a name alone
cannot tell you.

Dropping a whole image on a **Sprite Animation** slot is accepted and then
explained — *"that is a whole image, not a part of one"* — rather than refused,
because a refused drop leaves you holding the thing you meant with no idea why
nothing happened.

The **Tile Palette**'s art field is deliberately *not* one of these slots: it
is a field of a `.otile` file rather than a reference, and the panel's whole
design is that a wrong path turns red as you type it. It takes a drop too, and
keeps the text box.

### The checkbox and the Tag box, under the name

Two things that belong to the ENTITY rather than to any of its components, so
they sit above all of them.

**The checkbox switches the entity off.** It keeps everything it has - its
components, its values, its place in the hierarchy - and it still saves and
loads. But nothing acts on it: it is not drawn, it does not collide, it is
invisible to every physics query, its scripts do not run, its interface is
neither drawn nor clickable, and a camera on it cannot be the active camera.

Switching off a parent switches off everything under it, **without touching
their own checkboxes** - so switching the parent back on restores exactly what
was there. A row that is off for that reason is greyed in the Hierarchy and the
Inspector says "off: a parent is off" beside its ticked box, because an entity
that looks on and behaves off is otherwise a long afternoon.

A script reads it as `IsActive()` and writes it as `SetActive(bool)`. There are
no OnEnable / OnDisable hooks yet; writing one is a compile error that says so.

**The Tag box says what KIND of thing this is** - "Player", "Enemy",
"Pickup". It is not unique, so every bullet can carry the same one, and unlike
a collision layer it changes nothing about what the thing collides with and
works on things that have no collider at all.

A script asks `entity->HasTag("Enemy")`, or `FindEntityWithTag("Player")`, or
`scene->CountWithTag("Enemy")`. Empty means untagged, and `HasTag("")` is
false rather than true-for-everything.

### A prefab slot on a script

A script variable declared `eng::PrefabRef` appears as a slot you **drag a
prefab onto** from the Assets panel, rather than as a text box you type a path
into. It shows the file's own name, says so in red when the file is not there,
and clicking it clears the slot. That is how a spawner chooses what it spawns -
`assets/scripts/Gun.cpp` is the worked example.

**A prefab instance gets a Prefab block at the top**, listing everything about
this copy that differs from the prefab, with *Apply* and *Revert* for each one.
A changed component's header is blue. See [Prefabs](#prefabs).

**Some fields are read-only while the game is running.** That is deliberate:
while a system owns a value, anything you type into it is overwritten on the
very next step. Press Pause first.

---

## Assets

The files on disk, under one root: `assets/`. Images, scenes, prefabs and your
scripts all live here, in **whatever folder structure suits the game** -
`enemies/`, `player/`, `ui/`. Nothing is enforced.

**Drag an entity in from the Hierarchy to make a prefab of it**, in whichever
folder it lands on. See [Prefabs](#prefabs).

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

### Import settings: how an image is scaled

**Click an image here and the Inspector shows how it is imported.** The one
setting that matters for a 2D game is the scaling mode, and it has two answers:

| Button          | What it does                                        | Use it for |
| --------------- | --------------------------------------------------- | ---------- |
| **Pixel Art**   | Enlarging doubles pixels up - crisp, square pixels  | sprites drawn pixel by pixel |
| **Smooth / UI** | Enlarging blends them - soft edges                  | interface art, photographs, gradients |

A 16x16 character blown up to 64x64 can come out crisp and blocky or soft and
blurred, and **both are right, for different pictures**. Nothing in a `.png`
says which, because it is not a property of the pixels - so it is recorded
beside the file instead.

The change takes effect **before you let go of the mouse**: every sprite in the
open scene changes at once, and so does the preview in the Inspector. There is
no Apply button and nothing to reimport. The preview is deliberately drawn
larger than the image, because at its real size the setting makes no visible
difference at all.

**The settings live in a file of their own, next to the asset:**

```
assets/textures/player.png            the picture
assets/textures/player.png.oimport    how to import it
```

```json
{
  "filter": "Linear"
}
```

It is JSON, so you can open it and fix it by hand like every other asset in the
tree; the editor re-reads it when its window regains focus, the same way it
picks up a script you edited elsewhere. The Assets panel **does not show these
files** - one per image would double the length of a sprite folder - and
renaming, moving or deleting an image takes its settings with it.

**An asset with no such file uses the defaults, and the defaults are what the
engine always did**: `Nearest` for an image, `Linear` for a font. So a project
that has never touched any of this looks exactly as it did. Saving the defaults
*deletes* the file rather than writing one that says nothing, which is why
"Revert to defaults" and choosing `Pixel Art` on an untouched image come to the
same thing.

Fonts have the same setting and the opposite default, for a reason worth
knowing: a letter is a curve approximated into coverage values, and drawing
those unfiltered throws away the very thing that makes it look curved. A font
drawn *as* pixel art is the exception, and says so in its own `.oimport`.

**An exported game gets these files too.** Nothing in a scene names one - the
path is worked out from the asset's - so the exporter ships them as a step of
its own, and the Build log says how many went.

Only images and fonts have import settings. Select anything else and the
Inspector says so, and says where that kind of file *is* edited.

### Spritesheets: slicing one image into many

**Tick "This image is a spritesheet" and the Inspector grows a slicer.** A
spritesheet is one file holding many pictures, and slicing it gives each one a
name - after which it behaves exactly like a separate image file that happens
not to exist:

```
textures/hero.png          the sheet
textures/hero.png#f0       the first frame, usable anywhere an image is
textures/hero.png#walk     an animation, stored in the same place
```

Drop one on a sprite, name one in a tile, type one into the Texture box. There
is **no extraction step and no new files** - the frames are rectangles recorded
in the same `.oimport`, and the engine cuts one out when something asks for it.

**Setting the grid.** Give it a cell size, or say how many cells there are
across and down and let it work the size out. `Margin` is the border skipped on
all four sides; `Spacing` is the gap between neighbouring cells. The preview
draws the grid over the image as you drag, and underneath it says what the grid
comes to:

```
3 across, 4 down - 12 frame(s) of 16 x 20
```

If the grid does not divide the image exactly, it says how much is left over.
Blank space at the edge of a sheet is perfectly normal, so this is a warning
rather than a refusal - but far more often it means one of the four numbers is
not quite right.

**"Guess from the image size" works from the width and height only.** It
answers the largest square cell that divides the image exactly both ways. That
is right for the grid sheets every exporter produces and wrong for a sheet with
uneven padding, and it is worth knowing why it cannot do better: the editor has
no decoder of its own and no copy of the pixels - they went to the graphics card
and cannot be read back - so the dimensions are genuinely all there is to go
on. Always check the preview.

**Then press Slice.** The frames appear in a list underneath, each with its
name and rectangle; open one to rename it or to type its rectangle in by hand.
Clicking a frame in the preview shows that frame on its own.

Slicing again **replaces every frame**, so a name you changed by hand will not
survive it. Animations are kept, because they refer to frames by name.

**Animations are stored in the sheet too**, with a frame rate and whether they
repeat. "One animation per row" makes `row0`, `row1`, ... in one press, which
is the layout most sheets use. An animation's own path - `hero.png#walk` - loads
as a still of its first frame, because a path names a picture.

**Drag that animation onto an entity and it plays.** The drop does both halves:
the sprite gets the still, and a **Sprite Animation** component appears beside
it to flip through the frames. Press Play and it runs.

**And a single frame can become a tile.** Right-click one → **Make a Tile from
this frame**, and you get a `.otile` that draws exactly that frame. The frame
stays part of its sheet; nothing is copied out. See [Two kinds of
tile](#two-kinds-of-tile).

That component holds one thing - the animation's path - and reads everything
else from the sheet, so changing the frame rate or re-slicing is an edit to one
file and every entity playing it follows. Its two settings are the entity's own:

| Setting | What it does |
| ------- | ------------ |
| **Speed** | A multiplier on the sheet's rate, so `2` is twice as fast. One fast enemy, same animation. |
| **Play when the scene starts** | Untick it for something a script starts - a chest that opens when the player reaches it. |

The frame rate, whether it repeats, and whether replaying it carries on are
**not** here. They belong to the animation, in the sheet, which is where the
Animations list above edits them - so forty enemies sharing one walk cycle
cannot disagree about how fast it is.

**Carry on where it left off** is the third of those, and it is per *animation*
rather than per entity on purpose: one entity plays a walk and a swing from the
same sheet through one component, so a setting on the component could not tell
them apart.

| | |
| --- | --- |
| **on** | playing this animation while it is already the one loaded picks up from the frame it stopped on. A walk cycle wants this: stopping and setting off again should not visibly hitch. |
| **off** | it starts over, which is what every animation did before the tick box existed. A swing or a jump wants this, or the second one begins half way through the first. |

Two things it does not change. Switching to a **different** animation always
begins at that one's first frame — there is one playback position and it
belonged to the clip being left. And a non-looping clip that ran to its **end**
always starts again, because there is nothing to carry on with.

**It does not play while you are editing.** Animations run on the simulation
clock, and that clock is stopped outside Play mode. Press Play and both views
animate; **Pause** and then **Step** advances one frame at a time, which is the
frame-by-frame animation viewer.

A non-looping animation **holds its last frame** when it finishes. Nothing is
destroyed and nothing is reported - that is what "played once" means - and a
script can ask `IsFinished()` to act on it.

Every copy of an entity starts its animation together, so forty enemies loaded
from one scene file animate in lockstep. There is no per-entity start offset
yet.

A few rules the editor enforces rather than explains twice:

- A frame and an animation **cannot share a name**, because `#walk` has to mean
  one thing.
- An animation **cannot be empty**. Delete the animation instead of its last
  frame.
- A frame's name cannot contain `/`, `\`, `#`, `:` or `.` - these are names like
  `walk_1`, not filenames.

Everything about a frame other than its rectangle belongs to the sheet. Select
one and the Inspector shows what it is and offers the way back to the sheet;
there is nothing to set on it, because scaling is the sheet's and a frame has no
file of its own.

**One real limit, said in the panel too:** re-slicing cannot reach a frame
something is already drawing. Sprites in the open scene keep the picture they
have until the scene is reloaded.

### A sliced sheet opens like a folder

Once an image has frames, its tile in the Assets panel grows a **disclosure
arrow** beside its name. Click it and the frames appear indented underneath, as
tiles of their own - and from that point they behave like any other image:

- drag one onto an entity to give it that sprite
- drag one into the Scene view to place it
- drag one onto a UI Image's Texture field
- click one and the Inspector shows which rectangle of which sheet it is

The animations are in the same run, after the frames, each showing its first
frame as its picture. There is nothing to extract and no files appear: a frame
tile is drawn straight out of the sheet's own thumbnail, so **opening a sheet of
ninety-six frames loads nothing at all**.

The arrow appears the moment you press Slice - you do not have to press Refresh
- and a sheet you have opened stays open while you work, including across
navigating away and back.

**A frame is not a file, and the panel is honest about that.** Rename and Delete
are not offered on one, by key or by menu, because there is nothing on disk to
rename: a frame's name and rectangle are edited in the Inspector's Frames list,
which is where the right-click menu points you. Nor can a frame be dragged into
another folder, and the drag preview does not offer it.

A frame whose rectangle reaches outside its image shows a plain `FRM` chip
rather than a picture, with the reason in its tooltip. So does an animation
whose frames have all been renamed away. That matters more than it sounds: the
alternative is a tile that looks like a real frame while the same frame refuses
to load the moment it is dropped on something.

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

**Play is greyed out while the scripts are compiling**, and hovering it says
so. A build unloads the script libraries for as long as it runs, so a game
started during one would have none of its behaviour attached - it would open,
draw the scene, and sit there. That is most likely on the first few seconds of
opening a project the editor has not built before, which takes eight to fifteen
seconds; after that a rebuild is usually under a second and you will never see
this. Nothing needs doing: the button comes back by itself.

---

## Editing a scene

**File → Open Scene** lists everything in `assets/scenes/`. Dropping a
`.oscene` in there puts it in the menu with no rebuild. **Open Scene...**
(`Ctrl+O`) is the same list as a dialog, for when your hands are on the
keyboard. Either way, you are asked first if the open scene has unsaved work -
and so are the other four routes, since double-clicking a scene in **Assets**,
right-clicking one there, dropping one on the Scene view and the **Build**
panel's *Open* button all go through the same gate.

**Reload Scene is the exception and asks nothing**, deliberately: reloading
*means* "read the file again and throw away what I have done", so a dialog
would be asking whether you meant what you just clicked. Undo does not survive
it either. The menu item's tooltip says so.

**File → Save Scene**, or **Ctrl+S** - which opens **Save Scene As...** when
the scene has no file yet, rather than doing nothing. The menu bar shows
**UNSAVED** once anything has been changed.

Two things worth knowing:

- **Destroy is deferred.** An entity disappears at the end of the current
  simulation step rather than the instant you click, because the editor obeys
  the same rules as the engine. See
  `engine/include/engine/scene/DeferredOps.h`.
- **`Ctrl+Z` undoes it**, and so does **Edit → Undo**. See
  [Undo](#undo) for what that covers and the few things it does not.

Things the editor deliberately does not do: no rotate or scale handles (move
only), no dragging to reparent in the Hierarchy, and the Assets panel creates
files but does not rename, move or delete them.

---

## Prefabs

**A prefab is an entity you saved, and every copy of it stays linked to what it
was saved from.** Twenty copies of an enemy are not twenty enemies: they are
twenty instances of one enemy. Fix the enemy and all twenty change. Change one
instance and only that one changes, and the editor remembers exactly *which
parts* of it you changed, so everything else still follows.

That last sentence is the whole feature. Without it a prefab is a copy-paste
button, and copy-paste is exactly what stops working at the point a game is big
enough to need it - the moment you have to fix a mistake in all twenty.

### Making one

Three ways, and all three turn the entity you started from into an instance of
the new prefab. What you were working on is now linked, so the next change you
make to it can be pushed to every copy:

| How | Where the file goes |
| --- | --- |
| Drag an entity from **Hierarchy** onto the **Assets** panel | the folder being shown |
| Drag it onto a folder tile in **Assets** | that folder |
| Right-click it in **Hierarchy** → **Create Prefab** | `assets/prefabs/` |

The file is named after the entity, with the extension `.oprefab`. **It refuses
to overwrite an existing prefab**, because writing over one reaches every
instance of it in every scene in the project - that is what *Apply* is for, and
it should be something you asked for by name.

### Placing one

| How | Where it lands |
| --- | --- |
| Drag the `.oprefab` into the **Scene view** | where you dropped it |
| Drag it onto an entity in **Hierarchy** | as a child of that entity |
| Double-click it in **Assets**, or right-click → **Place in Scene** | where the Scene view is looking |
| Select an instance and press **`Ctrl+D`** | on top of the original, as another instance |

**Every member of an instance is drawn in blue** in the Hierarchy and the
Inspector. That is the one signal that says editing this is editing one copy of
something; hovering a row says which prefab.

### Changing one copy

Just edit it. Move it, change its colour, add a component. The Inspector's
**Prefab** block at the top lists everything about this instance that differs
from the prefab, with two buttons beside each entry:

- **Revert** puts that one thing back to what the prefab says.
- **Apply** writes that one thing *into the prefab*, so every other instance
  gets it too.
- **Apply All** / **Revert All** do the lot.
- **Unpack** breaks the link. Nothing moves and nothing is destroyed - the
  entities just become ordinary ones, and the prefab no longer reaches them.

A component you have changed has its header in blue, with the changed fields
listed underneath by the name they are saved under.

**Where an instance stands is not a change to the prefab.** Its position and
rotation are deliberately left out of the list, left alone by *Apply All* and
kept by *Revert All* - otherwise applying one enemy would move every other
enemy in the project to the same spot, and reverting would teleport the one you
were working on back to the prefab's origin. Scale is *not* excluded: how big
something is reads as a decision about the object rather than about where it
stands.

### Changing the prefab itself

There is no separate prefab-editing mode. Edit any instance and press
**Apply** - which is how Unity worked before it grew one.

You can also edit the `.oprefab` by hand, since it is JSON like everything else
here. When you alt-tab back to the editor, every instance in the open scene
rebuilds and keeps its own overrides. That is the same trigger that rebuilds
your scripts.

### Two things it refuses

**A prefab's child cannot be deleted or reparented.** The root is fine -
deleting it deletes the whole instance, and it can be parented anywhere you
like. The refusal on a child is not protectiveness: the prefab says that child
exists and says where it sits, so the next rebuild would put it straight back.
A delete that silently undoes itself ten minutes later is worse than one that
refuses now. **Unpack** is the way out, and every refusal says so.

**Making a prefab out of something that is already an instance** is refused
too, because it would flatten it - the new file would hold plain copies with no
link back to the first prefab. Unpack it first, or use *Apply* to change the
prefab it came from.

### What it does not do yet

- **Nested prefabs.** A prefab containing an instance of another prefab.
- **Prefab variants.** A prefab that inherits from another prefab.
- **Prefab mode.** Unity opens a prefab in its own isolated scene view.
- **Added children are ordinary entities.** Parent something new under an
  instance and it saves and loads correctly, and the prefab leaves it alone -
  but *Apply* will not push it into the prefab. Unity tracks that as an "added
  object".
- **Overriding a value to the same value the prefab has.** Unity can pin a
  property that way, so a later change to the prefab does not arrive. Here a
  value equal to the prefab's is indistinguishable from one nobody touched, so
  it follows the prefab.

### What it looks like in the file

The scene does **not** store an instance's entities. It stores a short record,
and the entities are built from the prefab when the scene loads - which is
exactly why editing the prefab reaches every instance:

```json
"prefabInstances": [
  {
    "prefab": "prefabs/enemy.oprefab",
    "name": "Enemy_3",
    "parent": "Spawners",
    "overrides": [
      { "entity": 2, "component": "SpriteComponent", "field": "tint",
        "value": [255, 0, 0, 255] }
    ]
  }
]
```

`"entity": 2` is a number inside the prefab rather than a name. Names cannot do
that job: three instances of a prefab with a child called `Muzzle` cannot all
have a child called `Muzzle`, and renaming one in the prefab would silently
detach every override anybody had made to it. The full argument, and the format,
are in `engine/include/engine/scene/Prefab.h`.

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

### Two kinds of tile

There are two, and the one you want depends on whether the art should **join up
with itself**:

| Kind      | What it is                                      | When |
| --------- | ----------------------------------------------- | ---- |
| **Sheet** | Five bands, assembled from four quarters chosen by the neighbours | ground, water, walls — anything that forms a *surface* |
| **Image** | One picture, drawn whole. Every cell identical.  | a crate, a bush, a sign, a single frame of a spritesheet |

A sheet is the clever one: paint a line of it and the ends round themselves off.
An image is the plain one, and plain is right far more often than it sounds —
and it is a quarter of the drawing work, because there are no quarters to
assemble.

**An image tile may be one frame of a sliced spritesheet.** Its `"image"` is a
path like `textures/atlas.png#rock`, so the frame stays part of its sheet and
nothing is copied out to a file of its own. That is the quickest way to get a
pile of scenery tiles out of one atlas.

The `.otile` says which kind it is **by which key it uses** — `"sheet"` or
`"image"`. There is no separate setting to contradict it, naming both is
refused, and naming neither tells you what the difference is.

### Making a tile

A tile is a `.otile` file, and **+ Tile** in the Assets panel writes one.

1. Select the art in the Assets panel, then press **+ Tile** — its path is
   filled in for you, and so is the kind: the editor tries it as a five-band
   sheet and falls back to **Image** when it is not the right shape. A selected
   *frame* is always an image, because a frame is one picture.
2. Choose the kind yourself at the top if the guess was wrong.
3. The dialog reports what the engine makes of the art — *"32px tiles, 4
   frame(s)"*. For a sheet it shows the five bands down the side with their
   names, so a sheet whose bands are in the wrong order is visible before you
   commit to it; for an image it shows the picture.
4. If the art is the wrong shape it says so, in the loader's own words, and
   **Create** stays greyed out.

Or skip the dialog's first two steps: **right-click an image or a frame → Make
a Tile from this**. It opens the same dialog with the kind and the path already
set, and a name suggested from the art (`atlas_rock` for a frame of
`atlas.png`).

The check in the dialog *is* the loader's check: it builds the `.otile` it is
about to write and hands it to the same code that reads one at runtime. There
is no second opinion to disagree with it.

The file it writes states only the art and the collision setting. **Tile size
and frame count are deliberately left out** — they are measured from the image,
so writing them down would be a second description of something the file
already carries.

**Frames go sideways, for both kinds.** Extra columns are extra frames: a strip
of squares for an image, a strip of five-band sheets for a sheet. What they are
*for* is the **Frames** setting — one picture, an animation, or one picked per
cell and kept.

A sheet must be `tileSize` wide (times the number of frames) and `tileSize × 5`
tall, with an **even** `tileSize` — it is drawn as four half-size quarters, and
an odd size cannot be halved. An image must be one square picture or a sideways
strip of them, and **may be any size**, odd included, because it is drawn whole.

#### If your tiles come out scrambled

You have pointed `"sheet"` at an ordinary picture. It wants `"image"`.

This used to be easy to do and hard to diagnose, because a `"sheet"` was all
there was and the shape check could not catch it: the tile size is worked out as
the image's height ÷ 5, so asking "is this five tiles tall?" is asking whether
the height is five times a fifth of itself — true of **every image whose height
is a multiple of ten**. A 48×80 character spritesheet was accepted as a 16px
tile with three frames and then drawn as four quarters cut from the wrong
places.

Two things now stand in the way. The dialog **guesses by trying it as a sheet
first**, so anything that cannot be one arrives as an **Image** — and a
`"sheet"` pointing at an image the slicer has cut into frames is refused
outright, naming the frame it would have used instead. Slicing a picture is a
statement that it is a spritesheet, and no measurement can supply that.

What is left is narrow but real, and worth knowing about: **an unsliced image
whose height happens to be a multiple of ten still passes as a sheet**, so the
dialog will still offer Sheet for one. Nothing can tell it apart — a tile sheet
and a spritesheet of those dimensions are the same image. The five labelled
bands beside the preview are what that mistake looks like *before* you write the
file: if "outer corner" and "fill" are pointing at bits of a character, choose
**Image**.

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

**Art** at the top switches between the two kinds. The path moves across with
it, and any `tileSize` or `frames` already in the file are dropped — a size
worked out for one kind is wrong by a factor of five for the other, so it is
re-measured rather than converted.

**Show all 47 shapes** draws every arrangement of neighbours the sheet can
produce, assembled from its quarters exactly as the renderer assembles them.
This is the quickest way to find a sheet that is subtly wrong: hover any of
them to see which band each quarter came from. For an animated sheet, the
**Preview frame** slider picks the column.

An image tile has neither of those: one picture has no bands to be in the wrong
order and no shapes to vary, so the panel shows the picture and says so rather
than greying out a row of controls that could never mean anything.

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

**`Ctrl+Z` takes back a whole stroke.** A tilemap's cells are part of the
scene, so painting is undone like any other edit - and the step covers the
whole stroke rather than one cell, because it is recorded when you let go.
Erase is still the quicker fix for a single wrong cell.

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

**No console window appears while it compiles.** The compiler is a separate
program and it used to be given one, which opened over whatever you were doing
and took the keyboard. Worse, it was a window, so it invited closing - and
closing it killed the compiler mid-build, which came back as scripts that
"did not compile" with no error to explain it. Everything the compiler prints
goes to the Console panel and to `.build/build.log` instead.

**Each script is its own library.** `assets/scripts/Orbiter.cpp` becomes
`.build/modules/Orbiter__<id>.dll`, and the engine loads that whole folder.
What happens on a rebuild:

1. every running script object is destroyed and the libraries are unloaded
2. **only the scripts whose source actually changed** are recompiled and
   relinked
3. they are all loaded again, and every script in the scene is reattached
   **by name**

So a scene you are editing keeps working across a rebuild with nothing
reattached by hand. Play mode is stopped first, the way Unity does, because a
running play session cannot survive its own code being replaced.

Changing one script takes a few seconds rather than rebuilding everything.
Changing an **engine header** still rebuilds every script, deliberately:
working out which script included which header needs the compiler's own
dependency output, and being wrong about it means running stale code while the
Console says the build succeeded.

### When one script will not compile

**The others keep working.** Only the broken one is affected, and the Console
names it:

```
scripts/Orbiter.cpp did not compile - the version from the last successful
build is still what runs
C:/dev/GameEngine/assets/scripts/Orbiter.cpp(170): error C2059: syntax error
```

Note the second line: the error points at **your file and your line number**,
even though what the compiler was handed is a generated wrapper. That is why
the wrapper `#include`s your source instead of copying it.

"The version from the last successful build is still what runs" is literal — a
script that has compiled before keeps its last working library, so the game
stays playable while one file is mid-edit. A script that has *never* compiled
has no library and shows as **NOT FOUND** in the Inspector instead. Either way
**SCRIPTS DID NOT BUILD** appears in the menu bar until it is fixed.

**The error is reported once, not on a loop.** A failed build is not tried
again until one of the files it was built from actually changes, so the
compiler's explanation stays where you can read it instead of being pushed up
the Console by copies of itself. Save a fix and the next time the editor
regains focus it builds again, as usual; **Build Scripts** in the Assets panel
always builds on the spot, whether anything changed or not.

### What an exported game gets

**One `userContent.dll`**, not the per-script libraries. A shipped game is never
going to recompile a script, and eleven libraries would mean eleven import
tables and eleven lots of load-time work for nothing.

It is the same object files linked a different way, so exporting straight after
a build is a link and not a rebuild — a second or two.

**This needs a C++ compiler on your machine.** That is unavoidable: these are
real compiled C++ scripts. On Windows install Visual Studio or the standalone
Build Tools; on macOS the Xcode command line tools; on Linux `g++` or
`clang++`. The editor says which compiler it found in the Console at start-up,
and says so plainly if it found none.

The connection between an entity and its script is **by name**, so a script
that has not compiled yet still attaches to an entity, still saves into the
scene, and shows as **NOT FOUND** in red in the Inspector until it builds.

**On the first open of a project, expect to see that for a few seconds.** The
editor starts, finds nothing compiled, and begins a build - and a build unloads
the libraries while it runs, so every script in the scene reads NOT FOUND until
it finishes. The Inspector says which of the two it is: a compile in progress,
or a script that genuinely is not in this build. Play is unavailable until the
build is done, for the same reason, and neither state needs anything from you.

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

**Declare a public variable and it is there.** No annotation, no registration,
nothing to keep in step. It can be dragged **while the game is running**, and
it is saved on each entity separately — so one script drives two entities that
behave differently:

```cpp
class Orbiter : public eng::ScriptBehaviour {
public:
    float radius = 90.0f;   // how far out it orbits
    float speed  = 1.2f;    // [0..10] turns per second

    void OnFixedUpdate() { /* read radius and speed here */ }
};
```

The **trailing comment becomes the tooltip**, and a `[low..high]` at the
*start* of that comment makes it a slider instead of a drag box. Use a range
only when the ends are part of what the number means — a 0-to-1 blend, a
percentage. A distance has no upper limit and a slider would invent one. A
bracket group anywhere other than the start is left alone as ordinary prose.

| the comment | what you get |
| --- | --- |
| `// how fast it goes` | a drag box, with that tooltip |
| `// [0..10] how fast` | a slider from 0 to 10, tooltip "how fast" |
| `// [Rigidbody2DComponent] what to push` | on an `EntityRef`, narrows the picker |
| `// see [1] for why` | a drag box; the bracket is not at the start |

**To keep a variable off screen**, make it `private`, or name it `m_something`
— this project's convention for internals is respected as a deliberate "not
this one", so a public `m_body` is left out silently.

A `static constexpr` constant is **not** exposed, and the Console says which
one and why. Make it an ordinary member to tune it.

#### How it works, and what it misses

C++ has no reflection — `requires` can ask whether a *named* member exists, but
nothing can ask what members exist. So the editor **reads the script's source**
when it builds it, the way Unreal's header tool does, and generates the
registration. `engine/tools/ScriptVars.h` has the detail.

It is a whitelist of declaration shapes, not a C++ parser, so it will miss
unusual ones. Three things make missing them cheap:

- anything unrecognised is simply not exposed — the same as before;
- the generated code wraps every variable in `if constexpr (requires ...)`, so
  a variable it got wrong **compiles away** rather than breaking your build;
- the Console lists what it exposed and what it skipped, with the reason.

#### When to write it yourself

A script can still declare the list by hand, and **the hand-written one wins**:

```cpp
void OnExpose(eng::ScriptFields& fields) {
    fields.Var("radius", radius, "how far out it orbits");
    fields.Range("speed", speed, 0.0f, 10.0f);
    fields.Ref("body", body, "Rigidbody2DComponent", "what to push");
}
```

Reach for it when the list is not a fixed set of declarations — a variable
exposed only in some builds, a name worked out at run time, or a declaration
the parser cannot see through. Nothing in `assets/scripts` needs it.

Seven types, and only ordinary members:

| | drawn as |
| --- | --- |
| `float`, `int` | a drag box, or a slider with `Range` |
| `bool` | a checkbox |
| `std::string` | a text box |
| `eng::Vec2` | two numbers |
| `eng::Color` | a colour picker |
| `eng::EntityRef` | another entity — drag one from the Hierarchy |

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

### Pointing one entity at another

An `eng::EntityRef` is a reference to another entity. **Drag an entity from the
Hierarchy onto it**, or click it for a list:

```cpp
eng::EntityRef target;   // what to chase
eng::EntityRef body;     // [Rigidbody2DComponent] what to push

void OnFixedUpdate() {
    if (eng::Entity* enemy = target.Target()) { /* chase it */ }
    if (auto* push = body.Find<eng::Rigidbody2DComponent>()) { /* shove it */ }
}
```

Plain, it takes anything. **A `[TypeName]` marker narrows it** to entities
carrying that component or script — `[Rigidbody2DComponent]`, `[DoorSwitch]` —
and the Inspector uses it twice: the picker offers only entities that have one,
and a reference pointing at something that does not says so.

The type is named as **text**, which is what lets one mechanism serve both a
component the engine ships and a script you wrote this afternoon. Writing the
list by hand, it is `fields.Ref(name, ref, "Rigidbody2DComponent", tooltip)` —
mandatory there, so a tooltip can never be mistaken for one.

| the field shows | meaning |
| --- | --- |
| `(none)`, greyed | nothing set |
| the name, plainly | set, and it resolves |
| the name **in red** | set, and nothing in the scene has that name |
| the name + amber | it is there, but has no `Rigidbody2DComponent` on it |

**A reference stores the target's NAME**, not a pointer and not an id. A name
survives being written to a file, is what the Hierarchy shows you, and is what
you would type editing the scene by hand — `"target": "Player"`. Entity names
are kept unique, which is what makes it unambiguous. An id would have been
worse than it looks: it is a slot number decided by the order a scene happens
to load in, so inserting an entity above another in the file would silently
re-point every reference to it.

**Renaming an entity moves every reference with it**, including the saved
values, so renaming things in the Hierarchy is safe. The one exception is a
script that is not compiled into this build: with no script bound, nothing can
say which of its saved values are references, so those are left alone and show
in red against the old name rather than being guessed at.

Ask for the target when you need it and do not keep it — `Target()` is a hash
lookup, and the entity may have been destroyed since last frame. That is the
same rule the rest of the engine follows for `EntityId`.

To reach another **script's own members** you need its class in a header both
files include, because a script in `assets/scripts` sits in an anonymous
namespace and no other file can name its type. Most references do not need
that: `fields.Ref(..., "DoorSwitch", ...)` plus a transform or a message gets
you there with no headers at all. See `engine/include/engine/scene/EntityRef.h`
and `FindScriptOn` in `ScriptComponent.h`.

`assets/scripts/GameRules.cpp` is the worked example — its three HUD labels
used to be names written in the C++.

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

---

## Exporting a standalone game

**File → Build Game...** asks for a folder and puts a game in it that runs on
its own: `<FolderName>.exe`, `engine.dll`, `SDL3.dll`, and an `assets/` folder
holding one `.bin` archive per top-level asset folder plus the compiled
scripts. Double-click the exe and it plays. The Console lists every file as it
goes and finishes with a count.

**Only the assets your scenes actually refer to are shipped.** The exporter
reads the scenes in the build list, follows what they name - textures, tiles,
prefabs, and the import settings beside them - and packs that. It says how many
it left behind, which is the number worth looking at: an asset a script loads
by a name it builds while running cannot be found this way, so list those in
`assets/export.json`.

**It is all or nothing.** If any part fails - a file it cannot write, a locked
`engine.dll` in the destination, scripts that will not compile - the export
stops, says why as an error, and **removes the files it had already written**.
A folder holding a game executable and no engine.dll looks finished and is not;
it fails when somebody runs it, which may be somebody you handed it to. Files
that were already in that folder are never touched.

**It refuses to export into your own project**, because the copy would find its
own output and copy that too, and the first sign of trouble would be the disk
filling up. Pick a folder somewhere else.

**A Debug build makes a game you cannot hand out.** It needs the debug C++
runtime, which Microsoft does not allow to be redistributed, so it runs only on
a machine with Visual Studio installed. The Console says so after every Debug
export. Build the Release preset and export again for a game you intend to
give to somebody; that one needs only the Visual C++ Redistributable, which
most machines already have.
