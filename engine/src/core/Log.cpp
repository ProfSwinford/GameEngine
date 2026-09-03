// =============================================================================
//  Log.cpp - a skeleton. Every function is here with the right signature and an
//  empty body. Log.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/core/Log.h>

namespace eng {

// Turns a level into the word the Console and the log file show.
const char* ToString(LogLevel /*level*/) {
    return "Info";
}

// Turns a word from the settings file back into a level. Returns false when the
// text is not a level name, so the caller can report it rather than guess.
bool ParseLogLevel(std::string_view /*text*/, LogLevel& /*out*/) {
    return false;
}

// Opens the log: the terminal, the log file, and the in-memory list the editor's
// Console window reads. First subsystem up, because everything else writes to it.
bool Log::Init(std::string_view /*logFilePath*/, LogLevel /*threshold*/) {
    return false;
}

// Closes the log file. Last subsystem down, so that every other subsystem's
// shutdown message still has somewhere to go.
void Log::Shutdown() {
}

// Has the log been opened yet? Anything that might run before start-up asks
// this first.
bool Log::IsInitialised() {
    return false;
}

// Sets the lowest level that gets recorded. Anything below it is dropped.
void Log::SetThreshold(LogLevel /*level*/) {
}

// The level currently being filtered at.
LogLevel Log::GetThreshold() {
    return LogLevel::Info;
}

// Would a message at this level be recorded? The logging macros ask this BEFORE
// formatting, so a filtered-out message never pays the cost of building its text.
bool Log::ShouldLog(LogLevel /*level*/) {
    return false;
}

// Records one finished message to all three destinations at once.
void Log::Write(std::string_view /*channel*/, LogLevel /*level*/,
                std::string_view /*message*/) {
}

// Pushes anything buffered out to the log file now, so a crash straight
// afterwards still leaves a readable record.
void Log::Flush() {
}

} // namespace eng
