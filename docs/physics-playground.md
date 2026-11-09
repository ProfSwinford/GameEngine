# The physics playground

`assets/scenes/physics_playground.oscene` is a scene whose only job is to let
you watch the 2D physics behave, one claim at a time. Thirteen stations laid out
on a grid, four across. Each one is a parent entity in the Hierarchy named after
itself, so the Hierarchy is the index: click a station, press **F** to frame it.

Open it with **File → Open Scene**, or run it on its own:

```bash
build/debug/bin/player --scene scenes/physics_playground.oscene
```

Press **Play** in the editor, then:

| Key | What it does |
|---|---|
| **E** | reload the whole playground, so everything is back where it started |
| **Space** | re-launch every body that has a `PhysicsLauncher` on it |

The readout in the top left is the `PhysicsLab` script on the root entity:
collider count, touching pairs, how many pairs the narrow phase actually tested
against how many it would have without a broad phase, solver passes, gravity,
and queries this frame.

---

## Seeing the colliders

In the **Scene view** every collider is outlined, whether anything is playing or
not:

| Colour | Means |
|---|---|
| green | an ordinary solid collider |
| cyan | a trigger — reports overlaps, pushes nothing |
| grey | disabled, so taking part in nothing at all |
| red | touching something, **only while playing** |

Red is only claimed while playing because outside play mode nothing has been
tested — the clock is paused, no simulation step has run, and there is no honest
answer to "is this touching something". A green outline in the editor means
"here is a collider", not "this is clear".

The **Gizmos** menu in the Scene view's toolbar switches the whole **Colliders**
category off, along with the grid, the axes and the camera outlines.

The **Game view** never draws them, and neither does a built game. That is the
whole difference between the two panels: gizmos are a debug overlay, and none of
it ships.

> This used to be drawn from the end of `CollisionSystem::Update`, which only
> runs when the clock asks for a fixed step — and a paused clock asks for none.
> So while you were editing, no collider was ever drawn, and one you had just
> added looked like it had not worked. It is now
> `CollisionSystem::DrawGizmos`, and `ScenePanel::RenderView` calls it.

---

## What it found

The playground was built to test the physics, and the first thing it did was
fail. An audit of the whole 2D physics then turned up **36 confirmed defects**,
15 of them high-severity.

**All 36 are now fixed** - and an adversarial review of those fixes found 13
more defects *in the fixes*, which are fixed too; see **And then the fixes were
reviewed** below. Every one is pinned down by a case in
`tests/src/test_physics.cpp`. The numbers below are what the engine actually did
beforehand. The four headline ones first, then the rest in a table.

**A box resting on flat ground span up and fell through it.** With the default
settings — `freezeRotation` is false unless you set it — a 40-unit box dropped
on a floor picked up spin on the first step it touched down, reached 13.6 rad/s,
turned five and a half times and ended up 3471 units *below* the floor after
four seconds. The cause was `Collide`'s contact point: it was the average of the
two support vertices along the normal, and `Support` breaks a tie by keeping the
first maximum — so for a narrow box on a wide floor it took the **floor's far
corner** and the **box's near corner**. The point landed at x = 190 instead of
x = 0, which is a 190-unit lever arm on a body doing nothing but resting, and
inflated the contact's effective mass 136-fold so the stopping impulse was 136
times too weak as well.

Two equal-width boxes cancel exactly, which is why it had never been noticed —
and every resting test in the suite set `freezeRotation(true)`, which zeroes the
inertia and hid both symptoms completely.

**A six-box tower sank into itself and never recovered.** The position half of
the solver was a single sweep over stale separations, which cannot satisfy a
chain: clearing the overlap at the bottom moves that box up into the one above
it, and only another pass notices. Bottom box upward, at the shipped four solver
iterations:

| | box 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| before | 13.07 | 39.75 | 68.44 | 99.61 | 133.59 | 170.54 |
| after | 18.41 | 56.69 | 95.30 | 134.28 | 173.69 | 213.53 |
| ideal | 20 | 60 | 100 | 140 | 180 | 220 |

What is left is the *velocity* half not fully converging in four passes over six
contacts, which is exactly what `solverIterations` in `config/engine.json` is
for — and its note already says so. At 32 the top box reaches 218.

**A compound body was fired off whatever it landed on.** Three colliders on one
entity gave three contacts of equal depth, and the single sweep moved the body
the full 80% for each: 2.4× its own penetration, so it left the ground, fell
back and repeated forever.

