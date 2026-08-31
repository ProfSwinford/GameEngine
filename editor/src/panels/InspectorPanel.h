#pragma once
// =============================================================================
//  WEEK 10 PANELS - inspector and toolbar. The IDE becomes an IDE.
//
//  INSPECTOR: every component on the entity selected in the Hierarchy, fields
//  editable and taking effect immediately - transform position/rotation/scale
//  as draggable fields, the sprite's texture and tint, collider bounds with
//  layer and mask as a CHECKBOX GRID.
//
//  TOOLBAR: Play / Pause / Step driving the GameClock, a time-scale slider,
//  the tick count and frame time.
//
//  ---------------------------------------------------------------------------
//  PAUSE + STEP + INSPECTOR IS THE PAYOFF FOR THE WHOLE SEMESTER. Pause the
//  simulation, step exactly one tick, and watch one entity's position change
//  by exactly one integration step in a table while its collider is
//  highlighted in the viewport. Ch. 10.5 argues for exactly this, and there is
//  no faster way to find out why a collision is not firing.
//
//  THE LAYER MASK GRID IS A MILESTONE 4 VERIFICATION ITEM: uncheck one box,
//  watch the collision events stop; check it, watch them resume.
//
//  ---------------------------------------------------------------------------
//  *** EDITING IS WHERE THIS GETS GENUINELY HARD. The three answers: ***
//
//  1. EDIT WHILE PAUSED - safe, and where most editing should happen. Nothing
//     is running to overwrite the value.
//
//  2. EDIT WHILE RUNNING - a system may overwrite the value on the very next
//     tick. Confusing rather than dangerous. THIS PANEL SHOWS SYSTEM-OWNED
//     FIELDS AS READ-ONLY WHILE RUNNING and says so, rather than letting
//     someone drag a position that a movement system puts straight back.
//
//  3. DESTROY FROM THE INSPECTOR - goes through the DEFERRED DESTROY QUEUE.
//     Deleting directly from a panel mid-frame is exactly the
//     iterator-invalidation bug DeferredOps exists to prevent, and the IDE is
//     not exempt from the engine's rules. This is where you discover the
//     editor is a system with the same constraints as any other.
//
//  THE GATE IS UNAFFECTED: adding an Inspector field is editor work and
//  touches no engine internals.
// =============================================================================
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
