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
//    * right-click for Rename, Duplicate and Destroy
//    * "+ Create Entity" for a new empty one
//    * a filter box for finding something in a big scene
//    * drag a texture or a script onto a row to attach it
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

namespace editor {

class HierarchyPanel final : public Panel {
public:
    const char* Title() const override { return "Hierarchy"; }
    void        Draw() override;

private:
    void DrawNode(eng::Entity& entity);
    void DrawContextMenu(eng::Entity& entity);
    void DrawRenamePopup();

    char m_filter[96] = {};

    // The rename dialog targets an ID, not a pointer or a row number: the
    // entity can be destroyed between opening the dialog and pressing OK.
    eng::EntityId m_renameTarget{};
    char          m_renameBuffer[128] = {};
    bool          m_openRenamePopup   = false;
};

} // namespace editor