**`CircleCast` could not see a box, a polygon or a tilemap.** It works by
growing the target's radius and firing a thin ray — and growing the radius is
what sent a shape into the rounded branch, which tested only the segment from
`points[0]` to `points[count - 1]`. For a box that is one of its four sides. A
character sweeping a circle at a wall could pass straight through it.

**A ray hit an edge collider past both its ends.** A 2-point radius-0 shape — an
edge collider at its default `edgeRadius` — was clipped as a polygon, and two
opposite half-planes intersect in the *infinite line* through the segment.

### And the rest

| What was wrong | What it looked like |
|---|---|
| Neither stepping loop sampled the end of its sweep | a move finishing just inside a wall reported clear |
| Inertia came from the rotated world AABB | a tumbling box got twice as hard to spin and back, once per revolution |
| Inertia ignored the collider's offset | a character whose collider sat at its feet span like one centred on it |
| One call to `MovePosition` never stopped | a lift told to rise 200 units rose 200 units *every step* until it left the level |
| `angularVelocity` was read but never written | saving a scene — or pressing Play then Stop — destroyed every body's spin while keeping its linear velocity |
| No restitution threshold | any bounciness above zero meant a body never came to rest, so "has it settled?" never became true |
| `Collide` returned the FIRST positive separation | the gap and normal `Physics2D::Distance` reported were whichever axis was tested first, not the real distance |
| A capsule's radius took `max(scale.x, scale.y)` | a character stretched to scale (1, 3) got a capsule **three times too wide**, snagging on every doorway |
| `Area()` was local-space on all six colliders | a 64×64 crate weighed what a 16×16 one did, while the same collider's *inertia* did follow the scale |
| Auto-mass used only the LAST collider's density | a compound body's mass changed fivefold with component order |
| Auto-mass and inertia counted DISABLED colliders | an attack hitbox live for three frames made its body permanently 7× harder to push |
| Velocity was applied as a LOCAL translation | a body under a parent scaled (2,2) fell twice as fast as its own reported velocity |
| `CircleCast`'s `point` was the circle's centre | reported a radius short of the thing it hit, while every other query reports the surface |
| `ClosestPoint` returned the query point from inside a circle | a circle answered the question differently from a box of the same footprint |
| `ClosestPoint` stepped INWARD on a rounded shape | a rounded box asked about its own centre answered 10 units inside its own surface |
| A compound body's events all carried the same contact | `hit.thisCollider` named the same collider in every handler, and the shallower contacts were unreachable |
| The old scene's exits reached the new scene | an `OnCollisionExit` on the first frame of a level, for a pair that never entered |
| A false "none of them will ever run" error | printed for a gate carrying a trigger sensor *and* a solid body, where the hooks were perfectly reachable |
| "Reload tiles" never reached the collider | editing a `.otile` from `"collision": "None"` to `"Grid"` left every cell walk-through |
| A polygon could be cut to two points | the collider stayed Enabled and collided with nothing, under a panel reading "NOT CONVEX" |
| A collider with no geometry made its entity unclickable | a fresh tilemap could be seen in the Hierarchy and never selected in the view |
| Collider defaults were Unity's metres | "+ BoxColliderComponent" gave a collider **one pixel across** in an engine whose units are pixels |
| Dropping an unused palette entry could resize the map | every cell and every collider box halved, with nothing painted or erased |
| `CompactPalette` claimed it ran on save | it never did; it is two manual buttons, and the comment now says so |
| `QueriesThisFrame` never reset | it counted every query since the process started |

### And then the fixes were reviewed

An adversarial review of the fixes themselves found **13 more defects, in the
fixes** - including one outright regression that 710 passing cases did not
notice, because the case written alongside that fix exercised the one shape the
fix happened to be right for.

`ClosestPoint` on a **2-point core** - every capsule, and every edge collider
with a thickness - answered on the *wrong side* for half the plane. Such a core
has two outward normals, because its two "edges" are the same segment walked
both ways; they tie on distance, the loop keeps the first, and consulting that
one normal put a capsule's nearest point 10 to the **right** when it was asked
about a point 50 to the left. A character standing on an edge collider was told
the nearest point was five pixels *under* the ground.

