#pragma once

// =============================================================================
//  WEEK 3 - the logger. Ch. 10.1.
//
//  Why not printf: by Week 9 there are a dozen subsystems producing output at
//  once, and the question you want to ask is never "show me everything." It is
//  "show me only the resource manager, only warnings and worse, and put it in
//  a file I can read after the crash."
//
//  Four things make that possible and all four are here:
//    - NAMED CHANNELS      - which subsystem said this
//    - GRADED VERBOSITY    - how much does it matter
//    - MULTIPLE SINKS      - console, file, and (Week 3 panel) memory
//    - A LAUNCH THRESHOLD  - decided at startup, without a rebuild
//
//  ---------------------------------------------------------------------------
//  CHANNEL REPRESENTATION - the deliberate decision, recorded as instructed.
//
//  A channel is a `std::string_view` naming a string literal, and the literals
//  live in Channels below. Not an enum, not a hashed id. Reasons:
//
//    - An enum cannot be extended by a subsystem that does not want to edit a
//      shared header, and by Week 9 there are a dozen channels across eight
//      directories.
//    - A string_view over a literal does not allocate at the call site. The
//      only copy is made inside the memory sink, once, and only for messages
//      that survive the threshold.
//    - The Week 3 log panel must discover the channel list AT RUNTIME - the
//      instructions are explicit that hardcoding it is wrong - and a set of
//      strings observed in the sink does that with no registration step.
//
//  WEEK 8 REVISIT: StringId now exists and would make the filter comparison an
//  integer compare. It was deliberately NOT converted, because the log panel
//  and the log file both need the text, so the hash would need the reverse
//  table on every line - paying for the hash and the lookup to save a compare
//  that happens once per surviving message. The measurement that would change
//  this decision is log volume in the hot path, and there is none: the hot
//  path logs nothing.
//
//  WEEK 5: this is thread-safe. The lock lives inside Write(), around the sink
//  fan-out only - see the note in Log.cpp about where the Week 3 guess was.
// =============================================================================

#include <engine/core/Types.h>

#include <format>
#include <string_view>

namespace eng {

enum class LogLevel : u8 {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

// Text for a level, for sinks and for the editor's level dropdown.
const char* ToString(LogLevel level);

// Parses "Info", "warning", ... Returns false and leaves `out` alone on a
// name it does not recognise - config files are written by humans.
bool ParseLogLevel(std::string_view text, LogLevel& out);

// The channels the engine itself uses. Nothing enforces this list; a
// subsystem may pass any literal. It exists so that a reader can find them
// all in one place and so that typos in engine code are a compile error
// rather than a silently new channel.
namespace Channels {
inline constexpr std::string_view kCore      = "Core";
inline constexpr std::string_view kPlatform  = "Platform";
inline constexpr std::string_view kInput     = "Input";
inline constexpr std::string_view kRender    = "Render";
inline constexpr std::string_view kMemory    = "Memory";
inline constexpr std::string_view kJobs      = "Jobs";
inline constexpr std::string_view kFileSys   = "FileSystem";
inline constexpr std::string_view kResource  = "Resource";
inline constexpr std::string_view kScene     = "Scene";
inline constexpr std::string_view kPhysics   = "Physics";
inline constexpr std::string_view kProfile   = "Profile";
inline constexpr std::string_view kConfig    = "Config";
inline constexpr std::string_view kEditor    = "Editor";
inline constexpr std::string_view kGame      = "Game";
} // namespace Channels

class Log {
public:
    // Opens the sinks. Console is always on; the file sink is opened once,
    // here, and kept open - opening a file per log line is slow enough to
    // change the timing of the thing you were trying to observe.
    //
    // An empty path means "console and memory only", which is what the test
    // binary wants.
    static bool Init(std::string_view logFilePath, LogLevel threshold);

    // Flushes and closes. The logger must OUTLIVE everything that logs during
    // its own shutdown, which is why it is registered first and therefore torn
    // down last. Week 7 makes that ordering explicit and declared.
    static void Shutdown();

    static bool IsInitialised();

    // The actual write. Drops anything below the threshold.
    static void Write(std::string_view channel, LogLevel level, std::string_view message);

    static void SetThreshold(LogLevel level);
    static LogLevel GetThreshold();

    // The threshold test, exposed so the macros below can skip FORMATTING a
    // message that is going to be dropped. This is why the check is here and
    // not inside Write(): std::format allocates and walks the format string,
    // and a Trace call in an update loop should cost a load and a compare.
    static bool ShouldLog(LogLevel level);

    static void Flush();
};

} // namespace eng

// -----------------------------------------------------------------------------
//  Convenience macros.
//
//  C++23's std::format is far closer to C# string interpolation than printf
//  is, and it is type-checked at COMPILE time - a bad specifier is a build
//  error rather than a 3am surprise.
//
//  Note the shape: the threshold check happens BEFORE std::format runs. A
//  suppressed message costs one comparison.
// -----------------------------------------------------------------------------
#define ENGINE_LOG(channel, level, ...)                                        \
    do {                                                                       \
        if (::eng::Log::ShouldLog(level)) {                                    \
            ::eng::Log::Write((channel), (level), ::std::format(__VA_ARGS__)); \
        }                                                                      \
    } while (false)

#define ENGINE_LOG_TRACE(channel, ...) ENGINE_LOG(channel, ::eng::LogLevel::Trace,   __VA_ARGS__)
#define ENGINE_LOG_DEBUG(channel, ...) ENGINE_LOG(channel, ::eng::LogLevel::Debug,   __VA_ARGS__)
#define ENGINE_LOG_INFO(channel, ...)  ENGINE_LOG(channel, ::eng::LogLevel::Info,    __VA_ARGS__)
#define ENGINE_LOG_WARN(channel, ...)  ENGINE_LOG(channel, ::eng::LogLevel::Warning, __VA_ARGS__)
#define ENGINE_LOG_ERROR(channel, ...) ENGINE_LOG(channel, ::eng::LogLevel::Error,   __VA_ARGS__)
#define ENGINE_LOG_FATAL(channel, ...) ENGINE_LOG(channel, ::eng::LogLevel::Fatal,   __VA_ARGS__)
