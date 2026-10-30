// ============================================================================
//  UndoStack.cpp - see UndoStack.h for why this snapshots the scene instead of
//  recording what each action did.
// ============================================================================

#include "UndoStack.h"

#include "EditorApp.h"
#include "panels/TilePalettePanel.h"

#include <engine/Engine.h>
#include <engine/core/Log.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <imgui.h>

#include <utility>
#include <vector>

namespace editor {
namespace Undo {
namespace {

// ---------------------------------------------------------------------------
//  HOW DEEP, AND WHAT IT COSTS.
//
//  The largest scene in this repository is 16 KB of JSON, so 64 steps is
//  about a megabyte per stack and two in the worst case. That is a price worth
//  paying outright rather than making configurable - a setting somebody has to
//  find and understand is a worse answer than a number that is simply large
//  enough.
//
//  The oldest step is dropped rather than refusing to record a new one.
//  Running out of undo at the far end is a mild disappointment; a tool that
//  stops recording your work because a buffer filled up is a bug.
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxSteps = 64;

struct Snapshot {
    std::string scene;   // Scene::SaveToString - the whole thing

    // The selection, BY NAME. See the header for why ids would not survive.
    std::vector<std::string> selection;
    std::string              active;

    // ---- AND THE TILE BRUSH'S TARGET, for the same reason ----------------
    //
    // TilePaintState holds the map it paints into as an EntityId too. After a
    // restore that id is not merely stale, it can be VALID and name a
    // different entity - ids are handed out as a compacted index plus a
    // generation that resets to 1 on a reload, so {5,1} still passes every
    // validity check and resolves to whatever is now sixth.
    //
    // It is the only id in editor state that PERSISTS across frames besides
    // the selection. The panels hold a few others - HierarchyPanel's rename
    // target and pending reparent, InspectorPanel's pending removal - but each
    // is set and consumed within a frame or two, and an undo can only land
    // between the two while a modal is open, which the shortcut guard already
    // forbids. Those are not reached from here; EditorApp owns no pointer to
    // the panels that hold them.
    //
    // Left unfixed, the first paint stroke after a Ctrl+Z could land in a
    // different tilemap, or in an entity that has no tilemap at all. Carrying
    // the name and re-resolving is the same treatment the selection gets.
    std::string brushTarget;
};

Snapshot              g_baseline;
std::vector<Snapshot> g_back;
std::vector<Snapshot> g_forward;

// Is there a baseline at all? Distinct from "the baseline is empty", because
// an empty scene snapshots to a perfectly valid short document.
bool g_ready = false;

// The scene as it was when it was last written to disk, so that stepping back
// onto it can clear the unsaved marker. Empty means "never saved in this
// session", and then every state counts as unsaved.
std::string g_saved;

// ---------------------------------------------------------------------------
//  THE EDIT COUNTER AS LAST SEEN, and the reason this feature is affordable.
//
//  Serialising the scene is NOT cheap, and this is the one place that figure
//  is written down - the other two notes that used to repeat it now point
//  here.
//
//  MEASURED, not estimated: a throwaway doctest built 200 entities with
//  distinct names and transforms, called Scene::SaveToString 200 times and
//  divided. It came to 24.6 KB of JSON and about 15 MILLISECONDS per call in
//  a debug build - a whole frame at 60Hz. (Release is several times faster,
//  but the debug editor is the one being used while developing, so the debug
//  figure is the one that matters.) To check it again, write that test; it was
//  not kept, because a test whose only output is a number nobody asserts on
//  is not a test.
//
//  The first version of this file
//  serialised on every frame the user was not mid-gesture and compared the
//  result against the baseline, which is correct, trivially complete, and
//  would have halved the editor's frame rate the moment anybody edited
//  anything.
//
//  So the question "has anything happened?" is answered by one integer
//  comparison instead. EditorState::MarkDirty advances a counter, every edit
//  site goes through it because the flag behind it is private, and this is
//  the last value that was looked at. When it has not moved, Poll returns
//  having done nothing measurable.
//
//  The content comparison is still done when the counter HAS moved, and that
//  is not redundant: a drag that ends where it started, or one of the
//  Inspector's `changed |= ...` accumulators firing on a widget that wrote
//  the same value back, both advance the counter without changing the scene.
//  Recording those would give a Ctrl+Z that appears to do nothing.
// ---------------------------------------------------------------------------
unsigned long long g_seenSerial = 0;

// ---------------------------------------------------------------------------
//  IS THE USER BETWEEN ACTIONS?
//
//  The one moment a snapshot must not be taken is the middle of a gesture. A
//  gizmo drag and an ImGui DragFloat both rewrite the scene on every frame
//  they are held, and the tile brush paints a cell per frame while the mouse
//  moves - snapshotting those would make Ctrl+Z walk back through a drag a
//  pixel at a time instead of undoing it.
//
//  THE MOUSE BUTTONS ARE NAMED INDIVIDUALLY rather than using
//  ImGui::IsAnyMouseDown(), which imgui.h marks "[WILL OBSOLETE]". Three
//  comparisons are cheaper than a deprecation to chase later.
// ---------------------------------------------------------------------------
bool BetweenActions() {
    if (ImGui::IsAnyItemActive()) {
        return false;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        return false;
    }

    // Still typing. The name in the Hierarchy is not committed yet, and the
    // scene would be snapshotted mid-word.
    return !ImGui::GetIO().WantTextInput;
}

// ---------------------------------------------------------------------------
//  THE CHEAP HALF OF A SNAPSHOT: who is selected, and what the brush points
//  at. A handful of string copies, no serialisation.
//
//  Separate from Capture because it has to happen far more often. See the
//  note in Poll.
// ---------------------------------------------------------------------------
void CaptureSelection(Snapshot& out) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    out.selection.clear();
    for (const eng::EntityId id : EditorState::Get().selection) {
        if (const eng::Entity* entity = scene.Get(id); entity != nullptr) {
            out.selection.push_back(entity->Name());
        }
    }

