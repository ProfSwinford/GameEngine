#pragma once

// ============================================================================
//  HierarchyPanel.h - the Hierarchy: every entity in the scene, as a tree.
//
//  This is the same panel Unity puts on the left. Parent/child relationships
//  show as nesting, clicking selects, and the selection is what the Inspector
//  displays.
//
//  What it can do:
//    * click to select
//    * right-click for Rename, Duplicate, Unparent and Destroy
//    * "+ Create Entity" for a new empty one
//    * a filter box for finding something in a big scene
//    * drag a texture or a script onto a row to attach it
//    * drag one ENTITY onto another to make it a child of it, and drag it
//      out onto the empty space below the tree to give it no parent again
//    * drag an entity onto the ASSETS panel to make a prefab of it
//    * right-click a prefab instance for Apply, Revert, Unpack and
//      "Select Prefab Asset"
//
//  ==========================================================================
//  PREFAB INSTANCES ARE DRAWN IN BLUE, every member of them, which is the one
//  signal that tells you an entity is not yours alone to change - editing it
//  is editing one copy of something, and Apply pushes it to all of them.
//
//  Two gestures are REFUSED on a member that is not the root: destroying it
//  and reparenting it. Both are refused because they would not work rather
//  than to be protective - the asset says that child exists and says where it
//  sits, so the next rebuild puts it straight back. See IsPrefabChild in
//  EditorApp.h, which both this panel and the destroy doors ask.
//
//  ==========================================================================
//  REPARENTING IS RECORDED WHILE DRAWING AND APPLIED AFTERWARDS.
//
//  Never in the middle of the tree. DrawNode sits between TreeNodeEx and
//  TreePop while it draws a row's children, and it is walking the parent's
//  list of children to do it. Changing who is whose child at that moment
//  moves entries in the very vector being walked, and rewrites the shape of
//  a tree ImGui is half-way through laying out.
//
//  So a drop writes down what was asked for and nothing else. Draw() carries
//  it out at the end, when the tree is finished and no node is open. This is
//  the same reason Destroy goes through DeferredOps rather than deleting the
//  entity where it stands.
//
//  The selected entity is outlined in yellow in the Scene view, which is one
//  extra call and is what makes selection feel real rather than like a row in
//  a list.
//
//  ==========================================================================
//  ONE RULE: THIS PANEL REMEMBERS AN EntityId, NEVER AN Entity*.
//
//  A panel that cached a pointer would crash the first time an entity was
//  destroyed while it was still selected - which happens constantly, because
//  Destroy is a menu item right there. The selection is an EntityId held in
//  EditorState, and it is looked up fresh every frame; an id belonging to
//  something that no longer exists simply comes back as nothing.
//
//  That is exactly the argument EntityId.h makes, arriving from the tools
//  side. It is also why real engines adopt ids the moment they grow an editor.
// ============================================================================

#include "Panel.h"

#include <engine/scene/Entity.h>

#include <imgui.h>
#include <imgui_internal.h>   // ImRect, for remembering where each row is

#include <vector>

namespace editor {

// The id ImGui matches a dragged ENTITY on, as opposed to a dragged file -
// those live in AssetDragDrop.h. Defined once and used by both ends for the
// reason given there: a typo in a payload id fails silently, and a drop that
// is quietly never accepted gives you nothing to debug.
inline constexpr const char* kPayloadEntity = "HIERARCHY_ENTITY";

class HierarchyPanel final : public Panel {
public:
    const char* Title() const override { return "Hierarchy"; }
    void        Draw() override;

private:
    void DrawNode(eng::Entity& entity);
    void DrawContextMenu(eng::Entity& entity);

    // The prefab items in that menu, when the entity is part of an instance -
    // and "Create Prefab" when it is not. Split out because the context menu
    // was already long enough to read badly with another branch in it.
    void DrawPrefabMenuItems(eng::Entity& entity);

    void DrawRenamePopup();

    // Makes the row just drawn draggable, and lets an entity be dropped onto
    // it. Both are called straight after the row, because ImGui attaches
    // drag sources and drop targets to the most recent item.
    void DrawEntityDragSource(eng::Entity& entity);
    void DrawEntityDropTarget(eng::EntityId newParent);

    // ---- selecting ---------------------------------------------------------
    //
    // Three gestures, the ones every list in every tool uses:
    //
    //   click         this one, and nothing else
    //   Ctrl+click    add or remove this one, leaving the rest alone
    //   Shift+click   everything from the active entity to this one
    //   drag a box    everything the box touches
    //
    // The first three are decided here, per row. The box is further down.
    void HandleRowClick(eng::EntityId id);

