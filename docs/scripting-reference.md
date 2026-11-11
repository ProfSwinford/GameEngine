# The scripting reference

Everything a script can call, arranged by what you are trying to do rather than
by which header it lives in.

> **Looking for one particular function?** [`docs/api/index.html`](api/index.html)
> is the browsable API reference - every public type reachable from
> `<engine/Engine.h>`, a page per class, with a search box. Open it in a
> browser; it needs no server. This page is the narrative; that one is the
> index.

A script is a C++ class that inherits `eng::ScriptBehaviour`, writes only the
lifecycle functions it actually wants, and ends with
`ENGINE_REGISTER_SCRIPT(YourClass)`. `assets/scripts/TestScript.cpp` is the
template; `engine/include/engine/scene/ScriptComponent.h` is the argument for
why it works that way.

Every script needs one include, and it is the only one most scripts ever need:

```cpp
#include <engine/Engine.h>
```

---

## Making your editor understand that include

Double-click a script and the include above is the first thing that fails. Not
because anything is wrong with it: the file opens with no project behind it, so
there are no include directories, so `engine/Engine.h` is not found - and since
that one header is the umbrella over every other, **every** `eng::` name in the
file then reads as unknown. No completion, no parameter hints, no
go-to-definition, and a red line under most of what you wrote.

The editor knows exactly what it compiles your scripts with, and can write it
down:

**File -> Set Up IDE IntelliSense**

That writes two files into your project folder. There are two because nothing
reads both:

| File | Read by |
|---|---|
| `CppProperties.json` | Visual Studio, and only in its Open Folder mode |
| `compile_commands.json` | clangd, and so VS Code, CLion, Qt Creator, Neovim, Emacs |

Then **File -> Open Project in Visual Studio**, which opens the *folder*. That
is the part that matters: Visual Studio only reads `CppProperties.json` for a
folder you opened as a folder, so opening a single script - which is what
double-clicking one does - is precisely the case that gives you nothing. With
the folder open, scripts you then open from the Assets panel land in that same
instance and work.

Without opening a window at all: `editor --write-ide-files`.

Two things worth knowing:

- **Neither file is worth committing.** The include paths in them are absolute,
  and a released editor and a source tree do not even use the same set of them,
  so a copy from somebody else is wrong rather than merely out of date.
  Regenerating is one menu item.
- **Your scripts are compiled as C++20**, one standard behind the engine itself.
  These files say so, which means your IDE will refuse a C++23 feature here
  before the compiler gets the chance to.

Working inside the engine's own source tree is the one case this does not
cover: the root holds a `CMakeLists.txt`, so Visual Studio treats it as a CMake
project and ignores `CppProperties.json`. It configures IntelliSense from the
CMake project model there, so what makes a script resolve is that some target
names it as a source - which is what `game_scripts` in the top-level
`CMakeLists.txt` is for. That target is also a typecheck nothing in the default
build performs:

```
cmake --build build/debug --target game_scripts
```

For clangd in a source tree it is the exported compile database that matters
instead, and only the Ninja presets produce one - the Visual Studio generator
writes none.

**If `ENGINE_REGISTER_SCRIPT` on its own looks like an error, the CMake cache
is stale.** It is worth knowing the shape of this one, because the symptom
points nowhere near the cause. A cache configured before `game_scripts`
existed has no target claiming `assets/scripts/*.cpp`, so Visual Studio falls
back to a default configuration for those files - and the default standard is
below C++20. `ScriptHooks.h` then stops parsing at its first `concept`, on
line 64, while the macro is defined at line 503. It is never reached, so the
one line in your script that uses it is the one line that goes red. Meanwhile
the heuristic include scan still resolves enough `eng::` names that nothing
else looks especially wrong.

In Visual Studio: **Project -> Configure Cache**, or close the folder and open
it again. Anywhere else: reconfigure the preset.

**If instead `std::` and every `#include` look like errors, the files were
written by an editor older than this paragraph.** Visual Studio inherits no
toolchain environment for a folder, so `CppProperties.json` has to name the
directories where the compiler keeps `<algorithm>` and `<windows.h>` as well
as the engine's. The first versions of it named only the engine, and a
`<algorithm>` that cannot be found is a *fatal* error - the file stops being
parsed there, so everything in it goes red at once rather than one line of it.
Run **File -> Set Up IDE IntelliSense** again and the include path will have
them.

---

## Coming from Unity

The vocabulary is different in two places and the same everywhere else:

| Unity | Here | Why |
|---|---|---|
| `GameObject` | `eng::Entity` | A name, an id and a bag of components - the same model, the older name for it. |
| `Transform` | `eng::Transform2D` | Two dimensions, so one rotation angle rather than a quaternion. |
| `MonoBehaviour` | `eng::ScriptBehaviour` | You inherit it and write only the hooks you want. |
| `GetComponent<T>()` | `Find<T>()` | House rule: a lookup that can come back empty is `Find`, never `Get`. |
| `Instantiate` | `Instantiate` | The same name, because it is the same thing. |
| `Debug.Log`, `Debug.DrawLine` | `eng::Debug` | The same class under the same name, minus the channel the engine's own macros require. See **Drawing and logging**. |
| `transform`, `gameObject`, `name`, `tag` | the same, spelled the same | Bare names on the base class, reached with `->`. See the next section. |
| `SpriteRenderer.sprite` | `SetTexture(path)` or `SetTexture(ImageRef)` | A property there, a method here. The type is named for the asset - `ImageRef` - and the method for the component's own field, which is `"texture"` in the scene file. There is no `SetImage`. |
| `public SpriteRenderer other;` | the same, spelled the same | A component reference, drawn as a slot you drag an entity onto. `Rigidbody2D` too. `Transform` is **`TransformRef`** here, because `Transform` cannot be a type name inside a script. See **A reference to a component on another entity**. |

Everything else keeps its Unity name where there is one. The full mapping is at
the bottom of this page, including the things that are **not here yet** and what
to do instead.

---

## The lifecycle

Write only the ones you want. No `virtual`, no `override`, no empty stubs - and
a script with a misspelled hook and nothing else is a compile error rather than
a script that silently does nothing.

| Hook | When |
|---|---|
| `void OnStart()` | Once, on the first simulation step after this script is attached and its entity is fully built. |
| `void OnFixedUpdate()` | Every fixed simulation step. Movement, physics, anything timed. |
| `void OnUpdate()` | Once per drawn frame. Fades, menus, anything tied to the screen. |
| `void OnDestroy()` | The entity is going away. Still safe to touch things here. |
| `void OnCollisionEnter/Stay/Exit(eng::EntityId other)` | A solid contact, where neither side is a trigger. |
| `void OnCollisionEnter/Stay/Exit(const eng::Collision2D& hit)` | The same, with `hit.normal`, `hit.point` and `hit.relativeVelocity`. |
| `void OnTriggerEnter/Stay/Exit(eng::EntityId other)` | An overlap where either side has "trigger" ticked. Nothing is pushed apart. |
| `void OnTriggerEnter/Stay(Collider2D other)` | The same, handed the other **collider** - Unity's form. `other->Owner()` is the entity. |
| `void OnTriggerEnter/Stay/Exit(const eng::Collision2D& hit)` | The same, with both colliders - `hit.thisCollider` is **yours**, so you can tell which of your triggers fired. |
| `void OnExpose(eng::ScriptFields&)` | Not an event, and usually not needed - see **Variables** below. |

