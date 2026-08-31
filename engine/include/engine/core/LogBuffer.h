#pragma once

// =============================================================================
//  WEEK 3 - the in-memory log sink. THE ENGINE CHANGE THE LOG PANEL FORCED.
//
//  The Week 3 logger writes to a console and a file. Both are WRITE-ONLY. The
//  editor's log console has to read entries back, so the logger grew a third
//  sink: a fixed-capacity ring of the most recent entries with a public way to
//  iterate it.
//
//  This is a good example of a tool requirement improving an engine design. A
//  write-only logger was always slightly poor; the panel is what made it
//  obvious.
//
//  Three design notes, all load-bearing:
//
//   - FIXED CAPACITY, oldest evicted. Unbounded growth over a long session is
//     a leak with a friendly name.
//
//   - LEVEL AND CHANNEL ARE STORED AS DATA, not baked into a pre-formatted
//     line. Otherwise the panel cannot filter without re-parsing strings it
//     just built.
//
//   - WEEK 5 (predicted in Week 3, and the guess was right): the ring is
//     shared state - the panel reads it on the main thread while worker
//     threads write. The lock is INSIDE this class, around every public
//     entry point, and Snapshot() copies under it so the panel never holds
//     the lock while drawing.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/core/Types.h>

#include <string>
#include <vector>

namespace eng {

struct LogRecord {
    u64         sequence = 0;      // monotonically increasing, survives eviction
    f64         timeSeconds = 0.0; // since Log::Init
    u64         threadId = 0;      // Week 3 stretch 1; essential from Week 5 on
    LogLevel    level = LogLevel::Info;
    std::string channel;
    std::string message;
};

class LogBuffer {
public:
    // The default is deliberately modest. 4096 entries is several minutes of
    // ordinary output and about half a megabyte.
    static constexpr usize kDefaultCapacity = 4096;

    static void SetCapacity(usize capacity);
    static usize Capacity();

    // Called by Log::Write. Not intended for direct use.
    static void Append(const LogRecord& record);

    // Copies the current contents, oldest first. The panel calls this once per
    // frame; copying is what lets it draw without holding the lock.
    static void Snapshot(std::vector<LogRecord>& out);

    // Every channel name seen since the last Clear(), sorted. This is what
    // makes the panel's per-channel checkboxes discoverable at runtime rather
    // than hardcoded - and the list grows on its own as weeks add channels.
    static void Channels(std::vector<std::string>& out);

    static usize Count();
    static u64   TotalWritten();   // includes entries already evicted
    static void  Clear();
};

} // namespace eng
