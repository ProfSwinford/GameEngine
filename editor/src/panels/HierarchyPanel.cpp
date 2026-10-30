// ============================================================================
//  HierarchyPanel.cpp - the tree of entities. See HierarchyPanel.h.
// ============================================================================

#include "panels/HierarchyPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ObjectMenu.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>   // memcpy, for reading a drag payload back out
#include <vector>

namespace editor {

void HierarchyPanel::DrawNode(eng::Entity& entity) {
    EditorState&      state     = EditorState::Get();
    eng::Scene&       scene     = eng::Engine::Get().GetScene();
    eng::Transform2D& transform = entity.Transform();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;

    // An entity with no children is a leaf: no expand arrow, and no matching
    // TreePop needed later.
    //
    // ASKED ONCE AND REMEMBERED, rather than asking Children() again further
    // down to decide whether to call TreePop. Those two answers have to
    // agree: ImGui pushes onto its id stack only for a non-leaf, and popping
    // when nothing was pushed takes the stack depth negative, which it then
    // shifts by. Deciding twice means a child list that changed in between
    // produces a crash nowhere near the code that caused it.
    const bool isLeaf = transform.Children().empty();
    if (isLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    // EVERY member of the selection is highlighted, not just the active one.
    // A multi-selection you cannot see is worse than none, because the next
    // Delete acts on all of it.
    if (state.IsSelected(entity.Id())) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // The hidden part after ## is the entity's id rather than its name,
    // because two entities can end up displaying the same name and ImGui would
    // treat them as one row. See the note about widget identity in Panel.h.
    char label[192];
    std::snprintf(label, sizeof(label), "%s##%d_%d", entity.Name().c_str(),
                  entity.Id().index, entity.Id().generation);

    const bool open = ImGui::TreeNodeEx(label, flags);

    // WHERE THIS ROW IS, recorded in the order rows are drawn. A Shift-range
    // and the marquee both need to know what is above what, and this is the
    // only place that knows. See HierarchyPanel::Row.
    m_rows.push_back(Row{entity.Id(), ImRect(ImGui::GetItemRectMin(),
                                             ImGui::GetItemRectMax())});

    // This row can be picked up and dropped on another one. Before the click
    // handling below, because a drag is not a click and ImGui needs to see
    // the source first to decide which it is.
    DrawEntityDragSource(entity);

    // IsItemToggledOpen tells clicking the expand arrow apart from clicking
    // the row, so opening a branch does not also change the selection.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        HandleRowClick(entity.Id());
    }

    // A drop target for this row, placed immediately after the row was drawn
    // because ImGui attaches drop targets to the most recently drawn item.
    // Drop a texture here to give this entity a sprite; drop a .cpp to attach
    // that script.
    AcceptAssetDropOnEntity(entity.Id());

    // ...and a second one for a dropped ENTITY, which parents it to this row.
    // Two separate target blocks on the same item is fine - the first one has
    // closed itself before this one opens.
    DrawEntityDropTarget(entity.Id());

    DrawContextMenu(entity);

    if (open && !isLeaf) {
        // A COPY OF THE LIST, not the list itself.
        //
        // Drawing a child can change this vector while it is being walked.
        // The Duplicate menu item is the one that does it today: a duplicated
        // entity is given the same parent as the original, so duplicating a
        // child appends to the very vector this loop is reading, and that
        // append reallocates. Both of a range-for's iterators then point at
        // freed memory, and the loop reads it on its next step.
        //
        // Copying a handful of pointers once per open branch is nothing, and
        // it makes the loop independent of anything the rows do. New rows
        // appear on the next frame, which is exactly when they should.
        const std::vector<eng::Transform2D*> children = transform.Children();

        for (eng::Transform2D* child : children) {
            // The transform tree knows about parenting; the scene's list of
            // entities is flat. So finding the child ENTITY for a child
            // TRANSFORM means searching for the entity whose transform this is.
            eng::Entity* childEntity = nullptr;
            scene.ForEach([&](eng::Entity& candidate) {
                if (&candidate.Transform() == child) {
                    childEntity = &candidate;
                }
            });
            if (childEntity != nullptr) {
                DrawNode(*childEntity);   // recursion: a child may have children
            }
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::HandleRowClick(eng::EntityId id) {
    EditorState&   state = EditorState::Get();
    const ImGuiIO& io    = ImGui::GetIO();

    if (io.KeyShift && !state.selected.IsNull()) {
        // DEFERRED, because the other end of the range may be a row further
        // down that has not been drawn yet - so m_rows does not contain it
        // and the range cannot be worked out until Draw has finished. See
        // ApplyPendingRange.
        m_pendingRangeTo  = id;
        m_hasPendingRange = true;
        return;
    }

    if (io.KeyCtrl) {
        state.ToggleSelection(id);
        return;
    }

    // THE PLAIN CLICK, and the one case that needs care: clicking a row that
    // is already part of a multi-selection must NOT collapse the selection
    // to that one row, because the usual next move is to drag the whole set
    // somewhere. It only becomes active. Letting go without dragging is what
    // narrows it down - but ImGui has already told us this was a click
    // rather than a drag by the time we are here, so the narrowing happens
    // immediately and the drag case is handled by the drag source, which
    // carries the whole selection.
    state.SelectOnly(id);
}

void HierarchyPanel::ApplyPendingRange() {
    if (!m_hasPendingRange) {
        return;
    }
    const eng::EntityId target = m_pendingRangeTo;
    m_pendingRangeTo           = eng::EntityId{};
    m_hasPendingRange          = false;

    EditorState& state = EditorState::Get();

    // Both ends, as POSITIONS IN THE DISPLAYED ORDER. "Everything between"
    // means everything between them on screen, which is not the same as
    // anything about the scene's own ordering or the parent tree - a range
    // can start in one branch and finish in another.
    std::size_t from = m_rows.size();
    std::size_t to   = m_rows.size();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].id == state.selected) {
            from = i;
        }
        if (m_rows[i].id == target) {
            to = i;
        }
    }
    if (from == m_rows.size() || to == m_rows.size()) {
        // One end is not on screen - a collapsed branch, or the filter is
        // hiding it. Falling back to selecting just the clicked row is what
        // every list does, and it is better than doing nothing.
        state.SelectOnly(target);
        return;
    }

    if (from > to) {
        std::swap(from, to);
    }

    // The anchor stays where it was, so Shift-clicking again from the same
    // place grows or shrinks the range rather than walking it along.
    const eng::EntityId anchor = state.selected;
    state.ClearSelection();
    for (std::size_t i = from; i <= to; ++i) {
        state.AddToSelection(m_rows[i].id);
    }
    state.selected = anchor;
}

void HierarchyPanel::DrawMarquee() {
    EditorState& state  = EditorState::Get();
    ImGuiIO&     io     = ImGui::GetIO();
    ImDrawList*  draw   = ImGui::GetWindowDrawList();

    // ---- starting one ------------------------------------------------------
    //
    // Only from EMPTY space. Pressing on a row already means something, and
    // a box that started from a row would fight the reparent drag for the
    // same gesture.
    if (!m_marqueeActive && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

        m_marqueeActive = true;
        m_marqueeStart  = io.MousePos;
        m_marqueeAdds   = io.KeyCtrl;

        // Remembered so the box can be reversible. Without this, sweeping
        // over a row and back off it would leave it selected, and a box you
        // cannot take back is a box you have to undo by hand.
        m_marqueeBaseline = state.selection;
    }

    if (!m_marqueeActive) {
        return;
    }

    // ---- finishing -------------------------------------------------------
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_marqueeActive = false;
        m_marqueeBaseline.clear();
        return;
    }