    out.active.clear();
    if (const eng::Entity* entity = scene.Get(EditorState::Get().selected);
        entity != nullptr) {
        out.active = entity->Name();
    }

    out.brushTarget.clear();
    if (const eng::Entity* entity = scene.Get(TilePaintState::Get().tilemap);
        entity != nullptr) {
        out.brushTarget = entity->Name();
    }
}

bool Capture(Snapshot& out) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::string error;
    if (!scene.SaveToString(out.scene, error)) {
        // ONCE, not every frame. Poll runs sixty times a second, and a scene
        // that cannot be serialised would otherwise fill the Console with the
        // same line until somebody fixed it - burying whatever else was wrong.
        static bool complained = false;
        if (!complained) {
            complained = true;
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "undo is not recording, because this scene cannot be "
                             "snapshotted: {}", error);
        }
        return false;
    }

    CaptureSelection(out);
    return true;
}

bool Apply(const Snapshot& snapshot, std::string& outError) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    // ---- THE SAME HYGIENE Engine::ExitPlayMode PERFORMS ------------------
    //
    // Putting a snapshot back is exactly what Stop does, and the two reasons
    // it clears these apply here word for word: a destroy queued by a panel
    // earlier in this frame would otherwise be applied to the freshly
    // restored scene and delete an entity in it, and a message queued against
    // entities that are about to stop existing has nowhere to go.
    //
    // ClearQueue, NOT Clear, on the bus - Clear drops every SUBSCRIPTION too.
    // See the note in Engine::ExitPlayMode, which is where that was found out
    // the hard way.
    eng::DeferredOps::Clear();
    eng::MessageBus::ClearQueue();

    // Dropped BEFORE the load, because every id in it is about to become
    // meaningless. It is rebuilt from names below.
    EditorState::Get().ClearSelection();

    if (!scene.LoadFromString(snapshot.scene, outError)) {
        return false;
    }

    // ---- AND THE CAMERA IS DELIBERATELY LEFT ALONE -----------------------
    //
    // Stop moves the view back to the scene's saved camera, because a play
    // session may have flown it anywhere. Undo must not: you are looking at
    // the thing you are undoing, and yanking the viewport on every Ctrl+Z
    // would make the feature unusable. Scene::LoadFromString only sets the
    // scene's own recorded camera, not the live one, so this is a matter of
    // not adding the line Stop has rather than of undoing anything.

    for (const std::string& name : snapshot.selection) {
        if (const eng::EntityId id = scene.Find(name); !id.IsNull()) {
            EditorState::Get().AddToSelection(id);
        }
    }

    // LAST, because AddToSelection makes what it adds the active entity - so
    // adding the active one after the rest is what puts it back in charge.
    if (!snapshot.active.empty()) {
        if (const eng::EntityId id = scene.Find(snapshot.active); !id.IsNull()) {
            EditorState::Get().AddToSelection(id);
        }
    }

    // CLEARED FIRST, SO A FAILED LOOKUP LEAVES NOTHING rather than the old id.
    // An empty target makes the paint tools do nothing until a map is chosen,
    // which is the safe answer; a stale one paints into the wrong entity.
    TilePaintState::Get().tilemap = eng::EntityId{};
    if (!snapshot.brushTarget.empty()) {
        TilePaintState::Get().tilemap = scene.Find(snapshot.brushTarget);
    }
    return true;
}

