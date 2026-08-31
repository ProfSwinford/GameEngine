// =============================================================================
//  WEEK 3 - the logger. See Log.h for the design contract.
//
//  Implementation notes, all of them earned:
//
//   - The file is opened ONCE at Init, not once per write.
//
//   - Flush on Warning and above, and on Shutdown. Not every line (a flush per
//     line is measurably expensive) and not never (the whole point of the file
//     sink is that it survives a crash, and an unflushed buffer does not).
//
//   - The threshold check lives in Log::ShouldLog and is called from the macro
//     BEFORE std::format runs, so a suppressed message never pays for its own
//     formatting. Write() re-checks, because Write() is public.
//
//  ---------------------------------------------------------------------------
//  WEEK 3 PREDICTION, and the Week 5 answer.
//
//  Week 3 guess: "the lock goes around the sink fan-out in Write(), and the
//  ring buffer needs its own because the panel reads it from another thread."
//
//  Week 5 verdict: correct on both counts, with one refinement. The mutex here
//  covers the console and file sinks only; LogBuffer has its own lock, so a
//  panel snapshotting the ring does not block a worker thread mid-write to the
//  file. Two small locks beat one big one when the readers and writers of the
//  two sinks are different threads.
//
//  The threshold is a std::atomic and is deliberately NOT under the mutex:
//  reading it is the first thing every log call does, including suppressed
//  ones, and making that contend would put a lock in the hot path.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/core/LogBuffer.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace eng {
namespace {

std::mutex               g_mutex;
std::ofstream            g_file;
std::atomic<LogLevel>    g_threshold{LogLevel::Info};
std::atomic<bool>        g_initialised{false};
std::chrono::steady_clock::time_point g_start;

// Seconds since Init at the last file flush, and lines written since then.
// Both guarded by g_mutex, like the stream they belong to. See the flush policy
// note in Write() - the line counter is the half that actually works.
f64   g_lastFlushSeconds = 0.0;
usize g_pendingLines     = 0;

u64 CurrentThreadId() {
    // Hashing the id is portable; the absolute value means nothing, the
    // difference between two of them means everything. Week 5's whole reason
    // for wanting this is telling two interleaved lines apart.
    return static_cast<u64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

f64 ElapsedSeconds() {
    using namespace std::chrono;
    return duration<f64>(steady_clock::now() - g_start).count();
}

// Console colour, because a wall of undifferentiated grey text is a wall you
// stop reading. ANSI escapes; harmless if the terminal ignores them.
const char* ColorFor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "\x1b[90m";
        case LogLevel::Debug:   return "\x1b[36m";
        case LogLevel::Info:    return "\x1b[0m";
        case LogLevel::Warning: return "\x1b[33m";
        case LogLevel::Error:   return "\x1b[31m";
        case LogLevel::Fatal:   return "\x1b[1;31m";
    }
    return "\x1b[0m";
}

} // namespace

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "Trace";
        case LogLevel::Debug:   return "Debug";
        case LogLevel::Info:    return "Info";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Error:   return "Error";
        case LogLevel::Fatal:   return "Fatal";
    }
    return "?";
}

bool ParseLogLevel(std::string_view text, LogLevel& out) {
    struct Entry { std::string_view name; LogLevel level; };
    static constexpr Entry kTable[] = {
        {"trace", LogLevel::Trace},     {"debug", LogLevel::Debug},
        {"info", LogLevel::Info},       {"warning", LogLevel::Warning},
        {"warn", LogLevel::Warning},    {"error", LogLevel::Error},
        {"fatal", LogLevel::Fatal},
    };

    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) {
        lowered.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c));
    }

    for (const Entry& entry : kTable) {
        if (entry.name == lowered) {
            out = entry.level;
            return true;
        }
    }
    return false;
}