The lesson generalised: the replacement test was a **proxy** - a dot product
against one arbitrarily-kept edge normal - and a proxy fails again on any tie.
A query 9.999 above a corner ties two edges at `400.0f` in float32, so the
wrong one was kept and a point plainly outside the core was treated as inside.
It now asks the real question: for a convex core, is the point outside *any*
edge's half-plane. One dot product per edge, in the loop already walking them,
and no tie can move the answer.

The other twelve: deriving a capsule's core axis from the matrix's *extracted*
rotation broke under shear (a rotated child under a non-uniformly scaled parent
composes to a matrix that is not scale-then-rotate at all, and both cap centres
landed 32 px out in a case the previous code had exactly right); a box's
`Area()` still ignored `edgeRadius` while the contract beside it had just
promised otherwise; the post-load event clear went into one of **three**
scene-replacement paths, so pressing Stop still leaked the play session's exits;
routing every body through `SetWorldPosition` made a parent scaled flat on one
axis teleport the body and log a warning once per body per step; the
catch-up-velocity wipe destroyed a velocity a script had set that same step;
`CompactPalette` pinned the cell size even when it could not change; a
corner-arc normal compared a *squared* length against a non-squared epsilon; and
two comments no longer described their code.

All 13 are fixed, with seven more cases covering them.

### Two things worth knowing about the fixes

**The rounded-shape raycast is now exact.** `Physics2D::CircleCast` promises "No
stepping, no approximation", and it works by *growing the target's radius* — so
the ray code has to handle a radius on an n-gon. It used to collapse the core to
one edge; the first fix made it step against the real core; it now solves the
real boundary: each core edge pushed outward by the radius, plus a circle round
each vertex, nearest candidate wins. At most sixteen exact solves instead of a
hundred approximate ones, and a capsule falls out of the same arithmetic.

**The position solver iterates over live separations.** A correction is a pure
translation, so a contact's live separation is its original separation plus a dot
product — no shape is rebuilt and the narrow phase still runs once. That is what
made iterating cheap enough to do, and it is what fixes the stack and the
compound body together.

---

## The stations

Each heading below says what to look at, then what a **failure** looks like —
because "it moved" is not a result and the point of a playground is to know in
advance what wrong would look like.

### 01 · Shapes on flat ground

The five shapes a dynamic collider can be — box, box with `edgeRadius`, circle,
capsule, convex polygon — dropped onto one static box floor.

**Pass:** every outline comes to rest sitting exactly on the floor's top edge.
The rounded box is a rectangle with round corners, not a bigger rectangle.

None of the five has `freezeRotation` set, so this station is also the one that
caught the contact-point defect: before it was fixed, all five span up and fell
through the floor. See **What it found** above.

**Fail:** a shape sinks into the floor and stays there (position correction too
weak, or applied once per solver pass against a stale overlap); a shape rests a
visible gap above it (the position correction over-shoots, or a shape's
`WorldShapes` is bigger than it looks); a shape jitters forever (restitution
with no rest threshold); a shape starts turning although nothing pushed it
sideways (the contact point is off-centre on a flat face).

### 02 · Shapes on an edge chain

The same five on an `EdgeCollider` valley, plus one body fired **upward** into
the chain from below.

`Collider.h` says an edge collider is the one for ground because it has "no
underside and no ends to catch on". That sentence is about SEAMS AND ENDS, not
about being passable from beneath: every segment of the chain becomes a capsule
in `EdgeColliderComponent::WorldShapes`, and a capsule is solid from both sides.

**Pass:** the body fired from below is **stopped** under the chain and hangs
there - its gravity scale is 0, so nothing pulls it back down. The five sliding
bodies cross the joins between segments without catching.

If you want a one-way platform - up through, not down - that is a script that
switches the collider off while the body is rising, which is what
`PhysicsToggle` in station 12 is the simplest form of. It is not something an
edge collider does by itself.

**Fail:** a body catches on a seam where two segments meet and stops dead on
flat-looking ground - the exact bug a chain exists to avoid; or the body from
below passes straight through, which at this speed would mean the segment
capsules are one-sided.

Note the speed it is fired at. The chain is 8 units thick (`edgeRadius` 4) and
the ball is 28 across, so the two overlap over a window of 36 units; at 520
units a second the ball moves 8.7 units a step and cannot miss it. Raise that
launch speed past about 2100 and it tunnels through - which is station 13, not
this one.

### 03 · Shapes on a tilemap

The same five again, on a `TilemapCollider` — which merges solid cells into as
few rectangles as will cover them, so a flat floor is one box with no cracks in
it.

