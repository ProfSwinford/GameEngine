#pragma once

// ============================================================================
//  InspectorPanel.h - the Inspector and the Toolbar.
//
//  INSPECTOR: every component on whichever entity is selected in the
//  Hierarchy, with its fields editable and taking effect immediately - the
//  transform's position, rotation and scale; the sprite's image and colour;
//  the collider's size and layers; a script's binding and its own variables.
//  Plus an "Add Component" row at the bottom. Same panel Unity puts on the
//  right.
//
//  TOOLBAR: Play, Pause and Step, plus a time-scale slider and a readout of
//  how far the simulation has got.
//
//  ==========================================================================
//  PAUSE + STEP + INSPECTOR IS THE BEST DEBUGGING TOOL IN THE EDITOR.
//
//  Pause the game, press Step once, and watch a single entity's position
//  change by exactly one simulation step in the Inspector while its collider
//  is outlined in the Scene view. There is no faster way to find out why a
//  collision is not firing.
//
//  ==========================================================================
//  EDITING IS WHERE THIS GETS GENUINELY AWKWARD. Three answers:
//
//  1. EDITING WHILE PAUSED is safe, and is where most editing should happen.
//     Nothing is running to overwrite what you type.
//
//  2. EDITING WHILE RUNNING may be undone on the very next tick by whichever
//     system owns that value. Confusing rather than dangerous. This panel
//     shows system-owned fields as READ-ONLY while the game runs, and says so,
//     rather than letting somebody drag a position that a movement system puts
//     straight back.
//
//     A SCRIPT'S OWN VARIABLES ARE THE EXCEPTION, and they stay editable
//     while the game runs on purpose. Nothing else owns them - the script
//     reads them and nobody writes them - so dragging one is not a value
//     being fought over, it is the only honest way to find out what a jump
//     height should be. See engine/scene/ScriptFields.h.
//
//  3. DESTROYING FROM THE INSPECTOR goes through the deferred queue, exactly
//     like everything else. Deleting an entity from a panel in the middle of a
//     frame is precisely the problem DeferredOps exists to prevent, and the
//     editor does not get an exemption from the engine's rules.
//
//  ==========================================================================
//  REMOVING A COMPONENT: the x on its header, or right-click the header.
//
//  Every component can be removed except the TRANSFORM, which every entity
//  has by definition - Entity::Transform() builds one on demand if it is
//  missing, so "removing" it would silently throw away the position and hand
//  back a fresh one at the origin, taking any children with it. The x is left
//  off that one header and the reason is offered as a tooltip.
//
//  Like the reparenting in the Hierarchy, the removal is RECORDED while
//  drawing and carried out at the end of Draw. The panel is part-way through
//  walking the component list at the point the x is clicked, and erasing from
//  that list shifts everything after it down - which would skip a component
//  at best. See the note on m_pendingRemoval.
// ============================================================================

#include "Panel.h"

#include <engine/scene/Entity.h>

#include <cstddef>

namespace editor {

class InspectorPanel final : public Panel {
public:
    const char* Title() const override { return "Inspector"; }
    void        Draw() override;

private:
    void ApplyPendingRemoval();

    // Which component the x was clicked on, acted on once the list is no
    // longer being walked.
    //
    // AN INDEX, paired with the id of the entity it belongs to. The index
    // alone would be meaningless if the selection changed in between, and
    // the pair is checked against the live entity before anything is removed
    // - an index into a list that has since changed is exactly the kind of
    // stale handle this editor refuses to keep.
    struct PendingRemoval {
        eng::EntityId entity{};
        std::size_t   index     = 0;
        bool          requested = false;
    };
    PendingRemoval m_pendingRemoval{};
};

class ToolbarPanel final : public Panel {
public:
    const char* Title() const override { return "Toolbar"; }
    void        Draw() override;
};

} // namespace editor
