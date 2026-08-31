# A tour of the engine

Where everything lives, and which file to open when you want to understand one
particular idea. Every header listed here starts with a long comment explaining
what it does and why it is built the way it is - those comments are the real
documentation, and this page is the map to them.

---

## The shape of one frame

```
BeginFrame()     read input; work out how many simulation steps this frame owes
Simulate()       run those steps
RenderFrame()    draw
PresentFrame()   show it
```

`Engine::Run()` is those four in a loop. The editor calls them separately,
because it has to slip its panels in between drawing and showing.

Inside `Simulate()`, systems run in a written-down order:

```
100  Input               read what the player wants
200  Gameplay / scripts  decide what everything is going to do
300  Movement            actually move things
400  Collision           check overlaps at the NEW positions
500  Messages            deliver collision and gameplay events
600  Create / destroy    apply the queued changes
700  Camera              follow whatever it follows
--- once per drawn frame from here ---
800  Sprites
900  Gizmos
```

`engine/include/engine/scene/SystemOrder.h` explains why that order and not
another one.

---

## The layers

| Folder        | What lives there                                                    |
| ------------- | ------------------------------------------------------------------- |
| `core/`       | the log, the settings file, the clock, JSON helpers, start-up order  |
| `math/`       | `Vec2`, `Mat3`, `Transform2D`, overlap tests, random numbers         |
| `platform/`   | the window and reading input from the operating system              |
| `render/`     | drawing, the camera, textures, gizmos                               |
| `fs/`         | finding and reading files                                           |
| `input/`      | key presses turned into named actions                               |
| `resource/`   | loading images, and sharing them between entities                    |
| `scene/`      | entities, components, levels, scripts, messaging                    |
| `physics/`    | collision shapes and collision events                               |
| `tools/`      | starting and stopping the editor's interface                        |

---

## Where to look for one idea

| If you want to understand...                     | Open                                       |
| ------------------------------------------------ | ------------------------------------------ |
| how to print something while the game runs       | `core/Log.h`                               |
| why the game behaves the same on every machine   | `core/GameClock.h`                         |
| what an entity actually is                       | `scene/Entity.h`                           |
| how to write your own component                  | `scene/Component.h`, then `SpinComponent.h`|
| how a level file becomes a world                 | `scene/Scene.h`                            |
| how to write your own behaviour                  | `scene/ScriptComponent.h`                  |
| why deleting things is queued                    | `scene/DeferredOps.h`                      |
| how entities tell each other things              | `scene/Messaging.h`                        |
| how collision layers work                        | `physics/Collider.h`                       |
| why parenting makes things orbit                 | `math/Transform2D.h`                       |
| what a matrix is doing in a 2D game              | `math/Mat3.h`                              |
| how clicking on something works                  | `render/Camera.h`                          |
| how images are shared and unloaded               | `render/Texture.h`                         |
| how key bindings are changed without rebuilding  | `input/InputMap.h`                         |
| why files are named the short way                | `fs/FileSystem.h`                          |
| why the engine starts up in a fixed order        | `core/Subsystem.h`                         |

---

## Ideas the engine keeps coming back to

**Data, not code.** Every entity, every position, every image and every key
binding lives in a `.json` file. Search the engine's source for the name of
anything in a scene and you will not find it. That is what lets somebody build
a level without writing C++.

**Refer to entities by id, not by pointer.** Entities get destroyed while other
things are still referring to them. An `EntityId` can be CHECKED; a pointer to
something that has been freed cannot. See `scene/EntityId.h`.

**Let the standard library own things.** Textures are `std::shared_ptr`, so an
image unloads itself when the last sprite using it goes away. SDL objects are
`std::unique_ptr` with a custom deleter, so they clean themselves up. Nothing
in this engine has a matching "release" call you have to remember.

**Never change a list while something is walking it.** Creating and destroying
entities goes through a queue applied at one known point in the frame. So does
message delivery. This is the single most common source of crashes in game
code, and `scene/DeferredOps.h` explains it in detail.

**Order is written down, not accidental.** Subsystems start in a declared order
and shut down in exactly the reverse. Systems update in a declared order. Both
are printed to the log at start-up, so it is never a guess.

---

## The two programs

`sandbox` is the game with no editor attached. `editor` is the development
environment. Both are built on the same `engine` library, and both link
`gamescripts` - which is why a script written for the game also runs when you
press Play in the editor.

The engine links SDL **privately**, which means neither program can reach SDL
directly even if it wanted to. That is not a rule anybody has to remember; the
build enforces it.
