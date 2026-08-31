#pragma once

// ============================================================================
//  ConsolePanel.h - the log window. Unity calls this the Console, and this is
//  the same thing.
//
//  It shows every message the engine and your game code have written, newest
//  at the bottom, and lets you narrow the list down:
//
//    * by LEVEL   - hide the routine chatter and show only warnings and errors
//    * by CHANNEL - show only the messaging from one part of the program
//    * by TEXT    - a search box
//
//  This is the panel you will spend the most time in. When something is not
//  working, add a line to your code:
//
//      ENGINE_LOG_INFO(eng::Channels::kGame, "health is now {}", health);
//
//  and watch it here while the game runs.
//
//  WHERE THE MESSAGES COME FROM
//  The log writes to three places at once: the terminal, a file, and an
//  in-memory list. This panel reads that third one - see LogBuffer.h. A
//  terminal and a file are both write-only, so a window that has to redraw the
//  whole list every frame with new filters applied needs a copy it can read
//  back.
//
//  THE CHANNEL LIST IS DISCOVERED WHILE THE PROGRAM RUNS. Nothing here has a
//  list of channel names in it. Invent a new channel name in your own code and
//  a checkbox for it appears here on its own.
//
//  This is also the one panel that legitimately remembers things between
//  frames - the filter settings. Those are a preference belonging to the
//  person using the editor, not information about the game.
// ============================================================================

#include "Panel.h"

#include <engine/core/LogBuffer.h>

#include <map>
#include <string>
#include <vector>

namespace editor {

class ConsolePanel final : public Panel {
public:
    const char* Title() const override { return "Console"; }
    void        Draw() override;

private:
    // Which channels are ticked. Filled in as channels are discovered.
    std::map<std::string, bool> m_channelEnabled;

    std::vector<std::string>    m_channels;
    std::vector<eng::LogRecord> m_snapshot;

    int  m_minLevel     = static_cast<int>(eng::LogLevel::Info);
    char m_search[128]  = {};
    bool m_autoScroll   = true;
};

} // namespace editor
