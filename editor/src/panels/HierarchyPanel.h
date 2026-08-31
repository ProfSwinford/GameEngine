#pragma once
// =============================================================================
//  WEEK 9 PANELS - the scene hierarchy and the resource browser. The week the
//  Unity comparison becomes literal: a tree of entities on the left, select
//  one and see what it is made of. Every engine editor converges on this
//  shape, because it is the natural view of an entity/component model.
//
//  HIERARCHY:
//    - a tree reflecting the Week 6 transform parenting
//    - click to select; the selection is shared with the Inspector
//    - the selected entity's bounds highlighted through DebugDraw - one call,
//      and it makes selection feel real
//    - a text filter over entity names
//
//  RESOURCE BROWSER:
//    - every loaded resource: virtual path, refcount, bytes, load state
//    - a preview thumbnail for textures
//    - TOTAL REFCOUNT, prominently
//
//  ---------------------------------------------------------------------------
//  THE RESOURCE PANEL IS THE MILESTONE 3 VERIFICATION, MADE LIVE. Load the
//  scene, watch checker_red.bmp sit at refcount 18, hit File > Unload Scene,
//  watch the list empty and the total hit zero. Better than a printed number,
//  because when it does NOT reach zero the panel says immediately which
//  resource is stuck and what its count is.
//
//  ---------------------------------------------------------------------------
//  *** ONE RULE: THE HIERARCHY HOLDS A HANDLE, NEVER A POINTER. ***
//
//  A panel caching Entity* crashes the moment Week 10 destroys an entity
//  mid-frame while it is still selected. The selection is an EntityHandle in
//  EditorState and is resolved every frame; a stale handle is detected and
//  reported rather than dereferenced.
//
//  That is exactly the argument Handle.h makes, arriving from the tools side -
//  and the IDE is the first thing to need it, which is why real engines adopt
//  handles the moment they grow an editor.
// =============================================================================
#include "Panel.h"

#include <engine/resource/ResourceManager.h>
#include <engine/scene/Entity.h>

#include <vector>

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

    // Rename targets a HANDLE, not a pointer or an index - the entity can be
    // destroyed between opening the popup and confirming it, and a stale handle
    // is detected where a pointer would crash. Same rule as the selection.
    eng::EntityHandle m_renameTarget{};
    char              m_renameBuffer[128] = {};
    bool              m_openRenamePopup   = false;
};

class ResourcePanel final : public Panel {
public:
    const char* Title() const override { return "Resources"; }
    void        Draw() override;

private:
    std::vector<eng::ResourceManager::Entry> m_entries;
};

} // namespace editor