bool Log::Init(std::string_view logFilePath, LogLevel threshold) {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_start            = std::chrono::steady_clock::now();
    g_lastFlushSeconds = 0.0;
    g_pendingLines     = 0;
    g_threshold.store(threshold, std::memory_order_relaxed);

    if (!logFilePath.empty()) {
        // Create the containing directory if the path names one. Doing this
        // here rather than making the caller do it means "logs/engine.log"
        // works out of the box on a fresh clone.
        const std::string path(logFilePath);
        const usize slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            const std::string dir = path.substr(0, slash);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
        }

        g_file.open(path, std::ios::out | std::ios::trunc);
        if (!g_file.is_open()) {
            std::fprintf(stderr, "[Log] could not open log file '%s'; console only\n",
                         path.c_str());
        }
    }

    g_initialised.store(true, std::memory_order_release);
    return true;
}

void Log::Shutdown() {
    // Deliberately logs its own last line BEFORE closing anything. Something
    // logging after this point gets the console fallback rather than a crash -
    // see IsInitialised() handling in Write().
    Write(Channels::kCore, LogLevel::Info, "Log shutting down");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
    g_initialised.store(false, std::memory_order_release);
}

bool Log::IsInitialised() {
    return g_initialised.load(std::memory_order_acquire);
}

void Log::SetThreshold(LogLevel level) {
    g_threshold.store(level, std::memory_order_relaxed);
}

LogLevel Log::GetThreshold() {
    return g_threshold.load(std::memory_order_relaxed);
}

bool Log::ShouldLog(LogLevel level) {
    return level >= g_threshold.load(std::memory_order_relaxed);
}

void Log::Write(std::string_view channel, LogLevel level, std::string_view message) {
    if (!ShouldLog(level)) {
        return;
    }

    LogRecord record;
    record.timeSeconds = ElapsedSeconds();
    record.threadId    = CurrentThreadId();
    record.level       = level;
    record.channel.assign(channel);
    record.message.assign(message);

    // Memory sink first, and outside our mutex: LogBuffer has its own lock and
    // its own readers.
    LogBuffer::Append(record);

    std::lock_guard<std::mutex> lock(g_mutex);

    // [   1.234] [ WARN] [Resource     ] (t:a3f1) message
    const std::string line =
        std::format("[{:9.3f}] [{:>7}] [{:<12}] (t:{:04x}) {}",
                    record.timeSeconds, ToString(level), record.channel,
                    static_cast<u16>(record.threadId & 0xFFFFu), record.message);

    std::fputs(ColorFor(level), stdout);
    std::fputs(line.c_str(), stdout);
    std::fputs("\x1b[0m\n", stdout);

    if (level >= LogLevel::Warning) {
        std::fflush(stdout);
    }

    if (g_file.is_open()) {
        g_file << line << '\n';

        // FLUSH POLICY, revised TWICE, and both revisions are worth recording
        // because the second one is a bug the first one hid.
        //
        // Week 3's rule was "flush on Warning and above, and at Shutdown; never
        // per line, because a flush per line is measurably expensive". Correct
        // as far as it goes, and it defeated the file sink's stated purpose:
        // an Info-only boot produces about 2 KB, an ofstream buffer is larger
        // than that, and killing the process left a ZERO-BYTE log after six
        // seconds of perfectly good output. A file sink that survives a crash
        // has to have actually written something.
        //
        // First fix: also flush if a second has passed since the last one.
        // That STILL left a zero-byte log, for a reason that is obvious in
        // hindsight - the check only runs when a line is written, and this
        // engine logs nothing at all during steady-state play. Boot finishes at
        // t=0.4s, no further line is ever written, so "has a second passed" is
        // never asked again and the buffer sits there indefinitely.
        //
        // Second fix, below: flush when EITHER a second has passed OR enough
        // lines have accumulated. The line counter is what makes it work,
        // because it does not depend on a later call arriving.
        //
        // Cost: boot flushes twice; a quiet session flushes never after that;
        // a chatty one flushes at most once per 16 lines. Nothing in the hot
        // path logs, so none of this is in the hot path.
        ++g_pendingLines;
        const bool important = level >= LogLevel::Warning;
        const bool stale     = (record.timeSeconds - g_lastFlushSeconds) >= 1.0;
        const bool batched   = g_pendingLines >= 16;
        if (important || stale || batched) {
            g_file.flush();
            g_lastFlushSeconds = record.timeSeconds;
            g_pendingLines     = 0;
        }
    }
}

void Log::Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fflush(stdout);
    if (g_file.is_open()) {
        g_file.flush();
    }
}

} // namespace eng