Nothing takes a delta. Ask `eng::Time::FixedDeltaTime()` in `OnFixedUpdate` and
`eng::Time::DeltaTime()` in `OnUpdate`.

**A pair of COLLIDERS sends collision events or trigger events, never both.**
A script with `OnCollisionEnter` on a trigger collider is never called, and the
Console says so rather than leaving it silent.

**One event per collider pair**, which is Unity's rule. An entity carrying
three colliders that all touch the same wall gets three `OnCollisionEnter`
calls, each naming that one wall. And an entity carrying a trigger collider
*and* a solid one gets both families at once - the solid contact is resolved
and the trigger is reported, from the same pair of entities.

Two colliders on the **same** entity never report each other, so a compound
body may overlap itself freely.

---

## Where am I, and what am I attached to?

Five of these are **bare names**, written as though they were local variables -
which is how Unity's `MonoBehaviour` reads too:

```cpp
transform      // eng::Transform2D*   where it is, how it is turned, how big
gameObject     // eng::Entity*        the entity itself
scene          // eng::Scene*         the world
name           // const std::string&  its name, "" once it has gone
tag            // const std::string&  what KIND of thing it is
```

```cpp
float speed = 200.0f;   // a tunable, and a box in the Inspector

void OnFixedUpdate() {
    transform->Translate({0.0f, speed * eng::Time::FixedDeltaTime()});
    if (gameObject->HasTag("Player")) {
        ENGINE_LOG_INFO(eng::Channels::kGame, "{} is the player", name);
    }
}
```

The same things spelled as functions, which still work and are what most of the
engine's own code uses:

```cpp
Owner()        // eng::Entity*      this script's entity
OwnerId()      // eng::EntityId     its id, which is what survives a frame
Transform()    // eng::Transform2D* its position, rotation and scale
GetScene()     // eng::Scene*       the world
Name()         // const std::string&  the entity's name, "" once it has gone
Tag()          // const std::string&  "" when untagged
```

All of them work from `OnStart` onwards. All of them answer null - or `""` -
once the entity has been destroyed, which is why nothing below needs a liveness
check first. `OwnerId()` has no bare spelling on purpose: the id is the one
thing worth *holding on to*, so it reads better as something you asked for.

### They are reached with `->`, because they ask afresh

A bare name is not a cached pointer. It is a one-pointer handle that asks the
entity again on every use, so `transform` cannot be stale after something moves
or reparents the entity, and `name` follows a rename made anywhere:

```cpp
eng::Transform2D* raw = transform;   // converts, so it passes anywhere the pointer does
if (transform) { ... }               // false once the entity has gone
std::size_t len = name->size();      // `->` for the string ones too
bool boss = (tag == "Boss");         // and they compare against strings
tag = "Enemy";                       // the only one that can be assigned
```

`transform->X()` on a destroyed entity is a null dereference, exactly as
`Transform()->X()` already was - the handle does not make that safe and does
not pretend to. In practice a script stops being ticked before its entity goes,
so the checks that eight of the fourteen scripts in `assets/scripts` do write
are belt-and-braces rather than load-bearing.

**`name` cannot be assigned.** A name is an identity here - parents and entity
references are recorded by it, and the scene keeps a name-to-entity table - so
renaming goes through the scene, can be **refused**, and says why:

```cpp
if (!Rename("Boss")) { /* taken, or empty - the Console says which */ }
```

### Three things worth knowing before you meet them

**`std::transform` is hidden.** A member found by unqualified lookup also
suppresses argument-dependent lookup, so an unqualified
`transform(first, last, out, fn)` no longer reaches `std::transform`, and the
error - *"term does not evaluate to a function taking 4 arguments"* - names
neither. Write `std::transform`, which is unaffected and is the better spelling
anyway. No other bare name has a standard-library counterpart.

**Every engine type is already spellable without qualification.** `Transform2D`,
`Vec2`, `Entity`, `Scene`, `Color` and the rest, because `<engine/Engine.h>`
contains `using namespace eng;`. The `eng::` prefixes in `assets/scripts` are
house style, not a requirement.

The one exception is the `Transform` **alias** for `eng::Transform2D*`, which
the member function `Transform()` hides - so it needs `::Transform`, and
`eng::Transform` does not exist at all. With `transform` in place there is no
longer a reason to write either; use `Transform2D*` when you need to name the
type.

**These five names are taken, and so is your class's own.** A public variable
of your own called `transform`, `gameObject`, `scene`, `name` or `tag` would
hide the inherited one, so the editor leaves it out of the Inspector and says
why in the Console. Give yours a different name.

The same lookup rule catches the script class itself: inside `class Destroy`
the name `Destroy` means the class, so `assets/scripts/Destroy.cpp` cannot call
the inherited `Destroy(EntityId)`. `Find`, `FindAll`, `Tag`, `SetTag` and
`Rename` are the other members a class name can hide. Nothing stops you - it is
legal C++ - but the member then needs `ScriptBehaviour::` in front of it.

---

## Components on this entity

```cpp
Find<T>()              // the first T, or nullptr          (Unity: GetComponent<T>)
FindInChildren<T>()    // the first T here or anywhere below
FindInParents<T>()     // the first T here or anywhere above
Add<T>()               // attach another one               (Unity: AddComponent<T>)
Require<T>()           // the existing one, or attach one

FindAll<T>(into)           // every T on this entity       (Unity: GetComponents<T>)
FindAllInChildren<T>(into) // every T here or below
FindAllInParents<T>(into)  // every T here or above, NEAREST FIRST
```

```cpp
void OnFixedUpdate() {
    if (auto* body = Find<eng::Rigidbody2DComponent>()) {
        body->SetVelocity(eng::Vec2{0.0f, 200.0f});
    }
}
```

The entity itself has more, for when you are holding one that is not yours:

```cpp
entity->Find<T>()                  entity->FindAll<T>(into)
entity->FindInChildren<T>()        entity->FindAllInChildren<T>(into)
entity->FindInParents<T>()         entity->FindAllInParents<T>(into)
entity->Add<T>()
```

Every `FindAll*` takes the vector to fill, **clears it first**, and returns how
many it wrote - so a per-frame walk allocates once, and a lookup that found
nothing hands back an empty vector rather than the previous answer. The two
hierarchy ones take a second argument to leave this entity out
(`FindAllInParents<T>(into, false)`); on the script's own versions it is always
included, as Unity's are.

**They do not find scripts.** Every script's component is called
`"ScriptComponent"`, so `Find<Chaser>()` cannot work. Use `FindScriptOn<T>` or
`FindScriptIn<T>`, below.

---

## Another entity

```cpp
FindEntity(id)               // an EntityId -> eng::Entity*, or nullptr
FindEntity("Player")        // by name
FindEntityWithTag("Enemy")  // by tag                       (Unity: FindWithTag)
FindFirstWith<T>()          // the first entity carrying a T (Unity: FindObjectOfType)
FindFirstComponent<T>()     // that T itself
```

`FindEntity(id)` is the one you will write most. Every collision and trigger
hook hands you an `EntityId`, and so does `Instantiate`:

```cpp
void OnTriggerEnter(eng::EntityId other) {
    eng::Entity* partner = FindEntity(other);
    if (partner == nullptr) { return; }        // it may already be gone
    if (!partner->HasTag("Player")) { return; }
    DestroySelf();
}
```