// The unsaved marker, worked out rather than assumed. Stepping back onto the
// state that is on disk clears it; anything else sets it.
void MarkDirtyFromBaseline() {
    if (g_saved.empty() || g_baseline.scene != g_saved) {
        EditorState::Get().MarkDirty();
    } else {
        EditorState::Get().ClearDirty();
    }
}

} // namespace

void Reset() {
    g_back.clear();
    g_forward.clear();
    g_baseline   = Snapshot{};
    g_ready      = Capture(g_baseline);
    g_seenSerial = EditorState::Get().EditSerial();

    // THE SCENE AS IT OPENED COUNTS AS SAVED. It is what the file says, so
    // editing and then undoing back to here should leave no asterisk.
    g_saved = g_ready ? g_baseline.scene : std::string{};
}

void Poll() {
    // The simulation is rewriting the scene; none of it is an edit.
    if (eng::Engine::Get().IsInPlayMode()) {
        return;
    }
    if (!g_ready) {
        // ---- TRY AGAIN FOR A BASELINE, BUT DO NOT CALL Reset() -----------
        //
        // Reset clears both stacks, and that is exactly wrong here: a scene
        // that momentarily could not be serialised must not cost somebody the
        // history that was recorded before it. Only a scene CHANGE clears the
        // history, and this is not one.
        g_ready      = Capture(g_baseline);
        g_seenSerial = EditorState::Get().EditSerial();
        return;
    }

    // ======================================================================
    //  THE SELECTION IS KEPT CURRENT SEPARATELY, AND CHEAPLY.
    //
    //  It cannot ride on the edit counter. Selecting something does not
    //  advance it - clicking around is not an edit, and a stack full of steps
    //  that change nothing would be a Ctrl+Z that appears to do nothing
    //  several times running. But the integer gate below then returns before
    //  anything here is reached, so the baseline would keep whatever was
    //  selected ONE EDIT AGO: undoing a move would put the entity back and
    //  leave a different one selected, which is exactly the stale selection
    //  that carrying names was supposed to prevent.
    //
    //  So this runs every settled frame. It is affordable because only the
    //  SCENE is expensive to capture; a few names are nothing.
    //
    //  AN EMPTY SELECTION IS NOT RECORDED, and that is deliberate. Deleting
    //  marks the scene and clears the selection in the same breath, so
    //  overwriting here would throw away the very thing undo wants to put
    //  back - keeping the last non-empty selection is what makes undoing a
    //  delete reselect what it restored. The cost is that deliberately
    //  deselecting everything and then editing restores the older selection,
    //  which is a far smaller surprise.
    // ======================================================================
    if (BetweenActions()) {
        Snapshot live;
        CaptureSelection(live);
        if (!live.selection.empty()) {
            g_baseline.selection = std::move(live.selection);
            g_baseline.active    = std::move(live.active);
        }
        g_baseline.brushTarget = std::move(live.brushTarget);
    }

    // THE CHEAP TEST, and on the overwhelming majority of frames nothing
    // below it runs. See g_seenSerial.
    if (EditorState::Get().EditSerial() == g_seenSerial) {
        return;
    }
    if (!BetweenActions()) {
        // Something HAS changed, but the gesture that is changing it is still
        // in progress - so the counter is deliberately left unread. The next
        // frame asks again, and the frame after the mouse comes up is the one
        // that records a single step for the whole drag.
        return;
    }

    // ======================================================================
    //  ANYTHING QUEUED HAS NOT HAPPENED TO THE SCENE YET.
    //
    //  Deleting an entity does not remove it; it queues a destroy, which
    //  Engine::Simulate drains at the top of the NEXT frame. So on the frame
    //  Delete is pressed, the counter has moved and the scene is still
    //  exactly what it was - and reading the counter here would consume the
    //  only notice that anything happened. When the destroy landed a frame
    //  later there would be nothing left to see it, and Delete followed by
    //  Ctrl+Z would do nothing at all.
    //
    //  That is the single most important thing undo has to get right, and it
    //  was wrong until this check existed. Leaving the counter unread costs
    //  one frame of waiting; the queue is drained every frame whether any
    //  simulation steps ran or not, which is deliberate - see the note at the
    //  end of Engine::Simulate - so it cannot stall.
    // ======================================================================
    if (eng::DeferredOps::PendingDestroyCount() > 0) {
        return;
    }

    Snapshot now;
    if (!Capture(now)) {
        // ---- THE COUNTER IS DELIBERATELY LEFT UNREAD ---------------------
        //
        // Consuming it and then failing would drop this edit from history for
        // good: nothing would look at the scene again until the next edit, and
        // by then the state this one started from is gone.
        //
        // The cost is that a scene which PERSISTENTLY cannot be serialised is
        // re-attempted every frame, which is the expense this design exists to
        // avoid - with one log line as the only notice. That is the right way
        // round: no component in the tree can currently fail to serialise, so
        // the frame cost is hypothetical while the dropped edit would be real.
        return;
    }
    g_seenSerial = EditorState::Get().EditSerial();

    if (now.scene == g_baseline.scene) {
        // The counter moved but the scene did not - a drag that ended where
        // it started, or one of the Inspector's `changed |= ...` accumulators
        // firing on a widget that wrote the same value back. Recording it
        // would give a Ctrl+Z that appears to do nothing. The selection is
        // already current; see the note above.
        return;
    }

    g_back.push_back(std::move(g_baseline));
    if (g_back.size() > kMaxSteps) {
        g_back.erase(g_back.begin());
    }

    // ANY NEW EDIT ENDS THE REDO CHAIN. Keeping it would offer to step
    // "forward" into a future that the edit just replaced.
    g_forward.clear();

    g_baseline = std::move(now);
}