**Pass:** identical behaviour to station 01. The step at the right-hand end is
solid and square.

**Fail:** a body trips on an invisible crack (the merge left a seam); a body
falls through where two chunks meet (the merge missed a cell); the floor is in
the wrong place entirely (the cell-to-world transform ignores the entity's
position or scale).

### 04 · Bounciness 0 → 1

Five circles dropped from one line onto a floor with `bounciness` 0, at
bounciness 0, 0.25, 0.5, 0.75 and 1. The reference line they were dropped from
is drawn as an edge collider on a layer that collides with nothing.

**Pass:** each returns higher than the one before it. The 1.0 ball comes back
to **about** the line and never above it.

All five come to **rest** eventually, including the 1.0 one. That needs a
restitution threshold — below a couple of steps' worth of falling speed the
bounce is dropped — and without one a bouncy body buzzes against the floor
forever and its velocity never reaches zero, so a script waiting for it to
settle waits all game.

**Fail:** the 1.0 ball climbs past the line, gaining a little energy every
contact until it leaves the level — which is what an unclamped restitution, or a
restitution applied to an already-separating contact, does; the 0.0 ball bounces
at all; any of them never stops.

### 05 · Friction 0 → 1 on a ramp

Five boxes on a 22° static ramp, rotation frozen so this measures sliding and
not rolling.

**Pass:** friction 0 reaches the bottom, friction 1 barely moves, and the three
in between are ordered.

**Fail:** all five behave the same (the tangent impulse is not being applied, or
not clamped to the friction cone); the frictionless one slows down anyway (a
contact that bleeds speed with friction at zero makes every surface in a game
slightly sticky, and reads as sluggish controls).

### 06 · Mass, stacking and sinking

A six-box tower; a 1 kg and a 20 kg box dropped side by side; a 20 kg box
dropped onto a 1 kg one.

**Pass:** the tower stands with no visible overlap between boxes. The 1 kg and
20 kg boxes land **together** — gravity is an acceleration, so mass cancels.
The heavy box shoves the light one.

**Fail:** the tower's boxes sink into each other and settle lower than six box
heights (raise `solverIterations` in `config/engine.json` and see whether it
improves — if it does, the solver is working and the setting is just too low for
this stack); the heavy box lands first (something is dividing by mass that
should not be).

### 07 · Rotation, tipping and a see-saw

A tall box nudged at its base; a plank on a fulcrum; a kinematic paddle turning
through `MoveRotation` with balls dropped on it.

**Pass:** the tall box **tips** rather than sliding, because the contact at its
foot is off-centre and so has a lever arm. The plank tilts when the weight lands
on one end. The paddle sweeps the balls round rather than passing through them.

**Fail:** the tall box slides upright (the angular term is missing from the
contact solver); it tips the wrong way (a sign error in the cross product);
anything resting on the paddle is passed through (`MoveRotation` is teleporting
rather than driving velocity).

### 08 · Triggers and layers

A trigger volume a ball falls through; two boxes on layers that do not want each
other; two on layers that do.

**Pass:** the ball passes through the cyan volume **and** the reporter above it
counts a trigger enter and, on the way out, a trigger exit. `PassA` and `PassB`
drop through each other. `StopA` and `StopB` do not.

Interest has to be **mutual**: one collider naming the other's layer is not
enough. `PassA` and `PassB` name only themselves and `World`, which is why they
ignore each other while both still land on the floor.

**Fail:** the trigger pushes the ball (it is being solved, not just reported);
the enter count rises and the exit never arrives (the pair was dropped from the
list without an exit being sent — in a real game this is the script that thinks
it is still inside a region it left); `PassA` and `PassB` collide anyway
(one-sided interest is being treated as enough).

### 09 · Kinematic bodies

A lift carrying a crate; the same lift with `useMovePosition` unticked; a
kinematic capsule driven into a static wall; two kinematic boxes driven into
each other.

**Pass:** the proper lift carries its crate up and down. The teleporting one
passes straight through its crate, which hangs in the air and then falls — that
is the point of the comparison, and it is the most common mistake in a 2D
physics game. The capsule is **stopped** by the wall and slides along it. The
two kinematic boxes pass through each other.

`PhysicsMover` calls `MovePosition` every step, which is how a platform is meant
to be driven and is why it never showed that **one** call used to move a body at
that speed forever. If you drive a platform from your own script, drive it every
step.