**Hold the id, never the pointer.** An `EntityId` carries a generation, so a
stale one resolves to nothing instead of to whatever moved into its slot. A
pointer is good until the next time entities are destroyed, which is once per
step. Ask again rather than remember.

Across a whole scene, from the scene itself:

```cpp
scene->FindEntity("Player")                 scene->FindByTag("Enemy")
scene->CountWithTag("Enemy")                scene->ForEachWithTag("Enemy", fn)
scene->FindFirstWith<T>()                   scene->ForEachWith<T>(fn)
scene->CountWith<T>()                       scene->ForEachUntil(fn)
```

All of them skip anything already queued for destruction, because "how many
enemies are left" answered with the one that died this tick is wrong. Pass
`false` as the last argument if you want the editor's answer instead.

---

## Tags

A tag is what **kind** of thing something is: `"Player"`, `"Enemy"`,
`"Pickup"`. Set it in the Inspector, next to the name.

```cpp
entity->Tag()              entity->SetTag("Enemy")        entity->HasTag("Enemy")

tag                        tag = "Enemy"                  HasTag("Enemy")
Tag()                      SetTag("Enemy")                // this script's own entity
```

`tag` is the one bare name that can be **assigned**, because a tag is a label
rather than an identity - setting one costs nothing and cannot collide with
anything. Compare it against a string directly: `if (tag == "Enemy")`.

It is not any of the three identities the engine already had:

- a **name** is unique, and is how parents and entity references are recorded -
  so you cannot label fifty bullets with it;
- a **collision layer** decides what collides with what, so labelling something
  with it changes its physics, and it does not exist on things without a
  collider;
- a **component type** says what something can do, not what it is - every
  bullet and every pickup has a sprite.

Empty means untagged, and `HasTag("")` is therefore **false** rather than true
for everything.

---

## Making things

```cpp
Instantiate(prefabPath)                      // Unity: Instantiate(prefab)
Instantiate(prefabPath, placement)
Instantiate(prefabRef)                       // the one to actually use
Instantiate(prefabRef, placement)
CreateEntity("Spark")                        // Unity: new GameObject("Spark")
CreateEntity("Spark", placement)
```

`placement` is an `eng::SpawnOptions`, built with whichever parts you care
about:

```cpp
Instantiate(bulletPrefab, eng::SpawnOptions{}
    .At(Transform()->WorldPosition() + Transform()->Right() * 24.0f)
    .Facing(Transform()->WorldRotation()));
```

| Builder | Means |
|---|---|
| `.At(worldPosition)` | Put it there, in world space. |
| `.Facing(radians)` | Turn it to face that way. |
| `.Scaled(factor)` | Set its scale. |
| `.Under(entityId)` | Parent it, keeping the world position above. |
| `.Named("Bullet")` | A name instead of the asset's. It is made unique either way. |

**A default `SpawnOptions{}` touches nothing**, so the prefab lands exactly
where the asset says. Every field is opt-in for that reason.

**Spawning is safe at any time**, including from inside an update or a collision
hook. Creating only ever appends, so nothing a system is walking gets moved out
from under it.

**Spawned names are made unique.** Fifty `Instantiate` calls give fifty
distinct names, which matters because a name is an identity here. If you create
an entity yourself, use `scene->CreateEntityUniquelyNamed` - plain
`scene->CreateEntity` takes the name exactly as given, because the scene loader
must, and it reports a collision at Error level rather than overwriting
silently.

### Choosing the prefab in the Inspector

Do not type the path into the code. Declare a `PrefabRef` and drag the prefab
onto it:

```cpp
eng::PrefabRef bullet;    // drag prefabs/bullet.oprefab onto this
```

It appears as a slot in the Inspector, says so in red when the file is missing,
and the scene file stores the same string a `std::string` would. A spawner with
an unset reference does nothing and reports it once, not sixty times a second.

---

## Unmaking them

```cpp
DestroySelf()                   // Unity: Destroy(gameObject)
Destroy(id)                     Destroy(entity)
DestroySelfAfter(2.0f)          // Unity: Destroy(gameObject, 2.0f)
DestroyAfter(id, 2.0f)
```

**Destroying is queued**, not immediate: it happens at one point in the frame,
once every system has finished. So:

- an entity destroyed this frame **still draws and still updates** for the rest
  of it, but **does not collide**;
- destroying something twice is harmless, so there is nothing to check first -
  two bullets hitting one enemy on one tick is ordinary code;
- a script that destroys its own entity finishes the function it is in.

A **delay** is on the game clock, so a fuse stops burning while the game is
paused. **While it burns, nothing has changed**: the thing still collides,
draws and updates completely normally, and only joins the destroy queue when
the delay runs out. Asking again replaces the fuse rather than adding a second
one, so "reset the timer" is just asking again.

```cpp
eng::DeferredOps::CancelQueuedDestroy(id);   // call one off
eng::DeferredOps::SecondsUntilDestroy(id);   // negative when nothing is waiting
```

---

## Switching things off

```cpp
entity->SetActive(false)            // Unity: gameObject.SetActive(false)
entity->IsActiveSelf()              // Unity: activeSelf
entity->IsActiveInHierarchy()       // Unity: activeInHierarchy
SetActive(false)  IsActive()        // this script's own entity
```

A switched-off entity keeps everything it has - components, values, its place in
the hierarchy - and it saves and loads. But nothing acts on it: it is not drawn,
it does not collide, it is invisible to every physics query, its scripts do not
run and receive no contact events, its interface is neither drawn nor clickable,
and a camera on it cannot be the active camera.

**Switching off a parent switches off everything under it**, without touching
their own flags - so switching the parent back on restores exactly what was
there.

A script whose entity is switched off **stops being ticked from the next step**,
which also means it cannot switch itself back on. And a script attached to
something already switched off does not get its `OnStart` until the thing is
switched on.

There are no `OnEnable` / `OnDisable` hooks yet. Writing one is a compile error
that says so, rather than a function nobody calls.

---

## The transform

```cpp
LocalPosition()  LocalRotation()  LocalScale()      // relative to the parent
WorldPosition()  WorldRotation()  WorldScale()      // in the world
SetLocalPosition(v)  SetLocalRotation(r)  SetLocalScale(v)
SetWorldPosition(v)  SetWorldRotation(r)  SetWorldScale(v)
Translate(delta)     Rotate(radians)
```

Which way it faces, and turning to face things:

```cpp
Right()                      // the direction its own +X points - "forwards"
Up()                         // the perpendicular of that
LookAt(worldTarget)          // turn so Right() points at a world point
RotateAround(pivot, radians) // orbit a world point, moving and turning
```

`LookAt` a point exactly on top of the object leaves the rotation alone, rather
than snapping to face right the instant two things touch.

The tree:

```cpp
Parent()  Children()  ChildCount()  ChildAt(i)  Root()
SetParent(parent, keepWorldTransform)  DetachChildren()
Depth()  IsDescendantOf(other)
```

By **entity** rather than by transform, which is what you need to reach the
components beside a child:

```cpp
entity->ChildCount()   entity->ChildAt(i)        entity->ParentEntity()
entity->RootEntity()   entity->FindChild("Muzzle")
entity->IsDescendantOf(other)
scene->EntityOf(someTransform)                   // a transform -> its entity
```

`FindChild` searches **direct children only**. `FindInChildren<T>` is the
recursive one, and it searches by type.

Converting between frames of reference:

