#pragma once
// =============================================================================
//  WEEK 3 PANEL - the log console. The panel that turns the IDE from a demo
//  into a tool, and the one used most for the rest of the semester.
//
//  Everything it needs was built in Week 3: named channels, graded verbosity,
//  a threshold. The panel is what makes that design pay off - the whole reason
//  for channels and levels is being able to ask for ONLY the resource manager
//  at ONLY warning and above, and this is where you ask.
//
//  ---------------------------------------------------------------------------
//  THE ENGINE-SIDE CHANGE THIS FORCED, which is the actual lesson:
//
//  The Week 3 logger wrote to a console and a file - both WRITE-ONLY. A panel
//  has to read entries back, so the logger grew a third sink: LogBuffer, a
//  fixed-capacity ring storing LEVEL AND CHANNEL AS DATA rather than as
//  pre-formatted text, with a public way to iterate it.
//
//  A write-only logger was always a slightly poor design; the panel is what
//  made it obvious.
//
//  THE CHANNEL LIST IS DISCOVERED AT RUNTIME from LogBuffer::Channels() and is
//  never hardcoded, because a channel was added in nearly every week after
//  Week 3 and a hardcoded list would have been wrong by Week 4.
//
//  This is also the one panel that legitimately holds state - the filters are
//  a USER PREFERENCE, not engine data.
// =============================================================================
#include "Panel.h"

#include <engine/core/LogBuffer.h>

#include <map>
#include <string>
#include <vector>

namespace editor {

class LogPanel final : public Panel {
public:
    const char* Title() const override { return "Log"; }
    void        Draw() override;

private:
    std::map<std::string, bool>  m_channelEnabled;   // discovered at runtime
    std::vector<std::string>     m_channels;
    std::vector<eng::LogRecord>  m_snapshot;
    int                          m_minLevel  = static_cast<int>(eng::LogLevel::Trace);
    char                         m_search[128] = {};
    bool                         m_autoScroll  = true;
};

} // namespace editor