    // ImRect wants its corners the right way round, and a box can be dragged
    // in any direction - up and to the left is not unusual.
    const ImVec2 a = m_marqueeStart;
    const ImVec2 b = io.MousePos;
    const ImRect box(ImVec2(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y),
                     ImVec2(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y));

    // RECOMPUTED FROM THE BASELINE EVERY FRAME rather than added to as the
    // box grows. That is what makes shrinking the box give the rows back.
    state.selection = m_marqueeAdds ? m_marqueeBaseline : std::vector<eng::EntityId>{};
    if (!m_marqueeAdds) {
        state.selected = eng::EntityId{};
    }

    for (const Row& row : m_rows) {
        if (box.Overlaps(row.rect)) {
            state.AddToSelection(row.id);
        }
    }

    // Drawn last so it sits over the rows. A fill as well as an outline,
    // because an outline alone over a list of highlighted rows is hard to
    // pick out.
    draw->AddRectFilled(box.Min, box.Max, IM_COL32(120, 190, 250, 40));
    draw->AddRect(box.Min, box.Max, IM_COL32(120, 190, 250, 200));
}

void HierarchyPanel::DrawEntityDragSource(eng::Entity& entity) {
    if (!ImGui::BeginDragDropSource()) {
        return;
    }

    // THE ID IS COPIED INTO THE PAYLOAD, not a pointer to it. ImGui keeps
    // this handful of bytes for the whole drag and hands them back at the
    // drop, so nothing has to stay alive in the meantime - and an EntityId
    // that refers to something destroyed mid-drag is still safe to look up
    // and simply comes back as nothing.
    // DRAGGING A SELECTED ROW DRAGS THE WHOLE SELECTION, which is what makes
    // bulk reparenting possible at all. Dragging an UNSELECTED row selects
    // it first and drags only it - picking something up is a way of choosing
    // it, and carrying off a set you are not pointing at would be a surprise.
    EditorState& state = EditorState::Get();
    if (!state.IsSelected(entity.Id())) {
        state.SelectOnly(entity.Id());
    }

    // The payload still carries ONE id - the row actually grabbed. The rest
    // of the set is read from the selection at the drop. Putting a whole
    // list in the payload would mean a second copy of it that could disagree
    // with the real one.
    const eng::EntityId id = entity.Id();
    ImGui::SetDragDropPayload(kPayloadEntity, &id, sizeof(id));

    // What rides under the cursor. Without this the drag is invisible and
    // feels broken even when it works.
    if (state.selection.size() > 1) {
        ImGui::Text("%zu entities", state.selection.size());
    } else {
        ImGui::Text("%s", entity.Name().c_str());
    }

    ImGui::EndDragDropSource();
}