```cpp
LocalToWorldPoint(p)   WorldToLocalPoint(p)      // affected by position
LocalToWorldVector(v)  WorldToLocalVector(v)     // not affected by position
LocalMatrix()  WorldMatrix()
```

**World scale has one caveat.** When a non-uniform scale sits above a rotation
in the chain, the combined transform contains shear and no `(x, y)` pair
describes it - Unity calls its version `lossyScale` for the same reason.
`SetWorldScale` is exact in every other case, which is everything except
deliberately squashing a rotated rig.

---

## Maths

`eng::Vec2` has the operators, plus `Length`, `LengthSquared`, `Normalized`,
`Perpendicular`, and the named directions `Zero`, `One`, `Up`, `Down`, `Left`,
`Right`, `UnitX`, `UnitY`. **`+Y` is up.**

Free functions in `eng`:

```cpp
Dot(a, b)   Cross(a, b)   Distance(a, b)   DistanceSquared(a, b)
Lerp(a, b, t)             Scale(a, b)      ApproxEqual(a, b)
Rotated(v, radians)       Reflect(direction, unitNormal)
ClampMagnitude(v, max)    MoveTowards(current, target, maxDelta)
FromAngle(radians, len)   AngleOf(v)       SignedAngle(from, to)
Clamp01(x)                InverseLerp(from, to, value)
WrapAngle(r)              DeltaAngle(from, to)   MoveTowardsAngle(f, t, max)
kPi  kTwoPi  kDegToRad  kRadToDeg
```

Three of those are worth a sentence each:

- **`MoveTowards` arrives, exactly.** A lerp never does, so `== target` is never
  true and whatever was waiting on it never fires - and the distance it covers
  depends on the frame rate.
- **`ClampMagnitude` leaves a short vector alone.** This is how a speed limit is
  spelled; the naive version snaps a slow drift up to the limit.
- **`DeltaAngle` knows angles wrap.** Turning from 350° to 10° is +20°, not
  -340°, and a turret written with the plain subtraction spins the long way
  round once per revolution.

There is deliberately no `Lerp(float, float, float)`, `Clamp`, `Min`, `Max` or
`Abs` - `std::lerp`, `std::clamp`, `std::min`, `std::max` and `std::fabs` are
correct and already available.

`eng::GlobalRandom()` is a shared `eng::Random`: `NextInt(lo, hiInclusive)`,
`NextFloat01()`, `NextRange(lo, hi)`, `NextBool()`, `NextDirection()`. Hold your
own `eng::Random{seed}` when you want a repeatable sequence - which is the
reason this is a type you can instantiate rather than a set of statics.

---

## Physics

Ask the world what is there:

```cpp
eng::Physics2D::Raycast(origin, direction, distance, filter)
eng::Physics2D::RaycastAll(origin, direction, distance, out, maxOut, filter)
eng::Physics2D::CircleCast(origin, radius, direction, distance, filter)
eng::Physics2D::BoxCast(origin, size, angle, direction, distance, filter)
eng::Physics2D::OverlapPoint(point, filter)
eng::Physics2D::OverlapCircle(centre, radius, out, maxOut, filter)
eng::Physics2D::OverlapBox(centre, size, angle, out, maxOut, filter)
eng::Physics2D::Distance(a, b)
eng::Physics2D::Gravity()  /  SetGravity(v)
```

`filter` is an `eng::QueryFilter`: a layer, whether triggers count, and an
entity to ignore. The defaults are the forgiving ones, because a query that
silently finds nothing is harder to debug than one that finds too much.

`Raycast` returns **the nearest hit**, with no limit on how many colliders the
ray crosses. `RaycastAll` returns as many as fit in your buffer, nearest first -
which is not the same as the nearest `maxOut` of them, so use `Raycast` when you
want the nearest.

Move things:

```cpp
auto* body = Find<eng::Rigidbody2DComponent>();
body->SetVelocity(v)        body->Velocity()
body->AddForce(f)           body->AddTorque(t)
body->AddForce(f, eng::ForceMode::Impulse)      // a kick rather than a push
body->SetAngularVelocity(r) body->SetBodyType(eng::RigidbodyType::Dynamic)
body->SetGravityScale(s)    body->SetLinearDrag(d)    body->SetFreezeRotation(b)
```

A collider has `Layer()`, `SetLayer()`, `IsTrigger()`, `Enabled()`,
`SetEnabled()`. `eng::FindCollider(entity)` finds whichever shape of collider an
entity has, which `Find<BoxColliderComponent>()` cannot.

---

## Input

Input goes through **named actions**, bound in `config/engine.json`, so the
player can rebind them:

```cpp
eng::InputMap::IsPressed("Jump")    // only on the FRAME it went down
eng::InputMap::IsHeld("Jump")       eng::InputMap::IsReleased("Jump")
eng::InputMap::IsDown("Jump")       // pressed OR held
eng::InputMap::GetAxis("MoveX")
eng::InputMap::GetAxis2D("MoveLeft", "MoveRight", "MoveDown", "MoveUp")
eng::InputMap::PushContext("Menu")  eng::InputMap::PopContext()
```

A context masks the bindings beneath it, which is how a menu stops gameplay
input without every script checking a flag.

### Which hook to read it in

Input updates **once a frame**, before any fixed step. So every query answers
the same thing to every `OnFixedUpdate` of that frame, and a *held* key or an
axis reads the same from either hook.

A **press** is different. `IsPressed` and `IsReleased` are true for one frame
only, and a frame runs between **zero and five** fixed steps — zero whenever
the frame rate is above 60, five when it collapses. Read from `OnFixedUpdate`,
a press is therefore missed on a fast frame and handled several times on a slow
one.

So latch an edge in `OnUpdate`, which runs once a frame, and consume it in
`OnFixedUpdate`:

```cpp
void OnUpdate() {
    m_fireQueued = m_fireQueued || eng::InputMap::IsPressed("Fire");
}

void OnFixedUpdate() {
    if (m_fireQueued) {
        m_fireQueued = false;   // the first step to see it is the only one
        Fire();
    }
}
```

`||` rather than `=`, because `OnUpdate` also runs on frames that no step
follows — assigning would throw away a press nothing had acted on yet. And
cleared by the first step that acts, which is what makes one press one shot.
`assets/scripts/TilemapWalker.cpp` is the worked example.

**`OnUpdate` runs after the step loop, not before it**, so a value stored there
is read by the *next* frame's steps — one frame later. That is what the latch
costs. It is worth paying for an edge; for an axis it is a frame of latency for
nothing, so leave those where they are unless you want every input read in one
place.

The mouse, which cannot be rebound and so is not an action:

```cpp
eng::Pointer::InWorld()      // world units - the one gameplay wants
eng::Pointer::InView()       // view pixels, from the top-left
eng::Pointer::IsOverView()   // false when the mouse is elsewhere entirely
eng::Pointer::IsDown()  WentDown()  WentUp()  ViewSize()
```

**Use these rather than `EventPump::MouseX`.** The pump gives window pixels,
which are right in a shipped game and wrong in the editor by the whole width of
the panel border - so a script written against it picks the wrong tile in the
Game view only.

There is no `GetKey(KeyCode)`, deliberately: it would be a way around
rebinding, and the first game that ships with hardcoded WASD is what the action
model exists to prevent.

---

## Time