    // Every row drawn this frame, top to bottom, with where it is on screen.
    //
    // REBUILT EVERY FRAME, and it has to be: the order depends on which
    // branches are open, and a row's position moves the moment anything
    // above it is expanded. A Shift-range means "between these two ROWS AS
    // DISPLAYED", so there is nowhere else that answer could come from.
    struct Row {
        eng::EntityId id;
        ImRect        rect;
    };
    std::vector<Row> m_rows;

    // A Shift-click cannot be resolved where it happens. The other end of
    // the range may be a row that has not been drawn yet this frame, so the
    // range is worked out at the END of Draw, when m_rows is complete.
    eng::EntityId m_pendingRangeTo{};
    bool          m_hasPendingRange = false;
    void          ApplyPendingRange();

    // ======================================================================
    //  WHICH ROW THE LEFT BUTTON WENT DOWN ON, and whether it landed on that
    //  row's expand arrow.
    //
    //  A ROW IS SELECTED ON THE RELEASE, not on the press, so that picking
    //  one up to drag it does not change what the Inspector is describing -
    //  see the long note in DrawNode. Deciding that on the release needs two
    //  things the release itself cannot answer: whether the press started on
    //  this row rather than another, and whether it was on the arrow, which
    //  is the one part of the row ImGui presses on mouse-down.
    //
    //  CLEARED AT THE END OF EVERY FRAME THE BUTTON COMES UP, after the rows
    //  have been drawn - so a press that ends anywhere else leaves nothing
    //  behind to fire on the next one.
    // ======================================================================
    eng::EntityId m_pressedRow{};
    bool          m_pressWasToggle = false;

    // ---- the marquee (drag a box round some rows) --------------------------
    //
    // Started by pressing in EMPTY space rather than on a row, because
    // pressing on a row already means "select it" and, if you move, "drag it
    // somewhere else".
    void DrawMarquee();

    bool   m_marqueeActive = false;
    ImVec2 m_marqueeStart{};

    // What was selected when the box started, so that dragging it back over
    // a row un-selects that row again rather than leaving it stuck on.
    std::vector<eng::EntityId> m_marqueeBaseline;
    bool                       m_marqueeAdds = false;   // Ctrl held at the start

    // The empty space under the tree, which only becomes a drop target while
    // an entity is actually being dragged. Dropping there means "no parent".
    void DrawRootDropZone();

    // Whether making `child` a child of `parent` is allowed, asked BEFORE the
    // drop is accepted so an impossible target never lights up. A null
    // `parent` means the scene root, which is always allowed.
    bool ReparentIsLegal(eng::EntityId child, eng::EntityId parent) const;

    // ======================================================================
    //  EVERYTHING A DRAG THAT GRABBED `grabbed` WILL MOVE.
    //
    //  The whole selection when the grabbed row is part of it - that is what
    //  makes bulk reparenting possible - and that row ALONE when it is not.
    //
    //  THE SECOND HALF USED TO BE UNREACHABLE and now is the common case.
    //  Starting a drag used to select the grabbed row first, so the selection
    //  always contained it; it no longer does, because selecting on the press
    //  that begins a drag swapped the Inspector out from under the drop. See
    //  DrawEntityDragSource.
    //
    //  ONE ANSWER, asked by the drop target when it decides whether to light
    //  up and by ApplyPendingReparent when it carries the move out. Two
    //  copies of this rule is how a target comes to accept a drop and then
    //  skip half of it.
    // ======================================================================
    std::vector<eng::EntityId> DragSet(eng::EntityId grabbed) const;

    // The same question for everything a drag carries. Legal only when it is
    // legal for every member - allowing the drop because the grabbed row
    // happens to be fine would quietly skip the others at the drop, which
    // looks like the feature failing at random.
    bool DragReparentIsLegal(eng::EntityId grabbed, eng::EntityId parent) const;

    void RequestReparent(eng::EntityId child, eng::EntityId parent);
    void ApplyPendingReparent();

    // What a drop asked for, carried out at the end of Draw. Ids rather than
    // pointers, like everything else here: either end can be destroyed
    // between the drop and the moment this is acted on.
    //
    // A NULL parent is not "nothing to do" - it means "give this entity no
    // parent at all", which is how unparenting is expressed. `requested` is
    // what tells the two apart.
    struct PendingReparent {
        eng::EntityId child{};
        eng::EntityId parent{};
        bool          requested = false;
    };
    PendingReparent m_pendingReparent{};

    char m_filter[96] = {};

    // The rename dialog targets an ID, not a pointer or a row number: the
    // entity can be destroyed between opening the dialog and pressing OK.
    eng::EntityId m_renameTarget{};
    char          m_renameBuffer[128] = {};
    bool          m_openRenamePopup   = false;
};

} // namespace editor