bool HierarchyPanel::ReparentIsLegal(eng::EntityId child, eng::EntityId parent) const {
    eng::Scene&  scene       = eng::Engine::Get().GetScene();
    eng::Entity* childEntity = scene.Get(child);
    if (childEntity == nullptr) {
        return false;
    }

    // The scene root. Always allowed - anything can stop having a parent.
    if (parent.IsNull()) {
        return true;
    }

    // Onto itself. Checked separately from the descendant test below only
    // because it reads clearly; Transform2D refuses both.
    if (child == parent) {
        return false;
    }

    eng::Entity* parentEntity = scene.Get(parent);
    if (parentEntity == nullptr) {
        return false;
    }

    // THE ONE THAT MATTERS: dropping something onto its own descendant would
    // make a loop in the parent chain, and asking for a world position walks
    // that chain - so it would hang rather than misbehave visibly. Refused
    // here so the target never lights up; Transform2D::SetParent refuses it
    // again, because a rule this consequential is worth enforcing where it
    // cannot be bypassed.
    return !parentEntity->Transform().IsDescendantOf(&childEntity->Transform());
}

bool HierarchyPanel::SelectionReparentIsLegal(eng::EntityId parent) const {
    const EditorState& state = EditorState::Get();

    // A drag carries the whole selection, so the target has to be legal for
    // EVERY member of it. Allowing the drop because the grabbed row happens
    // to be fine would quietly skip the others at the drop, which looks like
    // the feature failing at random.
    if (state.selection.empty()) {
        return false;
    }
    for (const eng::EntityId id : state.selection) {
        if (!ReparentIsLegal(id, parent)) {
            return false;
        }
    }
    return true;
}