```cpp
eng::Time::DeltaTime()          // the right one for whichever update you are in
eng::Time::FixedDeltaTime()     eng::Time::UnscaledDeltaTime()
eng::Time::ElapsedTime()        // seconds since the game started, scaled
eng::Time::UnscaledElapsedTime()
eng::Time::FrameCount()         eng::Time::TickCount()
eng::Time::TimeScale()          eng::Time::IsPaused()
```

`eng::Time` is read-only; `eng::Engine::Get().Clock()` is where the two are
SET - `SetTimeScale(float)` and `SetPaused(bool)`.

---

## Changing level

```cpp
eng::Engine::Get().RequestScene("scenes/you_win.oscene");
eng::Engine::Get().RequestSceneByIndex(2);     // from the build list
```

**Never call `LoadScene` from a script.** Loading destroys every entity, which
destroys the behaviour object whose update is running - the call returns into a
`this` that no longer exists, and it will often appear to work. `RequestScene`
writes the request down and carries it out at the end of the frame.

Asking twice in one frame is not an error; the last request wins.

---

## Talking to other scripts

Three ways, in increasing order of coupling:

**An entity reference.** A `public eng::EntityRef` variable appears in the
Inspector as something you drag an entity onto, survives a save and a script
reload, and can be pointed somewhere else without touching the code:

```cpp
eng::EntityRef scoreLabel{"HudScore"};   // [UITextComponent] where the score goes
```

```cpp
if (auto* label = scoreLabel.Find<eng::UITextComponent>()) {
    label->text = "Score: " + std::to_string(score);
}
```

**A message.** No headers at all, and the payload is deliberately small - two
floats, an int and another entity covers how much damage, what was hit, which
trigger:

```cpp
eng::Message hit;
hit.type   = "Damage";
hit.target = other;
hit.sender = OwnerId();
hit.f0     = 25.0f;
eng::MessageBus::Send(hit);
```

Messages are delivered at one point in the step, after collision and before the
destroy queue.

**The script object itself**, when you need to call its own methods:

```cpp
FindScriptOn<Chaser>(entity)        // a script on a particular entity
FindScriptIn<Chaser>(*GetScene())   // the one in the scene
FindScriptNamedIn(*GetScene(), "ScoreKeeper")   // by name, no headers needed
```

The typed forms need the class to be **nameable** from your file. Scripts share
the global namespace - they used to each sit in an anonymous `namespace { }`,
and no longer do - so nothing hides the name any more; what a script still
lacks is a **declaration** of the other class. C++ needs one in scope, and a
full definition to call anything on it, and one `.cpp` cannot see into another.

So to point at another script's members, put that class in a header both files
include, with the methods you call **defined in the header**. A declaration
there and the body in the `.cpp` will not link: nothing exports a script's
symbols.

**Two scripts cannot share a class name.** The anonymous namespace used to make
a collision merely confusing; now it is a one-definition-rule violation - only
one of them registers and the other silently never runs, and an export links
every script into one library, where the duplicate is folded away without a
word. The editor refuses the name when you create a script or rename one, and
warns on every build if two files already define it.

---

## Variables in the Inspector

A **public** variable of a supported type is on screen, editable while the game
runs, and saved on each entity separately. No code at all:

```cpp
float speed = 200.0f;          // how fast it goes
float blend = 0.5f;            // [0..1] how much to mix
eng::EntityRef target;         // [Rigidbody2DComponent] what to push
eng::PrefabRef bullet;         // what to fire
```

The trailing comment becomes the tooltip. A marker at its **start** says
something extra about the variable:

| Marker | On | What it does |
| ------ | -- | ------------ |
| `[0..10]` | a number | a slider with those ends |
| `[TypeName]` | an `EntityRef` | narrows the picker to entities carrying that component or script |
| `[image]` | a `std::string` | an image's path — drawn as a **slot**, not a box. The older spelling of `eng::ImageRef` |
| `[animation]` | a `std::string` | names an animation stored in a sheet. The older spelling of `eng::AnimationRef` |

An `[image]` or `[animation]` string gets the same slot a Sprite's texture
does: the file's **name**, a drop target, and a picker. That is the difference
between a reference you can drag onto and a path you have to type correctly
from memory.

```cpp
std::string portrait;                  // [image] shown on the HUD
std::string walkAnimations[4];         // [animation] right, left, up, down
```

`[texture]` and `[sprite]` also mean an image, `[anim]` and `[clip]` an
animation, and **case does not matter** — `[Animation]` is the same marker.
Which spellings count is decided in one place, `eng::AssetFieldKindFor`, and
the Inspector maps that answer onto its widget rather than keeping a list of
its own.

A marker naming a kind the Inspector has no slot for is reported and the
variable is drawn as ordinary text — so a tooltip written where the kind goes
costs a message rather than a variable.

**A marker may also sit on the comment line directly above the declaration.**
The trailing comment used to be the only place one fit, which is past the
tooltip and out of sight of the comment block that describes the variable —
and the variables most in need of a marker are the ones with the most to
explain:

```cpp
// [animation] which clip to play walking left
std::string walkLeft;
```

One comment line, immediately above, starting with the bracket: a blank line or
a banner line in between breaks it. A marker on the declaration's own line wins.

Supported: `float`, `int`, `bool`, `std::string`, `eng::Vec2`, `eng::Color`,
`eng::EntityRef`, `eng::PrefabRef`, `eng::AnimationRef`, `eng::ImageRef` —
**or a fixed-size array of any of them**. Anything else is left out, and the
Console says which and why.

### An animation is a type, not a marked string

```cpp
eng::AnimationRef walkLeft;                    // one slot
eng::AnimationRef walkAnimations[4];           // four, one per direction
```

Drag a clip from the Assets panel into the slot — expand a sheet's row to find
them — or click it to pick from a list. It paints **red** when no sheet holds
the animation it names, which is what happens when somebody renames a clip in
the slicer.

**Play takes one directly**, and that is the reason the type exists:

```cpp
void OnFixedUpdate() {
    animation->Play(walkLeft);
}
```

An `AnimationRef` holds the clip's whole sub-asset path —
`"textures/hero.png#walk_left"` — and `Play` works out what to do with it: a
clip in the sheet already playing is the cheap call, another sheet is the
expensive one. A bare name like `"walk_left"` still works, so nothing that
used a text box breaks. An **unset** reference does nothing and says nothing,
because a blank slot is a direction a character does not have.

It also answers the two questions a script used to ask by hand:

| | |
| --- | --- |
| `Path()` | the whole thing, as stored — `"textures/hero.png#walk_left"` |
| `Sheet()` | `"textures/hero.png"`, or **empty** when it holds a bare name |
| `Clip()` | `"walk_left"` — **always**, whichever shape it holds |
| `IsSet()` | whether anything has been chosen. Not whether it exists |

`Clip()` differs from `eng::SubAssetNameOf` on purpose: that returns empty for
a string with no `#`, and a bare name *is* a clip name.

**Changing a `std::string` field to this type costs nothing.** Field values are
stored as untyped JSON keyed by name, so a scene that already saved a path
loads straight into the new type — no migration, no version bump. `PrefabRef`
made the same promise for the same move.

`// [animation]` on a `std::string` still works and is the **older spelling**.
It is kept because scripts using it are correct today; prefer the type in new
code — it needs no comment to be guessed at, and it is the one that paints red.

