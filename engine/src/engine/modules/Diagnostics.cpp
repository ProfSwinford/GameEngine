// ============================================================================
//  Diagnostics.cpp - the message log, and the in-memory buffer the editor Console reads.
//  See Diagnostics.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Settings.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

// ============================================================================
//  Log.cpp - implementation of the message log declared in Log.h.
//
//  Every message is written to three destinations:
//    1. the terminal, with a colour that depends on the level
//    2. logs/engine.log, so the output survives a crash and can be attached to
//       a bug report
//    3. LogBuffer, the in-memory list the editor's Console window draws from
//
//  The state below lives in an anonymous namespace. An anonymous namespace is
//  the standard C++ way to say "these names belong to this file only" - no
//  other .cpp can see or accidentally reuse them.
// ============================================================================



namespace eng {
namespace {

// std::ofstream is the standard file-writing stream. The file is opened ONCE
// in Init and kept open for the whole run: opening and closing a file for every
// log line is slow enough to change the timing of whatever you were trying to
// observe.
std::ofstream g_file;

LogLevel g_threshold   = LogLevel::Info;
bool     g_initialised = false;

// std::chrono::steady_clock is the standard clock that is guaranteed never to
// go backwards. The other two standard clocks can: system_clock is the wall
// clock, and it jumps when the machine syncs its time or the user changes
// timezone. A timestamp that jumps backwards in the middle of a log file is
// worse than no timestamp at all.
std::chrono::steady_clock::time_point g_start;

// Bookkeeping for the flush policy at the bottom of Write().
double      g_lastFlushSeconds = 0.0;
std::size_t g_pendingLines     = 0;

double ElapsedSeconds() {
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - g_start;
    return elapsed.count();
}

// ANSI escape codes. These are the text sequences every modern terminal
// understands as "change the colour of what comes next". A terminal that does
// not understand them simply ignores them, so there is nothing to detect and
// nothing to configure.
const char* ColorFor(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return "\x1b[0m";    // default
        case LogLevel::Warning: return "\x1b[33m";   // yellow
        case LogLevel::Error:   return "\x1b[31m";   // red
    }
    return "\x1b[0m";
}

} // namespace

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return "Info";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Error:   return "Error";
    }
    return "?";
}

bool ParseLogLevel(std::string_view text, LogLevel& out) {
    // Lower-case the incoming text first so "Info", "INFO" and "info" all work.
    // Config files are written by people, and being fussy about capitalisation
    // buys nothing.
    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) {
        const bool upper = (c >= 'A' && c <= 'Z');
        lowered.push_back(upper ? static_cast<char>(c + ('a' - 'A')) : c);
    }

    if (lowered == "info")                            { out = LogLevel::Info;    return true; }
    if (lowered == "warning" || lowered == "warn")    { out = LogLevel::Warning; return true; }
    if (lowered == "error")                           { out = LogLevel::Error;   return true; }
    return false;
}

bool Log::Init(const BootConfig& config) {
    // How many messages the editor's Console window keeps; see LogBuffer.h.
    LogBuffer::SetCapacity(static_cast<std::size_t>(config.logBufferCapacity));

    g_start            = std::chrono::steady_clock::now();
    g_lastFlushSeconds = 0.0;
    g_pendingLines     = 0;
    g_threshold        = config.logThreshold;

    if (!config.logFile.empty()) {
        const std::string path(config.logFile);

        // Create the folder the log file lives in if it is missing, so that a
        // freshly cloned copy of the project writes "logs/engine.log" without
        // anybody having to make the folder by hand.
        //
        // std::filesystem is the standard cross-platform path and directory
        // library (C++17). Using it means this code is identical on Windows,
        // macOS and Linux instead of needing a #ifdef per platform.
        const std::size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::error_code ec;   // the non-throwing overload: a missing folder
                                  // is not worth an exception here
            std::filesystem::create_directories(path.substr(0, slash), ec);
        }

        g_file.open(path, std::ios::out | std::ios::trunc);
        if (!g_file.is_open()) {
            std::fprintf(stderr, "[Log] could not open '%s'; terminal only\n",
                         path.c_str());
        }
    }

    g_initialised = true;
    return true;
}

void Log::Shutdown() {
    // Log the last line BEFORE closing the file, so the file ends with a
    // message saying the shutdown was orderly. A log that simply stops is
    // indistinguishable from a crash.
    Write(Channels::kCore, LogLevel::Info, "log shutting down");

    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
    g_initialised = false;
}

bool Log::IsInitialised() { return g_initialised; }

void     Log::SetThreshold(LogLevel level) { g_threshold = level; }
LogLevel Log::GetThreshold()               { return g_threshold; }
bool     Log::ShouldLog(LogLevel level)    { return level >= g_threshold; }