void HierarchyPanel::DrawEntityDropTarget(eng::EntityId newParent) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }

    // LOOKED AT BEFORE IT IS ACCEPTED. GetDragDropPayload just reports what
    // is being dragged without claiming it, so an illegal target can decline
    // to light up at all rather than highlighting and then refusing on
    // release. A target that accepts and then does nothing is the worse of
    // the two: it looks like the feature is broken.
    if (const ImGuiPayload* peek = ImGui::GetDragDropPayload();
        peek != nullptr && peek->IsDataType(kPayloadEntity) &&
        peek->DataSize == static_cast<int>(sizeof(eng::EntityId))) {

        eng::EntityId dragged{};
        std::memcpy(&dragged, peek->Data, sizeof(dragged));

        if (SelectionReparentIsLegal(newParent)) {
            if (ImGui::AcceptDragDropPayload(kPayloadEntity) != nullptr) {
                RequestReparent(dragged, newParent);
            }
        }
    }

    ImGui::EndDragDropTarget();
}

void HierarchyPanel::DrawRootDropZone() {
    // ONLY EXISTS WHILE AN ENTITY IS BEING DRAGGED.
    //
    // An invisible button sitting over the empty part of the panel the rest
    // of the time would swallow the right-click that opens the New Object
    // menu: that menu is asked for with NoOpenOverItems, and this would be an
    // item covering everything it needs empty space for.
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (peek == nullptr || !peek->IsDataType(kPayloadEntity) ||
        peek->DataSize != static_cast<int>(sizeof(eng::EntityId))) {
        return;
    }

    eng::EntityId dragged{};
    std::memcpy(&dragged, peek->Data, sizeof(dragged));

    // Nothing to offer for something that is already at the root - saying
    // "drop here to remove the parent" to an entity with no parent is noise.
    eng::Entity* draggedEntity = eng::Engine::Get().GetScene().Get(dragged);
    if (draggedEntity == nullptr || draggedEntity->Transform().Parent() == nullptr) {
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float  height    = (available.y > 56.0f) ? available.y : 56.0f;
    const ImVec2 topLeft   = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##scene-root-drop", ImVec2(available.x, height));
    const bool hovered     = ImGui::IsItemHovered();
    const ImVec2 bottomRight = ImGui::GetItemRectMax();

    // Drawn by hand rather than as a button, because this is a target rather
    // than something to press: an outline that brightens when the drag is
    // over it says "let go here" without looking clickable the rest of the
    // time.
    ImDrawList*    draw   = ImGui::GetWindowDrawList();
    const ImU32    colour = hovered ? IM_COL32(120, 190, 250, 255)
                                    : IM_COL32(120, 190, 250, 110);
    draw->AddRect(topLeft, bottomRight, colour, 4.0f, 0, hovered ? 2.0f : 1.0f);

    const char*  text     = "drop here to remove the parent";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    draw->AddText(ImVec2(topLeft.x + (available.x - textSize.x) * 0.5f,
                         topLeft.y + (height - textSize.y) * 0.5f),
                  colour, text);

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload(kPayloadEntity) != nullptr) {
            RequestReparent(dragged, eng::EntityId{});   // null: the scene root
        }
        ImGui::EndDragDropTarget();
    }
}

void HierarchyPanel::RequestReparent(eng::EntityId child, eng::EntityId parent) {
    m_pendingReparent.child     = child;
    m_pendingReparent.parent    = parent;
    m_pendingReparent.requested = true;
}