**To hide one, make it `private`.** The name is not consulted: `m_speed` in a
public section is shown like anything else. It used to be hidden — an `m_`
prefix was read as this engine's `[HideInInspector]` — and that was a trap,
because half the C++ world names ordinary members that way and got no box, no
report and no way to find out why. `private:` is the language's own way to say
"not part of the interface" and cannot be arrived at by accident.

Every field that can take a drop does: drag an asset from the Assets panel onto
a string, an `ImageRef`, an `AnimationRef`, an `[image]`/`[animation]` slot or a
`PrefabRef`, and an entity from the Hierarchy onto an `EntityRef`. A plain
string accepts **any** kind of asset, because nothing can know which kind it
wanted.

### A picture is a type too

```cpp
eng::ImageRef portrait;                 // one slot
eng::ImageRef faces[4];                 // four, one per direction
```

The same slot a Sprite's texture gets: drag an image from the Assets panel, or
click to pick from a list that includes every frame of every sliced sheet. It
paints **red** when the reference does not resolve, and says *which* of the two
things is wrong underneath — "the file is not there" and "the sheet has no part
called that" have different fixes.

**`SetTexture` takes one directly:**

```cpp
sprite->SetTexture(portrait);
```

That overload is a one-line forwarder and **absorbs nothing** — unlike
`Play(const AnimationRef&)`, which exists because a clip's *name* and its
*path* need different calls. `SetTexture` always took either shape. The
overload is there so a script can pass the type where it used to pass the
string, instead of writing `.Path()` at every call.

| | |
| --- | --- |
| `Path()` | the whole thing, as stored — `"textures/items.png#key"` |
| `File()` | `"textures/items.png"` — **never empty** once set |
| `Frame()` | `"key"`, or **empty** when it names a whole picture |
| `IsSet()` | whether anything has been chosen. Not whether it exists |
| `IsWholeImage()` | `Frame().empty()`, without building a string to ask |

**`File()` and `Frame()` read the opposite way round from an `AnimationRef`'s
`Sheet()` and `Clip()`,** and that is why these are two types rather than one
carrying a kind. A bare string is *complete* for an image — a path with no `#`
**is** the picture — and *incomplete* for an animation, where a bare name is a
clip in a sheet it does not name:

| holding `"hero.png"` | holding `"walk_left"` |
| --- | --- |
| `ImageRef::File()` → `"hero.png"` | `AnimationRef::Sheet()` → **empty** |
| `ImageRef::Frame()` → **empty** | `AnimationRef::Clip()` → `"walk_left"` |

On a full sub-asset path the two agree exactly, which is what lets one string
sit in either slot: a sheet's frames and its animations share one namespace, so
`"hero.png#walk"` is legitimately both a clip and a picture — an animation
draws as the first of its frames that still exists.

**Changing a `std::string` field to this type costs nothing**, for the same
reason it costs nothing for an animation: field values are untyped JSON keyed
by name.

`// [image]` on a `std::string` still works and is the **older spelling**. It
is kept because scripts using it are correct; prefer the type in new code.
Nothing in this project ever used the marker, which is the problem it had — a
mechanism with no example anywhere, findable only by reading the engine's own
headers.

### A reference to a component on another entity

Unity's object fields. Declare one, drag an entity from the Hierarchy onto the
slot, and the script reaches the component on it:

```cpp
class Follower final : public MyScriptBehaviour {
public:
    TransformRef   anchor;   // drag the thing to follow here
    SpriteRenderer flash;    // and the sprite to tint here

    void OnFixedUpdate() {
        if (anchor) {
            transform->SetWorldPosition(anchor->WorldPosition());
        }
    }
};
```

`TransformRef`, `SpriteRenderer` and `Rigidbody2D` are the short spellings.
For anything else, name the component:

```cpp
eng::ComponentRef<eng::TilemapComponent> ground;
eng::ComponentRef<eng::BoxColliderComponent> area;
```

**The slot only accepts an entity that has that component.** The picker lists
only those, and the constraint comes from the type itself - there is no
`// [SpriteComponent]` marker to write and none to get wrong.

**`if (anchor)` is the question to ask**, and it is true only when the
reference resolves *right now*. Empty, deleted, renamed away, or carrying no
such component all read the same, because a caller does the same thing about
all four. `IsSet()` asks the other question: whether anything was ever dropped
on it.

**`TransformRef` hands back the `Transform2D`**, not the component, so
`anchor->WorldPosition()` is spelled exactly like the `transform->` you
already write. Every other reference hands back the component itself.
`GetComponent()` gets the component in either case.

**It is not a pointer, and it cannot be.** A reference stores the target
entity's *name* and resolves on every access. Two reasons, and the first is
the one that decides it:

- *A pointer could never be filled in.* The Inspector matters while you are
  editing, and in edit mode the clock is paused - no script ticks and
  `OnStart` never runs - so a `SpriteComponent*` field would read empty for
  the whole session however many entities you dropped on it. A pointer also
  has no form in a scene file, so there would be nowhere to save the drop.
- *A pointer could never survive.* The behaviour object is destroyed and
  rebuilt on Stop, on every undo step, on a scene change, on a script rebuild
  and on a prefab re-save. Storing an `EntityId` instead is worse than
  useless: unloading a scene restarts generations at `1`, so a stale id can be
  *valid* and point at a different entity.

The cost is a lookup by name per access - the same cost the rule below already
asks for, and for a reference across entities it is not optional.

**For a component on the *same* entity, this is the wrong tool.** Ask with
`Find<T>()` where you use it, or cache the raw pointer in a `private:` member,
which is safe there precisely because the cache and the script object die
together:

```cpp
private:
    eng::Rigidbody2DComponent* m_body = nullptr;   // filled in OnStart
```

**Two spellings that do not exist, and why:**

| You might write | What happens | Instead |
|---|---|---|
| `Transform anchor;` | Does not compile, with an error that points nowhere near the cause. `ScriptBehaviour` declares `Transform2D* Transform()`, and inside a script class that inherited member function hides a namespace-scope type of the same name. | `TransformRef anchor;` |
| `Collider2D area;` | Not a reference - it is still a plain pointer. `ColliderComponent` is the abstract base of the four collider types and has no `kTypeName`, and `Find<T>` looks a component up by exactly that name. | `eng::ComponentRef<eng::BoxColliderComponent>`, or `eng::FindCollider(entity)` for any collider |

**A reference to another *script* is not expressible.** A script class lives in
an anonymous namespace in its own file, so no other script can name the type.
Use an `eng::EntityRef` with a `// [ScoreKeeper]` marker to get the slot, and
`FindScriptNamedIn` or a message to do the talking — see **Talking to other
scripts**.

### An array is one row per element

```cpp
std::string walkAnimations[4];   // right, left, up, down
```

shows up as four boxes — `walkAnimations[0]` to `walkAnimations[3]` — each an
ordinary variable of the element's type, each saved separately, each with that
same tooltip. Index it in the script as you would any array.

**A marker applies to every element.** Written `// [animation] right, left, up,
down`, that same declaration gives all four the animation slot rather than four
boxes to type a path into — which is the whole of the difference between the
two string arrays in `assets/scripts/TilemapWalker.cpp`.

The **length is never read** from the declaration: the generated exposer loops
over `std::size()`, so `[4]`, `[kFacingCount]` or anything else the compiler
can count all work. Give the indices meaning with an enum and the indexing
reads as what it is:

```cpp
enum Facing { kRight = 0, kLeft, kUp, kDown, kFacingCount };
...
std::string walkAnimations[kFacingCount];
...
animation->Play(walkAnimations[kLeft]);
```

