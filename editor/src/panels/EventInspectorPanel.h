#pragma once
// =============================================================================
//  WEEK 2 PANEL - the first one, deliberately small.
//
//  Shows what the EventPump saw this frame: how many events arrived, of what
//  kinds, with what parameters, plus a running count per kind and a pause
//  toggle.
//
//  ---------------------------------------------------------------------------
//  WHAT THIS PANEL IS REALLY FOR: it is a TEST OF THE WEEK 2 ABSTRACTION.
//
//  To display an event, it reads RawEvent through the engine's public header
//  and includes NO SDL HEADER. It also shows key NAMES, which it gets from
//  EventPump::KeyName - because if the panel had to know that 44 is Space, the
//  scancode would have leaked out of the engine and Week 8's InputMap would
//  have been much harder.
//
//  If this panel could not be written without including SDL, that would be the
//  finding, not an inconvenience.
// =============================================================================
#include "Panel.h"

#include <engine/platform/EventPump.h>

namespace editor {

class EventInspectorPanel final : public Panel {
public:
    explicit EventInspectorPanel(const eng::EventPump& pump);

    const char* Title() const override { return "Event Inspector"; }
    void        Draw() override;

private:
    static constexpr eng::usize kKindCount = 9;   // RawEventKind, without Count

    const eng::EventPump* m_pump = nullptr;

    // Pause freezes the DISPLAY, not the pump. Freezing the pump would mean
    // the engine stopped receiving input because a debug panel asked it to,
    // which is the worst kind of debug tool - see the Week 5 panel note about
    // observing something you are also changing.
    bool                  m_paused = false;
    eng::RawEvent         m_frozen[32]{};
    eng::usize            m_frozenCount = 0;
};

} // namespace editor