void HierarchyPanel::ApplyPendingReparent() {
    if (!m_pendingReparent.requested) {
        return;
    }

    // Taken and CLEARED FIRST, so that an early return below cannot leave a
    // request that tries again on every frame for the rest of the session.
    const PendingReparent request = m_pendingReparent;
    m_pendingReparent             = {};

    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::Transform2D* parentTransform = nullptr;
    if (!request.parent.IsNull()) {
        eng::Entity* parent = scene.Get(request.parent);
        if (parent == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "could not reparent: what it was dropped on no longer "
                             "exists");
            return;
        }
        parentTransform = &parent->Transform();
    }

    // EVERYTHING SELECTED MOVES, not just the row that was grabbed. The
    // grabbed row is used only when it is somehow not in the selection,
    // which the drag source makes sure cannot normally happen.
    EditorState&               state = EditorState::Get();
    std::vector<eng::EntityId> moving =
        state.IsSelected(request.child) ? state.selection
                                        : std::vector<eng::EntityId>{request.child};

    int moved = 0;
    for (const eng::EntityId id : moving) {
        eng::Entity* child = scene.Get(id);
        if (child == nullptr) {
            continue;   // destroyed between the drop and here
        }

        // SKIP ANYTHING WHOSE ANCESTOR IS ALSO MOVING.
        //
        // Select a parent and its child, drag both onto somewhere new, and
        // the child must stay where it is in the tree - under its parent,
        // which is itself moving. Reparenting it separately would tear the
        // branch apart and flatten it, which is the opposite of what
        // dragging a branch somewhere means.
        bool ancestorMoving = false;
        for (const eng::EntityId other : moving) {
            if (other == id) {
                continue;
            }
            eng::Entity* otherEntity = scene.Get(other);
            if (otherEntity != nullptr &&
                child->Transform().IsDescendantOf(&otherEntity->Transform())) {
                ancestorMoving = true;
                break;
            }
        }
        if (ancestorMoving) {
            continue;
        }

        if (child->Transform().Parent() == parentTransform) {
            continue;   // dropped back where it already was
        }

        // keepWorldTransform: TRUE. The entity stays exactly where it looks
        // and its local numbers are recomputed to suit the new parent.
        // Anything else makes things leap across the scene when you organise
        // the tree, which is the opposite of what tidying up should do.
        child->Transform().SetParent(parentTransform, true);
        ++moved;
    }

    if (moved == 0) {
        return;
    }
    state.MarkDirty();

    if (parentTransform == nullptr) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "{} entit{} now have no parent", moved,
                        moved == 1 ? "y" : "ies");
    } else {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "{} entit{} now a child of '{}'", moved,
                        moved == 1 ? "y is" : "ies are",
                        scene.Get(request.parent)->Name());
    }
}

void HierarchyPanel::DrawContextMenu(eng::Entity& entity) {
    // BeginPopupContextItem opens a menu when the previous item is
    // right-clicked, and returns false on every frame it is not open.
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }
    eng::Scene& scene = eng::Engine::Get().GetScene();

    // The same items as the menu bar's Objects menu, from the one list. See
    // ObjectMenu.h.
    if (ImGui::BeginMenu("New Object")) {
        if (const eng::EntityId created = DrawObjectMenuItems(); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().MarkDirty();
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Rename...")) {
        m_renameTarget    = entity.Id();
        m_openRenamePopup = true;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", entity.Name().c_str());
    }

    // THE KEYBOARD-AND-MENU WAY OUT OF A PARENT, next to Rename because it
    // is the same kind of edit. Dragging onto the empty space below the tree
    // does the same thing, but a drag is a poor way to discover that the
    // option exists at all - and it is no use when the tree fills the panel
    // and there is no empty space left to drop into.
    //
    // Greyed out rather than hidden when there is no parent to remove: an
    // item that vanishes teaches nothing, one that is visibly unavailable
    // says what it would do and why it cannot right now.
    const bool hasParent = entity.Transform().Parent() != nullptr;
    if (ImGui::MenuItem("Unparent", nullptr, false, hasParent)) {
        // Acts on the selection, like the drag does - RequestReparent is
        // given the clicked row and ApplyPendingReparent widens it.
        RequestReparent(entity.Id(), eng::EntityId{});
    }
    if (!hasParent && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This entity is already at the top level of the scene.");
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Duplicate")) {
        std::string        error;
        const eng::EntityId copy = scene.DuplicateEntity(entity.Id(), error);
        if (copy.IsNull()) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not duplicate: {}", error);
        } else {
            // Select the copy, because the next thing anybody does after
            // duplicating something is move it.
            EditorState::Get().SelectOnly(copy);
            EditorState::Get().MarkDirty();
        }
    }

    ImGui::Separator();

    // Says how many, so a right-click on one row of eight cannot destroy the
    // other seven without warning.
    EditorState&      state    = EditorState::Get();
    const bool        bulk     = state.IsSelected(entity.Id()) && state.selection.size() > 1;
    char              destroyLabel[64];
    std::snprintf(destroyLabel, sizeof(destroyLabel), bulk ? "Destroy %zu entities"
                                                           : "Destroy",
                  state.selection.size());

    if (ImGui::MenuItem(destroyLabel)) {
        // Through the DEFERRED QUEUE, not straight away. Deleting an entity
        // from inside a panel, mid-frame, is exactly the problem DeferredOps
        // exists to prevent - and the editor does not get an exemption from
        // the engine's own rules. The entity disappears at the end of the tick.
        // Both through the shared doors, which is what marks the scene for
        // undo - see DestroySelection and DestroyEntity in EditorApp.h.
        if (bulk) {
            DestroySelection();
        } else {
            DestroyEntity(entity.Id());
        }
    }
    ImGui::EndPopup();
}