A **growable** list is still out, and the line is worth knowing where it is: a
`std::vector` has no length until the game runs, so there is nothing for the
Inspector to lay out and no key for the scene file to save it under. "Four
animations, one per direction" is a shape; "some waypoints" is not.

Two-dimensional arrays are left out rather than half-understood.

The scene file stores **only the values somebody changed**, so changing a
default in the code reaches every entity that never overrode it.

---

## Drawing and logging

This is Unity's `Debug` class, with Unity's names. `eng::Debug` is one include
away like everything else, and it is the whole of what a script needs for
either job.

### Logging

```cpp
Debug::Log("collected '{}' ({} left)", name, count);
Debug::LogWarning("no target to shoot at");
Debug::LogError("scene failed to load: {}", why);
```

**No channel.** The three `ENGINE_LOG_*` macros still exist and still demand
one - that is what the Console's filter buttons are made of, and engine code
that says `Physics` or `Resource` earns it. A game script has one channel it
could possibly mean, so `Debug::Log` uses `Game` and does not ask. When you
want another:

```cpp
Debug::In("AI").LogWarning("no path from {} to {}", from, to);
```

There are **three shapes** of each, and the difference matters once:

```cpp
Debug::Log("ready");                      // text, printed exactly as written
Debug::Log("hp {} of {}", hp, maxHp);     // a format and its values
Debug::Log(transform->WorldPosition());   // one value, printed as {}
```

The format shape is checked against its arguments **at compile time**, which is
why it needs a literal. The first shape takes any string, including one built a
moment ago, and prints it verbatim - so braces in it stay braces. The third is
Unity's `Debug.Log(object)`: any single value `std::format` can print, which
includes `Vec2` and `EntityId`.

Two mistakes the compiler cannot catch, because the first shape is a legitimate
call: `Debug::Log("x={}")` prints `x={}`, and `Debug::Log("{}", 1, 2)` prints
`1`.

A message from `OnUpdate` arrives sixty times a second, so:

```cpp
Debug::LogErrorOnce("no Rigidbody2D on '{}'", name);   // once per play session
Debug::LogEvery(0.5f, "pos {}", transform->WorldPosition());
```

Both are **keyed by the format string, not by the finished message**, which is
what makes the second one throttle at all.

```cpp
Debug::Assert(hp >= 0, "hp went negative: {}", hp);
```

`Assert` **logs an error and carries on** - Unity's behaviour, and the opposite
of `<cassert>`. It is not compiled out of a release build either.

`config/engine.json` ships with `logging.threshold` at `Info`, so all three
levels appear. Raise it to `Warning` or `Error` while hunting something
specific - and remember afterwards, because a raised threshold is the usual
answer to "my `Debug::Log` does nothing". `Debug::WillLog()` says.

### Shapes in the world

```cpp
Debug::DrawLine(from, to);                       // white, this frame
Debug::DrawLine(from, to, Color::Red(), 2.0f);   // red, lasts two seconds
Debug::DrawRay(origin, velocity * 0.2f);         // a point and an OFFSET
Debug::DrawArrow(from, to, Color::Cyan());
Debug::DrawCircle(centre, radius);
Debug::DrawBox(centre, size);                    // centre and FULL size
Debug::DrawCross(hit.point);                     // mark one spot
Debug::DrawText(position, "hp {}", hp);          // a label in the world
```

The last argument of each is a lifetime in seconds and **it defaults to 0,
which means one frame** - so call them every step from the thing they describe.
Pass a longer life to mark something that happened once, such as where a bullet
hit.

These are drawn as **gizmos**, which has one consequence worth knowing before
you report a bug: the **Scene** view draws gizmos and so does a built game, and
the editor's **Game** view does not, because gizmos are not part of the game.
Unity works the same way. `eng::Gizmos` is the layer underneath and is still
there if you want a shape in a specific category or a transformed box.

### Numbers on the screen

```cpp
void OnUpdate() {
    Debug::Watch("fps", 1.0f / Time::UnscaledDeltaTime());
    Debug::Watch("pos", transform->WorldPosition());
    Debug::Watch("grounded", m_grounded);
    Debug::ScreenText("step {} of {}", step, total);
}
```

One line per call, top-left of the view, rebuilt every frame. Call it from
`OnUpdate`; from `OnFixedUpdate` it prints twice in a frame that stepped twice.
It is a **debug overlay and not a UI** - a score or a health bar is
`UIRectComponent` + `UITextComponent`, and the two are separate on purpose.

### Pausing, and what kind of build this is

```cpp
Debug::Break();            // stops the editor where it is. Nothing in a built game
Debug::ClearConsole();     // empties the Console; the log FILE is untouched
Debug::IsDebugBuild();
Debug::IsEditorAttached();
```

---

## A worked example

Shoot a bullet, destroy what it hits, and keep score. Three scripts, and
nothing in them reaches past the scripting API.

```cpp
// Gun.cpp
#include <engine/Engine.h>

class Gun final : public eng::ScriptBehaviour {
public:
    eng::PrefabRef bullet;              // drag prefabs/bullet.oprefab onto this
    float speed    = 600.0f;            // how fast it leaves the barrel
    float cooldown = 0.2f;              // seconds between shots

    void OnFixedUpdate() {
        m_untilNext -= eng::Time::FixedDeltaTime();
        if (m_untilNext > 0.0f || !eng::InputMap::IsPressed("Fire")) { return; }
        m_untilNext = cooldown;

        eng::Transform2D* muzzle = Transform();
        const eng::EntityId shot = Instantiate(bullet, eng::SpawnOptions{}
            .At(muzzle->WorldPosition() + muzzle->Right() * 24.0f)
            .Facing(muzzle->WorldRotation()));

        if (eng::Entity* fired = FindEntity(shot)) {
            if (auto* body = fired->Find<eng::Rigidbody2DComponent>()) {
                body->SetVelocity(muzzle->Right() * speed);
            }
        }
        DestroyAfter(shot, 2.0f);        // it stops existing if it hits nothing
    }
private:
    float m_untilNext = 0.0f;
};

ENGINE_REGISTER_SCRIPT(Gun)
```

```cpp
// Bullet.cpp
#include <engine/Engine.h>

class Bullet final : public eng::ScriptBehaviour {
public:
    int damage = 1;                      // how much it takes off

    void OnTriggerEnter(eng::EntityId other) {
        eng::Entity* hit = FindEntity(other);
        if (hit == nullptr || !hit->HasTag("Enemy")) { return; }

        if (auto* rules = FindScriptNamedIn(*GetScene(), "ScoreKeeper")) {
            eng::Message scored;
            scored.type   = "Scored";
            scored.target = rules->OwnerId();
            scored.i0     = damage;
            eng::MessageBus::Send(scored);
        }
        Destroy(other);
        DestroySelf();
    }
};

ENGINE_REGISTER_SCRIPT(Bullet)
```