bool CanUndo() {
    return !g_back.empty() && !eng::Engine::Get().IsInPlayMode();
}

bool CanRedo() {
    return !g_forward.empty() && !eng::Engine::Get().IsInPlayMode();
}

std::size_t UndoDepth() {
    return g_back.size();
}

std::size_t RedoDepth() {
    return g_forward.size();
}

namespace {

// Both directions are the same operation with the stacks swapped, so they are
// written once. `from` is the stack to take the target off, `to` is where the
// state being left behind goes.
bool Step(std::vector<Snapshot>& from, std::vector<Snapshot>& to, const char* what,
          std::string& outError) {
    if (eng::Engine::Get().IsInPlayMode()) {
        outError = "stop playing first - the scene you are looking at is a play "
                   "session, and it is discarded when you press Stop anyway";
        return false;
    }
    if (from.empty()) {
        outError = std::string("there is nothing to ") + what;
        return false;
    }

    // WHERE WE ARE NOW IS CAPTURED FIRST, and a failure here stops everything.
    // Stepping without being able to record the current state would make the
    // step itself unrepeatable in the other direction.
    Snapshot current;
    if (!Capture(current)) {
        outError = "the scene as it stands could not be snapshotted, so this would be "
                   "a one-way trip";
        return false;
    }

    Snapshot target = std::move(from.back());
    from.pop_back();

    if (!Apply(target, outError)) {
        // PUT IT BACK. Apply failing leaves the scene in whatever state
        // LoadFromString got to, which is bad enough without also having
        // thrown away the step - see the note in the header.
        from.push_back(std::move(target));

        // AND THE BASELINE IS DELIBERATELY LEFT ALONE, which breaks the usual
        // invariant that it describes the scene - knowingly, because the
        // alternative is worse. It still holds the state from BEFORE this
        // attempt, so the next real edit records that as its undo step and
        // one Ctrl+Z recovers from the half-loaded scene. Re-baselining here
        // would throw that away to keep a tidier invariant.
        //
        // Reachable only if LoadFromString fails on a document SaveToString
        // produced, which would mean the serialiser emitted something it
        // cannot read back.
        return false;
    }

    to.push_back(std::move(current));
    g_baseline = std::move(target);
    MarkDirtyFromBaseline();

    // READ LAST, AND DELIBERATELY AFTER MarkDirtyFromBaseline, which may have
    // advanced the counter itself. Undoing is a change to the scene, so
    // marking it is right - but it is not an edit to be recorded, and without
    // this the next Poll would serialise the whole scene to discover that the
    // state it is looking at is the one it just put there.
    g_seenSerial = EditorState::Get().EditSerial();
    return true;
}

} // namespace

bool StepBack(std::string& outError) {
    return Step(g_back, g_forward, "undo", outError);
}

bool StepForward(std::string& outError) {
    return Step(g_forward, g_back, "redo", outError);
}

void NoteSaved() {
    // ---- SNAPSHOTTED AFRESH, NOT TAKEN FROM THE BASELINE -----------------
    //
    // It is tempting to write `g_saved = g_baseline.scene` on the grounds that
    // the baseline IS the current scene. It usually is, and the exception is
    // reachable: Poll commits an edit only once the gesture that made it has
    // finished, so for the frame between an edit and its commit the baseline
    // is one step behind. A Ctrl+S landing in that gap would record the state
    // BEFORE the edit as the saved one - and then undoing back to exactly what
    // is on disk would still show the unsaved asterisk, which is the one thing
    // g_saved exists to prevent.
    //
    // One serialisation on save is nothing; saving already writes the whole
    // file.
    Snapshot now;
    if (Capture(now)) {
        g_saved = std::move(now.scene);
    }
}

} // namespace Undo
} // namespace editor
