#pragma once
// =============================================================================
//  WEEK 4 PANEL - the profiler. Reads TimerRegistry.
//
//  A sortable table of every named measurement site with min, average, max and
//  sample count, a Reset button, and a frame-time history plot.
//
//  ---------------------------------------------------------------------------
//  WHY THE PLOT EARNS ITS PLACE: an average hides exactly the thing you care
//  about. A frame that takes 4 ms ninety-nine times and 40 ms once has a fine
//  average and a visible stutter. The plot shows the spike. It is the same
//  argument the Week 4 report makes when it asks for min/avg/max rather than a
//  single number - rendered.
//
//  ---------------------------------------------------------------------------
//  IT MEASURES THE IDE ITSELF. "Editor::Draw" wraps the whole panel pass and
//  appears in this table like anything else. An IDE costing 8 ms a frame
//  quietly corrupts every measurement taken in Weeks 5, 7 and 10; knowing the
//  number is the difference between a tool and a confound - the same
//  discipline as measuring the timer's own overhead, one level up.
//
//  Week 10 extended this into the per-system HUD, and collision appears as its
//  own line item because SystemScheduler wraps every system's Update in a
//  timer named after it. Adding a row costs nothing, which was the point of
//  building the table this way in Week 4.
// =============================================================================
#include "Panel.h"

#include <engine/debug/ScopedTimer.h>

#include <string>
#include <vector>

namespace editor {

class ProfilerPanel final : public Panel {
public:
    const char* Title() const override { return "Profiler"; }
    void        Draw() override;

private:
    struct Row {
        std::string     name;
        eng::TimerStats stats;
    };
    std::vector<Row> m_rows;
};

} // namespace editor