void Log::Write(std::string_view channel, LogLevel level, std::string_view message) {
    // Checked again here even though the macro already checked, because Write
    // is public and something may call it directly.
    if (!ShouldLog(level)) {
        return;
    }

    LogRecord record;
    record.timeSeconds = ElapsedSeconds();
    record.level       = level;
    record.channel.assign(channel);
    record.message.assign(message);

    // Destination 1 of 3: the editor's Console window.
    LogBuffer::Append(record);

    // One line, laid out so the columns line up when you read a wall of them:
    //   [    1.234] [Warning] [Resource    ] could not load textures/foo.bmp
    //
    // The numbers inside the braces are std::format's alignment controls:
    //   {:9.3f}  - a number, 9 characters wide, 3 digits after the point
    //   {:>7}    - text, 7 characters wide, pushed to the right
    //   {:<12}   - text, 12 characters wide, pushed to the left
    const std::string line = std::format("[{:9.3f}] [{:>7}] [{:<12}] {}",
                                         record.timeSeconds, ToString(level),
                                         record.channel, record.message);

    // Destination 2 of 3: the terminal.
    std::fputs(ColorFor(level), stdout);
    std::fputs(line.c_str(), stdout);
    std::fputs("\x1b[0m\n", stdout);   // reset the colour so later output is normal
    if (level >= LogLevel::Warning) {
        std::fflush(stdout);
    }

    // Destination 3 of 3: the log file.
    if (g_file.is_open()) {
        g_file << line << '\n';

        // FLUSH POLICY.
        // A stream buffers what you write and only hands it to the operating
        // system when the buffer fills. If the program crashes before that
        // happens, the log file is empty - exactly when you need it most.
        // Flushing after every single line is the other extreme and is slow.
        //
        // The compromise: flush when the message is important, or when a
        // second has gone by, or when enough lines have piled up.
        ++g_pendingLines;
        const bool important = (level >= LogLevel::Warning);
        const bool stale     = (record.timeSeconds - g_lastFlushSeconds) >= 1.0;
        const bool batched   = (g_pendingLines >= 16);
        if (important || stale || batched) {
            g_file.flush();
            g_lastFlushSeconds = record.timeSeconds;
            g_pendingLines     = 0;
        }
    }
}

void Log::Flush() {
    std::fflush(stdout);
    if (g_file.is_open()) {
        g_file.flush();
    }
}

} // namespace eng


// ============================================================================
//  LogBuffer.cpp - the in-memory copy of the log. See LogBuffer.h.
//
//  The storage is a "ring buffer": one std::vector of a fixed size plus an
//  index saying where the next message goes. When the index reaches the end it
//  wraps back to the start and overwrites the oldest message.
//
//  WHY A RING RATHER THAN JUST push_back-ing FOREVER
//  A vector that only ever grows would use more and more memory the longer the
//  program runs. A ring uses a fixed amount no matter how long the session is,
//  and "forget the oldest" is exactly the behaviour a scrollback window wants.
// ============================================================================



namespace eng {
namespace {

std::vector<LogRecord> g_ring;                                   // the storage
std::size_t            g_capacity = LogBuffer::kDefaultCapacity;  // how many fit
std::size_t            g_head     = 0;   // where the NEXT message will be written
std::size_t            g_size     = 0;   // how many slots currently hold a message
unsigned long long     g_total    = 0;   // messages ever written, including dropped

// std::set keeps its contents sorted and refuses duplicates automatically,
// which is exactly what "the list of distinct channel names, in alphabetical
// order" needs. Using a std::vector here would mean searching it by hand
// before every insert and sorting it before every read.
std::set<std::string> g_channels;

} // namespace

void LogBuffer::SetCapacity(std::size_t capacity) {
    if (capacity == 0) {
        capacity = 1;   // a zero-length ring would divide by zero below
    }
    g_capacity = capacity;
    g_ring.clear();
    g_ring.shrink_to_fit();   // actually hand the old memory back
    g_head = 0;
    g_size = 0;
}

std::size_t LogBuffer::Capacity() { return g_capacity; }

void LogBuffer::Append(const LogRecord& record) {
    // The storage is allocated the first time something is logged rather than
    // at startup, so a program that never logs never pays for it.
    if (g_ring.size() < g_capacity) {
        g_ring.resize(g_capacity);
    }

    LogRecord stored = record;
    stored.sequence  = ++g_total;

    g_ring[g_head] = std::move(stored);

    // The modulo is what makes this a ring: after the last slot, index 0 again.
    g_head = (g_head + 1) % g_capacity;

    // Grows until the ring is full, then stays there forever.
    g_size = std::min(g_size + 1, g_capacity);

    g_channels.insert(record.channel);
}

void LogBuffer::Snapshot(std::vector<LogRecord>& out) {
    out.clear();
    out.reserve(g_size);

    // Once the ring has wrapped, the OLDEST message is the one at g_head -
    // the slot about to be overwritten next. Before it wraps, the oldest is
    // simply slot 0.
    const std::size_t first = (g_size == g_capacity) ? g_head : 0;

    for (std::size_t i = 0; i < g_size; ++i) {
        out.push_back(g_ring[(first + i) % g_capacity]);
    }
}

void LogBuffer::Channels(std::vector<std::string>& out) {
    out.assign(g_channels.begin(), g_channels.end());
}

std::size_t        LogBuffer::Count()        { return g_size; }
unsigned long long LogBuffer::TotalWritten() { return g_total; }

void LogBuffer::Clear() {
    g_head = 0;
    g_size = 0;
    // The channel names are deliberately kept. Pressing "Clear" in the Console
    // should empty the message list, not make the filter checkboxes disappear
    // out from under the person who was using them.
}

} // namespace eng
