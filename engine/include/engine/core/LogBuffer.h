#pragma once

// ============================================================================
//  LogBuffer.h - the copy of the log that the editor's Console window shows.
//
//  WHY THIS EXISTS
//  The terminal and the log file are both write-only: once a message has been
//  printed there is no way to ask for it back. The Console window has to be
//  able to re-read every message every frame, because the user can change the
//  filters at any time and the whole list has to be redrawn with the new ones
//  applied. So the log keeps a third copy, in memory, right here.
//
//  WHAT IT STORES
//  One LogRecord per message, with the level and the channel kept as separate
//  fields rather than glued into one pre-formatted line of text. That is what
//  lets the Console filter without having to take strings apart again.
//
//  WHY IT IS A FIXED-SIZE LIST
//  A program left running all afternoon would otherwise fill memory with log
//  text. Once the list is full the oldest message is dropped to make room, the
//  same way a chat window forgets the top of the scrollback.
// ============================================================================

#include <engine/core/Log.h>

#include <string>
#include <vector>

namespace eng {

// One message, kept as data instead of as a finished line of text.
struct LogRecord {
    // Counts up forever, even after old messages are dropped. The Console uses
    // it to notice that new messages arrived so it can auto-scroll.
    unsigned long long sequence = 0;

    double      timeSeconds = 0.0;   // seconds since Log::Init
    LogLevel    level       = LogLevel::Info;
    std::string channel;
    std::string message;
};

class LogBuffer {
public:
    // 4096 messages is several minutes of ordinary output and roughly half a
    // megabyte of text. Changeable from config/engine.json.
    static constexpr std::size_t kDefaultCapacity = 4096;

    static void        SetCapacity(std::size_t capacity);
    static std::size_t Capacity();

    // Called by Log::Write. You should not need to call this yourself.
    static void Append(const LogRecord& record);

    // Copies the whole list, oldest first, into `out`. The Console calls this
    // once per frame and draws from its own copy.
    //
    // WHY A COPY RATHER THAN HANDING BACK A REFERENCE
    // The list can gain a message at any moment (any code anywhere can log).
    // Drawing directly from the live list while it is being modified is the
    // classic way to walk off the end of a container. Copying is cheap here
    // and removes the whole category of bug.
    static void Snapshot(std::vector<LogRecord>& out);

    // Every channel name seen so far, sorted. The Console builds its
    // per-channel checkboxes from this, so channels the game code invents
    // appear in the filter list on their own with no registration step.
    static void Channels(std::vector<std::string>& out);

    static std::size_t        Count();
    static unsigned long long TotalWritten();   // includes dropped messages
    static void               Clear();
};

} // namespace eng