void HierarchyPanel::DrawRenamePopup() {
    if (m_openRenamePopup) {
        ImGui::OpenPopup("Rename Entity");
        m_openRenamePopup = false;
    }

    // Centred, because a dialog that opens under the mouse in a docked editor
    // is as likely to be half off the screen as not.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Rename Entity", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* target = scene.Get(m_renameTarget);

    if (target == nullptr) {
        // Destroyed while the dialog was open. Noticed, not crashed on - which
        // is the whole reason the dialog remembers an id.
        ImGui::TextUnformatted("that entity no longer exists");
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // Names are how the scene looks entities up, so two entities cannot share
    // one. Refused up front rather than after pressing the button.
    const bool taken = !scene.Find(m_renameBuffer).IsNull() &&
                       scene.Find(m_renameBuffer) != m_renameTarget;
    if (taken) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                           "something else is already called that");
    }

    ImGui::BeginDisabled(taken || m_renameBuffer[0] == '\0');
    if ((submitted && !taken) || ImGui::Button("Rename", ImVec2(120, 0))) {
        if (scene.RenameEntity(m_renameTarget, m_renameBuffer)) {
            EditorState::Get().MarkDirty();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void HierarchyPanel::Draw() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    // Rebuilt from nothing each frame - see HierarchyPanel::Row.
    m_rows.clear();

    // Routed through the same CreateObject every menu uses, so "an empty
    // entity" means one thing in one place. See ObjectMenu.h.
    if (ImGui::Button("+ Create Entity")) {
        if (const eng::EntityId created = CreateObject("Empty"); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().MarkDirty();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Object")) {
        ImGui::OpenPopup("newobject");
    }
    if (ImGui::BeginPopup("newobject")) {
        if (const eng::EntityId created = DrawObjectMenuItems(); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().MarkDirty();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(appears where the camera is looking)");

    ImGui::SetNextItemWidth(-1.0f);   // -1 means "use all the remaining width"
    ImGui::InputTextWithHint("##filter", "filter by name...", m_filter, sizeof(m_filter));

    ImGui::TextDisabled("%zu entities", scene.EntityCount());
    ImGui::Separator();

    const bool filtering = m_filter[0] != '\0';

    if (filtering) {
        // A flat list while filtering. A tree with most of its branches hidden
        // is harder to read than a list, and the point of a filter is to find
        // one thing.
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Name().find(m_filter) == std::string::npos) {
                return;
            }
            char label[192];
            std::snprintf(label, sizeof(label), "%s##%d_%d", entity.Name().c_str(),
                          entity.Id().index, entity.Id().generation);
            if (ImGui::Selectable(label, EditorState::Get().IsSelected(entity.Id()))) {
                HandleRowClick(entity.Id());
            }
            // Recorded here too, so Shift and the marquee work the same way
            // in a filtered list as they do in the tree.
            m_rows.push_back(Row{entity.Id(), ImRect(ImGui::GetItemRectMin(),
                                                     ImGui::GetItemRectMax())});
            DrawContextMenu(entity);
        });
    } else {
        // Only the entities with no parent are drawn here; DrawNode draws each
        // one's children itself.
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Transform().Parent() == nullptr) {
                DrawNode(entity);
            }
        });
    }

    // Outline whatever is selected, in the Scene view.
    //
    // Looked up from the ID every frame, which is the rule. A selection that
    // was destroyed simply comes back as nothing and no outline is drawn.
    // EVERY selected entity gets an outline. The ACTIVE one gets its name as
    // well, and a brighter colour - with eight things outlined in the same
    // yellow there would be no way to tell which one the move gizmo belongs
    // to, and eight name labels on top of each other would be unreadable.
    for (const eng::EntityId id : EditorState::Get().selection) {
        eng::Entity* entity = scene.Get(id);
        if (entity == nullptr) {
            continue;
        }
        const bool      active   = (id == EditorState::Get().selected);
        const eng::Vec2 position = entity->Transform().WorldPosition();

        eng::Gizmos::Box(
            eng::AABB::FromCenterHalfExtents(position, eng::Vec2{22.0f, 22.0f}),
            active ? eng::Color::Yellow() : eng::Color::Cyan(), 0.0f,
            eng::GizmoSpace::World, eng::GizmoCategory::Bounds);

        if (!active) {
            continue;
        }

        // ABOVE the outline rather than beside it. Beside it is where the
        // Scene view puts its move handle, and a name label sitting on top of
        // the handle you are trying to grab makes a tool feel broken.
        eng::Gizmos::Text(position + eng::Vec2{-20.0f, 34.0f}, entity->Name(),
                          eng::Color::Yellow(), 0.0f, eng::GizmoSpace::World,
                          eng::GizmoCategory::Bounds);
    }

    // The empty space below the tree, which becomes "give this no parent"
    // while an entity is being dragged and is nothing at all otherwise.
    DrawRootDropZone();

    // RIGHT-CLICKING EMPTY SPACE offers the same menu, which is where people
    // reach for it when the scene is empty and there is no row to click on.
    if (ImGui::BeginPopupContextWindow("hierarchy-background",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
        if (const eng::EntityId created = DrawObjectMenuItems(); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().MarkDirty();
        }
        ImGui::EndPopup();
    }

    // ---- the keys this panel owns ----------------------------------------
    //
    // Both need the panel FOCUSED, not merely hovered, and both need
    // WantTextInput to be false. Delete while renaming an entity would
    // otherwise delete the entity instead of a character.
    //
    // Destroying goes through the DEFERRED QUEUE, like every other delete in
    // the editor: this runs while panels are still drawing and still holding
    // on to what they are describing. See scene/DeferredOps.h.
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !io.WantTextInput) {

        // DELETE ACTS ON THE WHOLE SELECTION, through the one shared
        // implementation - see DestroySelection in EditorApp.h. This used to
        // be written out here and again in the Scene view, and the two had
        // already stopped matching.
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            DestroySelection();
        }

        if (eng::Entity* selected = scene.Get(EditorState::Get().selected)) {
            // Rename stays SINGULAR. Renaming eight entities to one name is
            // not a thing anybody wants, and the scene requires names to be
            // unique anyway - it would fail for seven of them.
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                m_renameTarget    = selected->Id();
                m_openRenamePopup = true;
                std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s",
                              selected->Name().c_str());
            }
        }
    }

    DrawRenamePopup();

    // The box, and then the Shift-range - both AFTER the rows, because both
    // need m_rows to be complete. The marquee draws over the rows as well,
    // which is where it belongs.
    DrawMarquee();
    ApplyPendingRange();

    // LAST, when the tree is finished and no node is still open. Changing
    // who is whose child rewrites the lists the tree above was walking, so
    // it waits until there is nothing left walking them. See HierarchyPanel.h.
    ApplyPendingReparent();
}

} // namespace editor
