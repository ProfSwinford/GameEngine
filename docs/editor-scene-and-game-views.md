# The Scene view and the Game view

*Post-Week-10 editor work. Everything here is built on subsystems the ten weeks
already produced — no new dependency, no new third-party library.*

Until this change the editor rendered the world **straight to the back buffer**
and drew the IDE on top of it. That gives you exactly one view, from wherever
the game camera happens to be standing, and it means the panels are floating
over the game rather than containing it.

The goal was Unity's arrangement: a **Scene** view you edit in, a **Game** view
you play in, both of them ordinary dockable panels.

---

## The one thing that made it possible

Two views of one world need two pictures in the same frame, and a back buffer
can only ever hold one. So the first piece is an off-screen surface:

```cpp
class RenderTarget {                    // engine/platform/Renderer.h
    bool  Resize(i32 width, i32 height);
    void* NativeTexture() const;        // for ImGui::Image
};

Renderer::SetRenderTarget(&target);     // draw here instead of the window
Renderer::SetRenderTarget(nullptr);     // back to the window
```

Two details that are not obvious until they bite:

- **`Resize` recreates the texture only when the size actually changes.** A
  docked panel reports a new content region on almost every frame it is being
  dragged, and reallocating a render texture per frame is a stutter you can
  see.
- **`Renderer::OutputSize()` returns the bound target's size**, not the
  window's. Everything downstream — `Camera::SetViewportSize`, `VisibleBounds`,
  `ScreenToWorld` — asks that one function, so a view that is 900×600 inside a
  1600×950 window gets correct projection for free rather than needing a
  parallel "but what size are we really" path.

`NativeTexture()` returns `void*`. Naming the SDL type would put `SDL_Texture`
back into a public header, which Week 3 spent a week removing.

---

## The bug that the split of `DebugDraw::Render` is about

`DebugDraw::Render(camera, dt)` used to draw the queue **and** age it, dropping
anything whose lifetime had expired. That is correct for one view and silently
wrong for two: the Scene view drew the queue and consumed it, and the Game view
got an empty one.

It is now two calls with one caller each:

```cpp
DebugDraw::Render(camera);        // draws. Called once per VIEW.
DebugDraw::EndFrame(deltaSeconds) // ages and drops. Called once per FRAME.
```

A function that both reports state and mutates it is fine right up until
something needs to report twice. Worth noticing that the failure was invisible
in the Scene view — the view that worked — and only showed in the second one.

---

## Play and Stop

Unity's contract, and the reason the Play button is safe to press on something
you have been editing for an hour:

| | |
|---|---|
| **Play** | serialise the whole scene to a string, keep it, unpause the clock |
| **Stop** | clear the deferred queues and the message bus, deserialise the string, pause the clock |

Three decisions inside that are worth stating.

**Play is REFUSED if the snapshot is incomplete.** `Scene::SaveToString` fails
when any component declines to serialise, where `Scene::Save` merely warns. The
difference is deliberate: a *file* with a component missing is a file you can
open and fix, but a *snapshot* with a component missing silently deletes that
component when Stop restores it. Turning a non-destructive action into a
destructive one without saying so is the worst failure this feature could have,
so it does not start.

**The queues are cleared before the restore.** A destroy queued on the last
frame of play would otherwise be applied to the freshly restored scene and
delete an entity that the user never touched.

**`m_sourcePath` is not touched by `LoadFromString`.** Restoring a snapshot must
not make the scene forget which file it came from — Ctrl+S after Stop still
saves to the right place.

### Verified, not asserted

```
sandbox --playmode-check
```

```
EDIT MODE: 14 entities, refcount 13
play mode entered (5403 byte snapshot)
PLAY MODE: 12 entities, refcount 10 (3 destroyed, 1 spawned, Player moved)
play mode exited; scene restored
PLAY/STOP: 14 entities before, 12 during, 14 after;
           refcount 13 -> 10 -> 13; 0 mismatch(es) -- PASS
```

The middle line is the part that is easy to leave out. Checking only that
"after" matches "before" would pass perfectly for a Play button that did
nothing at all, so the check **fails if the scene did not change during play**.

---

## Focus follows Play, and the bug on the way there

Pressing Play brings the Game tab forward and hands it the keyboard. "Hands it
the keyboard" is two things, and doing one gives you a view that looks focused
and ignores every key — so both live behind one call:

```cpp
EditorGui::SetGameInputFocus(bool);   // ImGui nav off AND capture reporting off
```

