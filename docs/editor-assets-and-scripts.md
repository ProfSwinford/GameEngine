# The asset browser, drag-and-drop, and scripts

*Post-Week-10 editor work, built on the Scene/Game view split described in
[editor-scene-and-game-views.md](editor-scene-and-game-views.md).*

Three things arrived together, because each is useless without the others: a
browser that can see files on disk, a way to drag those files onto the world,
and something worth dragging.

---

## First, a bug: the translate gizmo did not move anything

Reported as *"when using the translate gizmo handles, the object doesn't
move"*, and the cause is worth writing down because the code looked correct.

`ScenePanel::Draw` called, in this order:

```cpp
HandlePicking(...);   // click -> select whatever is under the cursor
DrawGizmo(...);       // grab a handle, drag the selection
```

and `HandlePicking` guarded itself with:

```cpp
if (m_hovered && m_dragAxis == GizmoAxis::None && ImGui::IsMouseClicked(...))
```

`m_dragAxis` is set by `DrawGizmo` — **later in the same frame**. So on the one
frame that matters, the frame of the press, the guard was always open. Clicking
the red X handle, which sits sixty pixels away from the object over empty
space, ran a pick there, cleared the selection, and the gizmo it was about to
grab no longer had an entity to move. Nothing moved, and nothing looked broken
enough to point at.

**A guard against state produced later in the same frame is not a guard.** The
fix is not "call them in the other order" — it is to split the gizmo into an
input pass and a draw pass, so the ordering requirement is explicit rather than
implicit in where a draw call happened to sit:

```cpp
UpdateGizmo(...);     // input.  Gets first refusal on the click.
HandlePicking(...);   // only runs if the gizmo did not take it.
DrawGizmo(...);       // pure output.
```

Axis constraint, verified on screen: dragging the **X** arm 180 units right
while travelling 120px *downward* moved the entity `160 → 340` on X with Y
unchanged at `0.000`. Dragging the **Y** arm while travelling 196px *right*
moved `-200 → -32` on Y with X unchanged at `0.000`. The dragged arm also draws
thicker, so the active constraint is visible.

---

## The asset browser

The Week 9 Resource panel lists what is **loaded** — handles, refcounts, the M3
evidence. It can only ever show things a scene already asked for, so it cannot
answer *"what textures do I have"*, which is the question you ask before
authoring anything. The browser walks the **directory** instead.

### Two roots, and the split is not cosmetic

| | |
|---|---|
| `assets/` | shipped data — textures, scenes. Loaded at runtime by virtual path. |
| `gamescripts/` | C++ **source** the build compiles. Never shipped, never loaded. |

Presenting them as two roots rather than one tree keeps that distinction
visible. A browser that showed scripts nested inside `assets/` would be quietly
teaching that source is data — and the day someone writes a packaging step,
that mistake ships the game's source with the game.

`FileSystem::Resolve` already had this concept for `config/` and `logs/`, which
live beside `assets/` rather than inside it. `gamescripts/` joins that list.

**The assets root is the empty virtual path**, and that is not a shortcut: the
virtual path space is *already* rooted at `assets/`, so the virtual path OF
`assets/` is `""`. The first version used `"assets"`, asked for
`<root>/assets/assets`, and the panel correctly reported that it did not exist.

### Why the directory is not the game-scripts directory

`scripts/` already existed in this repository, holding `fresh-clone-check.sh`,
and two milestone documents reference it by path. Taking the name over would
have made Week 1 and Week 3 evidence describe a file that had moved. The game
scripts went to `gamescripts/` instead — which also matches the CMake target
name, so the directory and the library agree.

### Two costs, stated rather than discovered

- **Thumbnails are real loads.** The browser `Acquire`s a texture handle for
  every image it shows, so those textures appear in the Resource panel with a
  refcount the scene did not ask for. That is honest — they really are
  resident — and they are released on navigation and in the destructor. Anyone
  reading the M3 refcount table with this panel open should know why the
  numbers are higher.
- **The listing is cached**, refreshed on navigation, on create, and on the
  Refresh button. A directory scan per frame is exactly the cost a debug tool
  must not impose on the thing it is observing.

---

## Drag and drop

| Drag | Onto | Result |
|---|---|---|
| texture | Scene view | new entity **at the cursor's world position**, with that sprite, selected |
| texture | Hierarchy row | that entity gets the sprite (adding the component if absent) |
| texture | Inspector | the same, for the selected entity |
| script | Hierarchy row | attaches a `ScriptComponent` bound to that script |
| script | Inspector | the same |
| scene | Scene view | loads it |

Three implementation notes that are each one bug avoided:

**The payload ids live in one header.** ImGui matches a source to a target by a
string, and a typo does not fail loudly — the drop is simply never accepted,
with nothing on screen to explain why.

**`BeginDragDropTarget` binds to the LAST SUBMITTED ITEM.** The Scene view's
target is written immediately after `ImGui::Image`; moving it below the gizmo
code would silently target something else.

**Scene loads are deferred through `EditorState::requestedScene`.** Loading
destroys every entity, and a drop happens in the middle of a frame in which
other panels are holding pointers into those entities. It is the argument
`DeferredOps` makes for the engine, arriving at the editor — and the editor
does not get to be exempt from the engine's own rules.

The drop position matters: it is the **cursor's** world position, not the
camera centre. Dragging something to a particular spot and having it appear
somewhere else is what makes a tool feel like it is not listening.

---

## Scripts

