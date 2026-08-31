#pragma once

// ============================================================================
//  InspectorPanel.h - the Inspector and the Toolbar.
//
//  INSPECTOR: every component on whichever entity is selected in the
//  Hierarchy, with its fields editable and taking effect immediately - the
//  transform's position, rotation and scale; the sprite's image and colour;
//  the collider's size and layers; a script's binding. Plus an "Add Component"
//  row at the bottom. Same panel Unity puts on the right.
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
//  3. DESTROYING FROM THE INSPECTOR goes through the deferred queue, exactly
//     like everything else. Deleting an entity from a panel in the middle of a
//     frame is precisely the problem DeferredOps exists to prevent, and the
//     editor does not get an exemption from the engine's rules.
// ============================================================================

#include "Panel.h"

namespace editor {

class InspectorPanel final : public Panel {
public:
    const char* Title() const override { return "Inspector"; }
    void        Draw() override;
};

class ToolbarPanel final : public Panel {
public:
    const char* Title() const override { return "Toolbar"; }
    void        Draw() override;
};

} // namespace editor