The first version of the focus request did not work, and the reason is worth
recording. The Toolbar raised a flag, and `GamePanel::Draw` acted on it with
`ImGui::SetWindowFocus()`. That reads correctly and never ran: **when Play is
pressed the Game view is a background tab, `ImGui::Begin` returns false, and
`Draw` is not called.** The request was read on no frame at all.

Focus is now requested from `EditorApp`, **by name**, outside any Begin/End —
because the panel that needs focusing is precisely the panel that is not
running.

The same shape caused a second, quieter bug: `GamePanel` caches "am I focused?"
during `Draw`, so once it became a background tab it kept reporting `true`
forever, and the keyboard kept going to a game nobody could see. Hence:

```cpp
virtual void Panel::OnHidden() {}   // called INSTEAD of Draw on frames the
                                    // panel is closed, collapsed, or a
                                    // background tab
```

Any panel that caches per-frame state now has somewhere to clear it, and the
only thing that knows a panel did not run is the shell that owns `Begin`/`End`.

---

## Frame order in the editor

```
BeginFrame            events -> ImGui backend
Simulate              fixed timestep; paused in edit mode
  BeginDockspace / menu bar / EndDockspace
  DrawPanels          every panel's Draw(), or OnHidden()
  FocusGameViewIfRequested
ScenePanel::RenderView    -> its own target, its own camera, WITH debug draw
GamePanel::RenderView     -> its own target, game camera, WITHOUT debug draw
DebugDraw::EndFrame       age the queue ONCE, after both views read it
SetRenderTarget(nullptr); Clear
EditorGui::EndFrame       ImGui draws on top
PresentFrame
```

The views render **after** the panels because a view cannot know its size until
ImGui has laid the panel out, and **before** `EditorGui::EndFrame` because that
is when ImGui samples the textures. Filling them in between means the images
are current rather than one frame stale.

This is also why the editor calls `Engine::RenderWorld` itself rather than
`Engine::RenderFrame`: `RenderFrame` draws to the window through the game
camera, and there is no longer anything on the window to draw to.

---

## Scene view controls

| | |
|---|---|
| middle- or right-drag | pan |
| wheel | zoom, **toward the cursor** |
| left-click | select — smallest area under the cursor wins |
| drag a gizmo arm | move the selection on X, on Y, or both from the centre square |
| **F** | frame the selection |

Zoom-toward-cursor is six lines and worth all of them: zooming to the centre
means every zoom is followed by a pan to put back what you were looking at.

**The gizmo is drawn with ImGui, not with DebugDraw**, and that is the one
place the Week 6 rule about keeping the two apart argues *for* ImGui. A
manipulator has to be a fixed size on screen at any zoom, and has to hit-test
in the coordinates the mouse arrives in. The panel's own draw list gives both;
world-space debug geometry gives neither.

---

## What this made honest, and what it made stale

**Fixed:** the Viewport panel's live `ScreenToWorld` readout used the raw
*window* mouse position. That was a genuinely useful check while the world
filled the window, and became a lie the moment the world moved into a panel
drawn at an offset. The cursor readout moved into the Scene view, where the
number sits beside the cursor that produced it; what stays in the Viewport
panel is the part that never depended on the mouse — a fixed point pushed
through the transform and back, with the error shown.

**Fixed:** the Viewport panel now says *which* camera it edits. With two
cameras on screen, unlabelled position and zoom fields are unusable, because
you cannot tell which of the two pictures they will move.

**Fixed:** `SpinComponent` had no inspector, which was a poor showing for the
component whose entire argument is that three numbers in a data file produce a
three-deep orbiting system. It is far more convincing when you can drag one of
them and watch the hierarchy respond.

**Fixed:** `--save-check` left `scenes/_roundtrip_check.json` behind in
`assets/scenes/`, which is a *discovered* directory — so the temp file appeared
in the editor's Open Scene menu and in every other check that walks the folder.
It is removed on pass and deliberately **kept on failure**, because on failure
it is the evidence.

---

## Still not done

- No drag-and-drop: no dragging a texture from the Resource panel onto a
  sprite, and no drag-to-reparent in the Hierarchy.
- Translate only. No rotate or scale gizmo.
- The Game view runs the **engine**, not your game logic. `CollectorGame` lives
  in the `sandbox` target, which the editor does not link — moving it into a
  shared library both targets link would change the Week 10 gate story, which
  is a deliberate decision rather than an oversight.
- No undo.
