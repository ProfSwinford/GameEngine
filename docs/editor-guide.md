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

The files on disk, in two roots:

| Root           | What it holds                                                    |
| -------------- | ---------------------------------------------------------------- |
| `assets/`      | images and scenes - data the game loads while it runs             |
| `gamescripts/` | C++ source the build compiles - it becomes part of the program    |

They are kept separate on purpose. Source code is not data, and a browser that
mixed them would be teaching otherwise.

**+ Script** writes `gamescripts/<Name>.cpp` from a template with every
lifecycle hook written out and explained.

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

**File → Open Scene** lists everything in `assets/scenes/`. Dropping a `.json`
in there puts it in the menu with no rebuild.

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

## Writing a script

**Assets → + Script** writes a new file. Scripts are **compiled C++**, so
creating one writes the file and nothing else - it runs after you rebuild.

Everything else works in the meantime, because the connection is **by name**: a
script that has not been compiled yet still attaches to an entity, still saves
into the scene, and shows as **NOT FOUND** in red in the Inspector. Rebuild,
start the editor again, and the same scene file produces a working behaviour
with nothing reattached.

```cpp
class MyScript final : public eng::ScriptBehaviour {
    void OnStart() override {}                          // first step after attaching
    void OnUpdate(float dt) override {}                 // every simulation step
    void OnDestroy() override {}                        // the entity is going away
    void OnCollisionEnter(eng::EntityId other) override {}
};
ENGINE_REGISTER_SCRIPT(MyScript)   // without this it can never be found
```

`gamescripts/Orbiter.cpp` is a worked example. Read it before writing your own.

The Console prints how many scripts were compiled in when the editor starts. A
zero there while `gamescripts/` has files in it means the build is not keeping
them - see `gamescripts/ScriptsAnchor.cpp`.