That last pair is deliberate: both would have to give way, and which one moves
is a question for the scripts positioning them. Box2D leaves them alone and so
does this.

The capsule being stopped is this engine **departing from** Box2D and Unity on
purpose: `Rigidbody.h` recommends a kinematic body for a character controller,
and a character controller that walks through walls is not one. See
`SolverMasses` in `physics/Collider.cpp`.

### 10 · Gravity scale and drag

Five balls at `gravityScale` −0.4, 0, 0.5, 1 and 2; three boxes launched
identically with `linearDrag` 0, 1 and 4, leaving trails.

**Pass:** the −0.4 ball rises, the 0 ball hangs, the rest fall at increasing
rates. The three arcs are visibly different and the draggiest is shortest.

**Fail:** drag reverses the motion instead of damping it (a drag factor applied
as `v - v*drag*dt` goes negative once `drag*dt > 1`); `gravityScale` on one body
affects another.

### 11 · World queries

Seven probes, one per query — `Raycast`, `RaycastAll`, `CircleCast`, `BoxCast`,
`OverlapCircle`, `OverlapBox`, `OverlapPoint` — against a box, a circle, a
capsule, a polygon, an edge chain and a trigger.

**The casts sweep slowly, and that is the whole design.** A query fired once in
a fixed direction either hits or does not, and both answers look plausible. One
that turns shows the moment the answer stops making sense.

White is the query, green means no hit, a red dot is the hit point, a yellow
line is the surface normal, and magenta boxes are the bounds of whatever an
overlap query found.

**Pass:** the normal always points out of the surface that was hit, and turns
smoothly as the ray slides along a flat face. `RaycastAll` numbers its hits 0
upward, nearest first. `CircleCast` stops a radius short of what a thin ray
would have touched. A magenta box always encloses the green outline it belongs
to.

**Fail:** a normal that flips sign halfway along a flat face; a hit point that
jumps to the far side of a shape; `CircleCast` reaching as far as a thin ray
(the radius is being ignored); `CircleCast` missing the box, the polygon or the
tilemap altogether while `Raycast` finds them (which it did — see **What it
found**); a magenta box that does not line up with its green outline
(`WorldBounds` and `WorldShapes` disagree, which is what makes a broad phase
drop real contacts).

### 12 · Offset, scale, compound and switching

Where a collider is **not** where the sprite is.

* An **offset** collider — a box 42 units below its entity, which is how a
  character's feet become the part that collides.
* A box on an entity scaled `[2, 0.5]`, beside a **circle** on an entity scaled
  the same way. The box can be squashed and a circle cannot. The circle takes
  the **larger** of the two scales, on purpose: it comes out slightly too big
  rather than slightly too small, so contacts fire early, which is the safer
  direction for a game. A capsule does the same with its radius. Use a polygon
  collider when you genuinely want an ellipse - there is no ellipse test in
  `Overlap.h` and none is faked. So expect the circle outline to be visibly
  wider than its sprite is tall, and the box outline to match its sprite
  exactly.
* One entity rotated, scaled and offset at once — the case where a transform
  bug shows up as a shape in the wrong **place** rather than the wrong size.
* A **compound body**: two box colliders on one entity with one rigidbody. They
  must not collide with each other, and landing on the floor sends **two**
  enters naming the same floor, which the reporter's counts show.
* A ledge that **switches itself off** every few seconds, with a box resting on
  it.

**Pass:** the switching ledge's outline goes grey, the box falls through, and
the reporter's "now" count returns to **zero** — the exit arrived, rather than
the pair just going quiet.

**Fail:** the box is left hanging; the count stays at 1 after the ledge goes
grey; the scaled circle takes the SMALLER scale, or an average, instead of the
larger; the offset collider moves when its entity rotates but not the right way
round it; the compound body's two colliders push each other apart.

> The Inspector could not build that compound body until recently: its **Add
> component** list skipped any type the entity already had, so a compound body
> was something a scene file could contain and the editor could not make. It
> now asks the component itself - `Component::AllowsSeveral`, which a collider
> and a script answer yes to and everything else answers no to - so
> **+ CircleColliderComponent** stays on the list after the first collider has
> been added.

### 13 · Speed and tunnelling

Four lanes. Identical 24-unit bodies fired at 300, 1500 and 6000 units a second
into a 6-unit wall, and at 6000 into a 120-unit wall. Trails are on.

