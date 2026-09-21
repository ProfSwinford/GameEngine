// =============================================================================
//  Log.cpp - a skeleton. Every function is here with the right signature and an
//  empty body. Log.h is the specification; read it before filling one in.
// =============================================================================

///
///TODO: Implement text color function and text integration for output
/// 

#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/core/LogBuffer.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace eng {
std::ofstream g_file;
LogLevel g_threshold = LogLevel::Info;
bool g_initialized = false;

std::chrono::steady_clock::time_point g_start;

double g_lastFlushSeconds = 0.0;
std::size_t g_pendingLines = 0;

double ElapsedSeconds() {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - g_start;

    return elapsed.count();
}

// Turns a level into the word the Console and the log file show.
const char* ToString(LogLevel level) {
    switch (level) {
        using enum LogLevel;
        case Info:      return "Info";
        case Warning:   return "Warning";
        case Error:     return "Error";
    }
    
    return "?";
}

// Turns a word from the settings file back into a level. Returns false when the
// text is not a level name, so the caller can report it rather than guess.
bool ParseLogLevel(std::string_view text, LogLevel& out) {
    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) {
        const bool upper = (c >= 'A' && c <= 'Z');
        lowered.push_back(upper ? static_cast<char>(c+('a'-'A')) : c);
    }

    if (lowered == "info")                          { out = LogLevel::Info;     return true; }
    if (lowered == "warning" || lowered == "warn")  { out = LogLevel::Warning;  return true; }
    if (lowered == "error")                         { out = LogLevel::Error;    return true; }

    return false;
}

// Opens the log: the terminal, the log file, and the in-memory list the editor's
// Console window reads. First subsystem up, because everything else writes to it.
bool Log::Init(const BootConfig& config) {
    LogBuffer::SetCapacity(static_cast<std::size_t>(config.logBufferCapacity));
    
    g_start = std::chrono::steady_clock::now();
    g_lastFlushSeconds = 0.0;
    g_pendingLines = 0;
    g_threshold = config.logThreshold;

    if (!config.logFile.empty()){
        const std::string path(config.logFile);

        const std::size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::error_code ec;

            std::filesystem::create_directories(path.substr(0,slash), ec);
        }

        g_file.open(path, std::ios::out | std::ios::trunc);
        if (!g_file.is_open()) {
            std::fprintf(stderr, "[Log] could not open '%s'; terminal only\n",
                         path.c_str());
        }
    }

    g_initialized = true;
    return true;
}

// Closes the log file. Last subsystem down, so that every other subsystem's
// shutdown message still has somewhere to go.
void Log::Shutdown() {
    Write(Channels::kCore, LogLevel::Info, "log shutting down");
    
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
    g_initialized = false;

}

// Has the log been opened yet? Anything that might run before start-up asks
// this first.
bool Log::IsInitialised() { return g_initialized; }

// Sets the lowest level that gets recorded. Anything below it is dropped.
void Log::SetThreshold(LogLevel level) { g_threshold = level; }

// The level currently being filtered at.
LogLevel Log::GetThreshold() { return g_threshold; }

// Would a message at this level be recorded? The logging macros ask this BEFORE
// formatting, so a filtered-out message never pays the cost of building its text.
bool Log::ShouldLog(LogLevel level) { return level >= g_threshold; }

// Records one finished message to all three destinations at once.
void Log::Write(std::string_view channel, LogLevel level, std::string_view message) {
    if (!ShouldLog(level)) {
        return;
    }

    LogRecord record;
    record.timeSeconds = ElapsedSeconds();
    record.level = level;
    record.channel.assign(channel);
    record.message.assign(message);

    LogBuffer::Append(record); //The Editor's console window output

    //[1.234][Warning][Resource] | could not load textures
    const std::string line = std::format("[{:9.3f}] [{:>7}] [{:<12}] | {}",
                                        record.timeSeconds, ToString(level),
                                        record.channel, record.message);

    // Terminal output
    // excluded: Change to new color

    std::fputs(line.c_str(), stdout);

    // excluded: Change back to default

    if (level >= LogLevel::Warning){
        std::fflush(stdout);
    }

    // Log file output
    if (g_file.is_open()) {
        g_file << line << '\n';

        g_pendingLines++;

        const bool important = (level >= LogLevel::Warning);
        const bool stale = (record.timeSeconds - g_lastFlushSeconds) >= 1.0;
        const bool batched = (g_pendingLines >= 16);

        if (important || stale || batched) {
            g_file.flush();
            g_lastFlushSeconds = record.timeSeconds;
            g_pendingLines = 0;
        }
    }
}

// Pushes anything buffered out to the log file now, so a crash straight
// afterwards still leaves a readable record.
void Log::Flush() {
    std::fflush(stdout);
    if (g_file.is_open()) {
        g_file.flush();
    }
}

} // namespace eng