The honest difference from Unity, stated first: **a script here is compiled
C++, not an interpreted file.** There is no VM, and pretending otherwise by
"hot-loading" a `.cpp` would be a lie the first time someone pressed Play.

What that costs: creating a script writes the file and reconfigures nothing; it
runs after a rebuild. What it does **not** cost is the rest of the workflow.

### The binding is BY NAME, and that is the whole design

```
ScriptBehaviour    what you write. Virtual hooks, Unity's names.
ScriptRegistry     name -> factory, populated by ENGINE_REGISTER_SCRIPT.
ScriptComponent    the engine component. Stores a NAME, and an instance if
                   that name is compiled into this build.
```

Drop `PlayerController.cpp` onto an entity in a build where it has not been
compiled yet, and the component **attaches, serialises, and reports itself
UNRESOLVED in red**. Recompile, relaunch, and the same scene file produces a
live behaviour with nothing reattached.

A component that refused to attach until the type existed would make the editor
useless for authoring anything not already built — and *author the scene, then
write the code* is a completely normal order to work in. This is the argument
`Component.h` makes for identity being a runtime **string** rather than a C++
type, followed one step further.

**The name is written to the scene file whether or not it resolved.** Dropping
it from the save would silently delete the author's work the first time they
saved a scene from a build that lacked their script.

### The lifecycle

| | |
|---|---|
| `OnStart()` | once, on the first simulation step after attach |
| `OnUpdate(dt)` | every **fixed** step, at stage 200 — before movement (300) and collision (400) |
| `OnDestroy()` | entity going away; still valid here, not after |
| `OnCollisionEnter/Stay/Exit(other)` | forwarded from the message bus |

`OnStart` runs on the first **tick**, not at attach. At attach time a scene load
may not have built the rest of the entity — components are attached one at a
time — so a script looking for its sibling collider in `OnAttach` would find it
only if the file happened to list the collider first.

`other` is an `EntityHandle`, never a pointer, and may already be dead. That
rule is not specific to scripts; it is how the whole engine works.

### Three linking details

**`WHOLE_ARCHIVE`.** Script registrars are file-scope objects that nothing
references by name — that is the point of registration by name. A normal
static-library link discards every one of them, and the only symptom is that
nothing happens when you press Play. Both `editor` and `sandbox` link
`gamescripts` with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`, and the boot log prints
the registered count so a zero is visible immediately:

```
1 script(s) registered
    script 'Orbiter'
```

**The registry table is a function-local static.** Registrar construction order
across translation units is unspecified; a namespace-scope table could be
constructed *after* the first registrar that fills it. Function-local statics
construct on first use, so whichever registrar runs first builds the table.
That is the static initialization order fiasco avoided rather than survived.
(`ComponentFactory` sidesteps it differently — an explicit `RegisterBuiltins()`
at boot — because it *can*; scripts cannot, since the engine does not know
their names.)

**One glob, deliberately.** Every other target lists its sources by hand, so a
new file is a reviewed change. Scripts are the exception because the editor
*creates* them, and a "New Script" flow that requires hand-editing a
CMakeLists before the file can compile is a flow nobody would use twice.
`CONFIGURE_DEPENDS` makes it safe: the generator re-checks the directory each
build and reconfigures when it changes.

### The default script

"New Script" writes the file from a template with every hook stubbed and
commented — the same idea as Unity's default MonoBehaviour, which teaches more
people the lifecycle than the manual does, because the explanation is in the
file you are already looking at. This one carries one extra obligation: it says
in a banner that the file is compiled C++ and needs a rebuild, or the first
experience of the feature is writing a script that never runs.

The name is validated **as it is typed** — it becomes a C++ class name and is
pasted into a macro — rather than after the user commits to it.

---

## Verification

```
sandbox --script-check
```

```
--- a script that IS compiled in ---
  ScriptComponent attaches                                   ok
  'Orbiter' resolves to a behaviour                          ok
  moved (500.00,500.00) -> (565.24,562.00)
  OnUpdate actually ran (it moved)                           ok
--- a script that is NOT compiled in ---
  an unknown script still attaches                           ok
  and reports itself unresolved                              ok
  UnresolvedCount reports exactly 1                          ok
--- save / load round trip ---
  the scene saves with both scripts                          ok
  and loads again                                            ok
  the resolved binding came back                             ok
  the UNRESOLVED binding came back too                       ok
--- destruction ---
  destroying the entity deregisters its script               ok
SCRIPTS: 0 failure(s) -- PASS
```

Checking only that the script *resolves* would pass for a system that never
ticked anything, so the check moves the entity and requires that it moved. And
the unresolved case is tested deliberately: it is the whole reason the
component stores a name rather than a type, and it is the behaviour easiest to
"fix" into a refusal by someone who has not read why.

The full suite after this work: 126 tests / 198,690 assertions,
`--playmode-check` PASS, both `--save-check` scenes PASS, `--collision-check`
enter/stay/exit and mask suppression correct, `--autoplay` wins with 48.5s
spare.

---

## Still not done

- No drag-to-reparent in the Hierarchy.
- Translate only. No rotate or scale gizmo.
- No undo.
- Renaming, moving or deleting files from the browser — it reads and creates,
  it does not manage.
- `CollectorGame` still lives in `sandbox` rather than in `gamescripts/`, so
  the gate game is still not playable inside the editor. The mechanism to move
  it now exists; doing so would change the Week 10 gate story, which is a
  separate decision.