**This station documents a property, not a defect.** Collision here is
*discrete*: every fixed step moves a body and *then* looks for overlaps — see
`SystemOrder.h`. So a body travelling further in one step than a wall is thick
can be on one side at the end of one step and the far side at the end of the
next, with no step in between where the two overlapped. Nothing is reported,
because as far as the collision system is concerned they never met.

The arithmetic is worth knowing for any game, and it is the whole station. At a
sixtieth of a second a body doing 6000 units a second moves **100 units a
step**. Two shapes overlap over a window of `(bodyRadius + wallHalfWidth) * 2`,
and a step longer than that window can land clear on both sides of it:

| speed | units per step | wall | overlap window | result |
|---|---|---|---|---|
| 300 | 5 | 6 thick | 30 | stopped |
| 1500 | 25 | 6 thick | 30 | stopped, only just |
| 6000 | 100 | 6 thick | 30 | **through** |
| 6000 | 100 | 120 thick | 144 | stopped |

That is why the fourth wall is 120 units and not 60. At 60 the window is 84,
smaller than one step, and the thick lane would have tunnelled as well.

**Expected:** 300 and 1500 are stopped by the thin wall. 6000 goes through it,
and its trail passes visibly out the far side - and keeps going, several
stations wide, until the launcher fires it again two and a half seconds later.
6000 is stopped by the thick wall.

The fix in a real game is to sweep ahead of a fast thing with
`Physics2D::CircleCast`, which is exactly what it is for — or to make walls
thicker than anything's per-step travel. `tests/src/test_physics.cpp` pins this
behaviour down so nobody "fixes" it by accident.

---

## The scripts

All seven are in `assets/scripts/` and every one is a plain script you can drop
on anything in your own project.

| Script | What it does |
|---|---|
| `PhysicsLab` | the readout, the reset key, the launch key. One per scene. |
| `PhysicsLabel` | a title and a note pinned to a place in the world. |
| `PhysicsProbe` | one world query, fired every step and drawn where it went. `mode` picks which. |
| `PhysicsMover` | drives a kinematic body, through `MovePosition` or — to show the difference — by writing the transform. |
| `PhysicsReporter` | makes collision and trigger **events** visible: enter/exit counts, contact points, normals, closing velocity. |
| `PhysicsLauncher` | puts a body back where it started and fires it again, on a key or a timer, leaving a trail. |
| `PhysicsToggle` | switches a collider off and on, to check that `SetEnabled(false)` is the complete switch it claims to be. |

Every one draws with **gizmos**, so none of them show anything until you press
Play — a script does not run while the clock is paused, which is the whole of
the time you spend editing. The one thing that has to be visible while editing
is the collider outlines, and the Scene view draws those itself.

### A note on trails

`PhysicsLauncher` leaves a mark every fourth step for two seconds, not one every
step. A gizmo with a lifetime stays in the queue until its time is up and the
whole queue is redrawn every frame, and a `Gizmos::Circle` is twenty-four line
segments — so one dot per step at a three-second lifetime, across thirteen
trailing bodies, came to roughly **fifty-six thousand lines a frame** and took
the playground down to ten frames a second in a debug build. Thirty short dashes
per body costs nothing and reads better, because a dash shows speed as well as
position.

---

## Regenerating it

The scene was written by a generator, because 121 entities on a grid typed by
hand is a grid with two stations overlapping that nobody notices for a week. The
generator is not part of the build and is not kept in the repository: the scene
file is the artefact, and the editor owns it from here. Saving it in the editor
rewrites `name`, `camera` and `entities` and keeps every `_`-prefixed key, so
re-generating would overwrite your edits.

## Where to read next

| For | Read |
|---|---|
| Colliders, layers, triggers, events, the solver | `engine/include/engine/physics/Collider.h` |
| Bodies, body types, forces, `MovePosition` | `engine/include/engine/physics/Rigidbody.h` |
| Raycasts, overlaps, sweeps, `QueryFilter` | `engine/include/engine/physics/Physics2D.h` |
| What a script is told about a contact | `engine/include/engine/physics/Contact.h` |
| The shape maths every collider turns into | `engine/include/engine/math/Overlap.h` |
| Why movement runs before collision | `engine/include/engine/scene/SystemOrder.h` |
| The gizmo overlay and its categories | `engine/include/engine/render/Gizmos.h` |

[docs/scripting-reference.md](scripting-reference.md) is the script API.
[docs/editor-guide.md](editor-guide.md) covers the editor itself.
