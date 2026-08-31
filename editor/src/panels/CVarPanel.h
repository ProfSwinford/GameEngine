#pragma once
// =============================================================================
//  WEEK 8 PANEL - the CVar editor. The highest-payoff panel in Phase 1.
//
//  Every registered CVar in a filterable table, type-appropriate editing,
//  changes taking effect IMMEDIATELY with no relaunch, descriptions as
//  tooltips, and a Save button writing values back to the config file.
//
//  ---------------------------------------------------------------------------
//  WHY IT IS WORTH DOING PROPERLY: without it, changing a tunable costs edit
//  source, rebuild, relaunch, navigate back to what you were testing - call it
//  ninety seconds. With it, a mouse drag. Phase 2 tunes player speed, jump
//  height, spawn rates and camera feel, twenty or thirty times each. It pays
//  for itself in the first afternoon.
//
//  It is also the concrete form of Ch. 6.5's argument that a hardcoded
//  constant is a design smell. The smell is not aesthetic; it is that ninety
//  seconds, multiplied.
//
//  ---------------------------------------------------------------------------
//  THE ENGINE-SIDE REQUIREMENT IT EXPOSED: CVarRegistry MUST BE ENUMERABLE. A
//  registry that only answers lookups cannot be browsed, because the panel
//  does not know what to ask for. That is why ForEach is a requirement in
//  CVar.h rather than a nicety, and it is a good example of a tool requirement
//  improving an engine API.
//
//  THE INPUT-CAPTURE PAYOFF: type in a text field here and the player does not
//  move. If it did, EventPump would be ignoring ImGui's capture flags from
//  Week 2 - and this is the harmless place to find that out.
// =============================================================================
#include "Panel.h"

namespace editor {

class CVarPanel final : public Panel {
public:
    const char* Title() const override { return "CVars"; }
    void        Draw() override;

private:
    char m_filter[96] = {};
    bool m_modifiedOnly = false;
    char m_status[192] = {};
};

} // namespace editor
