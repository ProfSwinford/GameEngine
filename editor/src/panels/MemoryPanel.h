#pragma once
// =============================================================================
//  WEEK 7 PANEL - allocator statistics. This REPLACES the debug-text HUD
//  described in the Week 7 core work; the text version still exists in the
//  sandbox, which has no ImGui.
//
//  Per allocator: bytes used against capacity as a progress bar, PEAK marked
//  distinctly, allocation and block counts, and a history plot of bytes in
//  use.
//
//  ---------------------------------------------------------------------------
//  THE PLOT IS THE POINT. A single number gives the current level; the plot
//  gives the SHAPE, and shape is what you need:
//
//    SAWTOOTH     - allocated and freed each frame. Healthy for a scratch
//                   allocator, suspicious for anything else.
//    STAIRCASE UP - a leak, one step per event. This is exactly what the Week
//                   10 thousand-frame stress test looks for, and on a plot it
//                   is unmistakable in about four seconds.
//    FLAT         - what an update path should look like after warm-up. Week 8
//                   asks for a proof of precisely this.
//
//  ---------------------------------------------------------------------------
//  ONE HONEST WARNING, heeded: THIS PANEL ALLOCATES, and so does ImGui. If the
//  history buffers grew every frame, the panel would become the leak it exists
//  to detect - a genuinely embarrassing bug and an easy one to write.
//
//  So the history rings are FIXED-SIZE C ARRAYS, not vectors, and the panel
//  displays its own footprint alongside the engine's, so the cost of observing
//  is visible rather than hidden. Same discipline as measuring the timer's own
//  overhead in Week 4.
// =============================================================================
#include "Panel.h"

#include <engine/core/Types.h>

namespace editor {

class MemoryPanel final : public Panel {
public:
    const char* Title() const override { return "Memory"; }
    void        Draw() override;

private:
    static constexpr int kHistory   = 240;   // four seconds at 60 Hz
    static constexpr int kMaxTracked = 8;    // allocators plotted

    // FIXED SIZE. Not vectors that grow. See the warning above.
    float m_history[kMaxTracked][kHistory] = {};
    int   m_head = 0;
};

} // namespace editor