```cpp
// ScoreKeeper.cpp
#include <engine/Engine.h>

class ScoreKeeper final : public eng::ScriptBehaviour {
public:
    eng::EntityRef label;                // [UITextComponent] where to show it

    void OnStart() {
        m_subscription = eng::MessageBus::SubscribeBroadcast(
            "Scored", [this](const eng::Message& message) {
                if (message.target == OwnerId()) { m_score += message.i0; }
            });
        Show();
    }

    void OnFixedUpdate() {
        if (m_score != m_shown) { Show(); }
    }

    void OnDestroy() {
        // THE SUBSCRIPTION HOLDS A LAMBDA DEFINED IN THIS LIBRARY, and the bus
        // does not know that. Leaving it behind is a use-after-unload the next
        // time scripts are rebuilt.
        eng::MessageBus::Unsubscribe(m_subscription);
    }

private:
    void Show() {
        m_shown = m_score;
        if (auto* text = label.Find<eng::UITextComponent>()) {
            text->text = "Score: " + std::to_string(m_score);
        }
    }

    eng::SubscriptionId m_subscription = 0;
    int m_score = 0;
    int m_shown = -1;
};

ENGINE_REGISTER_SCRIPT(ScoreKeeper)
```

---

## Playing an animation

A spritesheet can store named animations - see the editor guide - and a
`SpriteAnimationComponent` plays one into the sprite beside it.

```cpp
void OnStart() override {
    if (auto* anim = Find<eng::SpriteAnimationComponent>()) {
        anim->Play("idle");
    }
}
```

`Play` takes the animation's **name**, not its path. The component already
knows which sheet it came from, so `"jump"` is all there is to say - and a
script that said `"textures/hero.png#jump"` would repeat the sheet's path at
every call site and break the day the sheet was renamed.

Whether it **restarts** or **carries on** when that animation is already the
one loaded is the animation's own setting — *Carry on where it left off*, in the
sheet's Animations list. Off by default, so `Play("hit")` twice flashes twice;
on for a walk cycle, so stopping and setting off again does not hitch. Ask the
component with `Resumes()` if a script needs to know.

Switching to a *different* animation always starts it at the beginning, and a
non-looping clip that has finished always starts again — there is nothing to
carry on with in either case.

`Play` returns `false` and says so in the Console when the sheet has no
animation of that name.

```cpp
void OnFixedUpdate(float) override {
    auto* anim = Find<eng::SpriteAnimationComponent>();
    if (anim == nullptr) {
        return;
    }
    if (hurt) {
        anim->Play("hit");
        hurt = false;
    }
    // A non-looping animation holds its last frame and reports that it ended.
    if (anim->IsFinished()) {
        DestroySelf();
    }
}
```

The rest of it:

| Call | What it does |
| ---- | ------------ |
| `Play("name")` | Switch to that animation and start it from the beginning. |
| `Play()` | Resume. Rewinds first only if it had finished. |
| `Restart()` | Back to the first frame, playing. |
| `Pause()` | Stop advancing and keep the picture that is on screen. |
| `Stop()` | Stop, rewind, and put the sprite's own image back. |
| `IsPlaying()` | Whether it is advancing. |
| `IsFinished()` | A non-looping animation that reached its end. Stays true until something plays it again. |
| `SetSpeed(2.0f)` | A multiplier on the sheet's frame rate. |

**Do not keep the pointer.** Ask with `Find` each time, the same rule as for
every other component: the component dies with its entity, and the whole scene
is rebuilt by Play, by Stop and by every undo step.

**What it deliberately does not do:** there is no blending, no state machine and
no transitions. It plays one animation at a time, which is why it is called a
Sprite Animation and not an Animator.

## What is not here yet

Said plainly, so that nobody spends an afternoon looking:

| Unity | Status |
|---|---|
| `AudioSource`, `AudioClip`, `PlayOneShot` | **No audio at all.** Not a gap in the scripting API - the subsystem does not exist. |
| `Animator`, animation clips | No animator. A frame index advanced with a float and a modulo is the current answer. |
| `ParticleSystem` | None. Spawning entities works at the scale this engine targets. |
| `StartCoroutine`, `WaitForSeconds` | None. An `enum Phase` plus a float is the house pattern, and `assets/scripts/GameRules.cpp` is the worked example. |
| `Invoke`, `InvokeRepeating` | None. Count down a float in `OnFixedUpdate`. |
| `OnEnable`, `OnDisable` | None. Writing one is a compile error that says so. Compare `IsActive()` against last step. |
| `enabled`, `isActiveAndEnabled` | **No per-component switch.** `SetActive` / `IsActive` are the *entity's*, and `IsActive()` already means Unity's `activeInHierarchy` - a different thing from `enabled`, so it is left out rather than spelled misleadingly. |
| `GetComponent<T>`, `CompareTag`, `FindObjectOfType<T>` | The same jobs under house names - `Find<T>`, `HasTag`, `FindFirstComponent<T>`. The *properties* are spelled Unity's way; the *methods* are not, and there are no Unity-spelled aliases for them. |
| `SendMessage`, `BroadcastMessage` | Different model: `eng::MessageBus` is queued and typed rather than a reflective call by method name. See **Talking to other scripts**. |
| `print` | Deliberately absent. `eng::Debug::Log` is the one to use - it keeps the level, which `print` throws away, and fills in the channel so you do not have to. `ENGINE_LOG_INFO` and friends are still there for engine code, which means a different channel on almost every line. |
| `LateUpdate` | None. Stage 700 already runs after all gameplay and movement; see `SystemOrder.h`. |
| `Input.GetKey(KeyCode)` | Deliberately absent - use named actions. |
| Gamepad, analog axes | Not wired up. `GetAxis` is currently Unity's `GetAxisRaw`. |
| `DontDestroyOnLoad`, additive scenes | There is exactly one `Scene` in the process. Carry state in a file-static, or save it. |
| Object pooling | None. Spawn and destroy. |
| Physics joints | None. Integrate one constraint by hand against `AddForce`. |
| `PlayerPrefs` | None. |
| Nested prefabs, prefab variants | Not supported; making a prefab out of something already in one is refused rather than flattened. |

---

## Where the real documentation is

Every header listed here opens with a long comment explaining what it does and
why it is built that way, and those comments go further than this page does:

| For | Read |
|---|---|
| How scripts work at all | `engine/include/engine/scene/ScriptComponent.h` |
| Why there is no `virtual` | `engine/include/engine/scene/ScriptHooks.h` |
| Inspector variables | `engine/include/engine/scene/ScriptFields.h`, `engine/include/engine/tools/ScriptVars.h` |
| Entities and components | `engine/include/engine/scene/Entity.h`, `Component.h` |
| Spawning and destroying | `engine/include/engine/scene/DeferredOps.h`, `Scene.h` |
| Prefabs | `engine/include/engine/scene/Prefab.h`, `PrefabRef.h` |
| The transform | `engine/include/engine/math/Transform2D.h` |
| Physics and queries | `engine/include/engine/physics/Physics2D.h`, `Collider.h`, `Rigidbody.h` |
| Entity references | `engine/include/engine/scene/EntityRef.h` |
| Animation and image references | `engine/include/engine/scene/AnimationRef.h`, `ImageRef.h` |
| Messages | `engine/include/engine/scene/Messaging.h` |
| Input | `engine/include/engine/input/InputMap.h`, `Pointer.h` |
| The frame, and system order | `engine/include/engine/Engine.h`, `scene/SystemOrder.h` |

[docs/engine-tour.md](engine-tour.md) is the map of the whole engine.
[docs/editor-guide.md](editor-guide.md) covers the editor, including prefabs and
the script build.
[docs/physics-playground.md](physics-playground.md) is a scene that exercises
every physics interaction, with what a failure looks like at each one - and the
seven scripts it is built from are plain scripts you can drop into your own
project.
