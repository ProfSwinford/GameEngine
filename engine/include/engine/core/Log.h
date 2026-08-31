#pragma once

// ============================================================================
//  Log.h - the engine's message log.
//
//  WHAT THIS FILE GIVES YOU
//  Three macros. Use them instead of std::cout anywhere in the engine, the
//  editor, or your game code:
//
//      ENGINE_LOG_INFO (Channels::kGame, "player spawned at {}, {}", x, y);
//      ENGINE_LOG_WARN (Channels::kGame, "no texture named '{}'", path);
//      ENGINE_LOG_ERROR(Channels::kGame, "scene failed to load: {}", why);
//
//  Every message goes to three places at once: the terminal, a text file
//  (logs/engine.log), and an in-memory list that the editor's Console window
//  reads and displays. See LogBuffer.h for that third one.
//
//  WHY A LOG INSTEAD OF std::cout
//  std::cout gives you a wall of text with no way to ask "show me only the
//  errors" or "show me only what the physics code said". A log message here
//  carries two extra pieces of information that make those questions
//  answerable:
//
//    * a LEVEL   - how much it matters (Info, Warning, Error)
//    * a CHANNEL - which part of the program said it ("Physics", "Scene", ...)
//
//  The editor's Console window turns those two fields into filter buttons.
//  That is the entire reason they exist.
//
//  WHY std::format (from <format>) RATHER THAN printf
//  std::format is the C++20 standard formatting library. It is used here for
//  two reasons:
//    1. It checks the format string against the arguments AT COMPILE TIME, so
//       a mismatch is a build error rather than a crash while the game runs.
//       printf cannot do that - it will happily print garbage.
//    2. It works with std::string and with any type, so you never have to
//       remember whether an argument needs %d, %s, %f or %zu. Every value is
//       just "{}".
//
//  WHY THE MACROS EXIST INSTEAD OF PLAIN FUNCTIONS
//  A macro can skip the work entirely. The threshold test happens BEFORE
//  std::format runs, so a message that is going to be filtered out never pays
//  the cost of building its own text. A plain function would have to build the
//  string first just to hand it over and have it thrown away.
// ============================================================================

#include <format>
#include <string_view>

namespace eng {

// How much a message matters. The editor's Console shows one filter button per
// entry here, exactly like Unity's Console does.
enum class LogLevel {
    Info,      // ordinary progress: "scene loaded", "window created"
    Warning,   // something looks wrong but the program continues
    Error,     // something failed; a feature will not work
};

// Turns a level into text for the log file and the Console's dropdown.
const char* ToString(LogLevel level);

// Reads a level out of the config file, accepting "info", "Warning", "ERROR".
// Returns false and leaves `out` untouched for anything else, because config
// files are typed by people and a typo should not silently change behaviour.
bool ParseLogLevel(std::string_view text, LogLevel& out);

// The channel names the engine uses. A channel is just a short label saying
// which part of the program produced a message.
//
// WHY std::string_view AND NOT std::string
// A std::string_view is the standard "I am looking at some text that somebody
// else owns" type. Every name below is a string literal that lives for the
// whole run of the program, so there is nothing to own and nothing to copy.
// Passing a std::string here would make a fresh heap copy of "Physics" every
// single time you logged a message.
//
// Nothing forces you to use a name from this list - any text works - but
// keeping the engine's own names here means a typo in engine code is a
// compiler error instead of a silently-new channel in the Console.
namespace Channels {
inline constexpr std::string_view kCore     = "Core";
inline constexpr std::string_view kPlatform = "Platform";
inline constexpr std::string_view kInput    = "Input";
inline constexpr std::string_view kRender   = "Render";
inline constexpr std::string_view kFileSys  = "FileSystem";
inline constexpr std::string_view kResource = "Resource";
inline constexpr std::string_view kScene    = "Scene";
inline constexpr std::string_view kPhysics  = "Physics";
inline constexpr std::string_view kConfig   = "Config";
inline constexpr std::string_view kEditor   = "Editor";
inline constexpr std::string_view kGame     = "Game";
} // namespace Channels

// The log itself. Every function is static because there is exactly one log
// for the whole program and passing a pointer to it through every subsystem
// would be noise.
class Log {
public:
    // Opens the log file and starts the clock that timestamps each message.
    // Pass an empty path for "terminal and Console window only", which is what
    // the unit tests want.
    static bool Init(std::string_view logFilePath, LogLevel threshold);

    // Flushes and closes the file. The log is started first and shut down last
    // of everything in the engine, so that a subsystem can still report a
    // problem while it is being torn down.
    static void Shutdown();

    static bool IsInitialised();

    // Writes one message. The macros below call this; you normally should not.
    static void Write(std::string_view channel, LogLevel level,
                      std::string_view message);

    // Messages below the threshold are dropped. Set from config/engine.json,
    // so the amount of output can change without rebuilding.
    static void     SetThreshold(LogLevel level);
    static LogLevel GetThreshold();
    static bool     ShouldLog(LogLevel level);

    // Forces anything buffered out to the file. Called at shutdown, and worth
    // calling by hand just before code you suspect of crashing.
    static void Flush();
};

} // namespace eng

// ----------------------------------------------------------------------------
//  The macros.
//
//  `do { ... } while (false)` is the standard way to write a multi-statement
//  macro. It makes the whole macro behave like ONE statement, so this stays
//  correct:
//
//      if (health <= 0) ENGINE_LOG_INFO(Channels::kGame, "dead");
//      else             Respawn();
//
//  Without the do/while, the `else` would attach to the wrong `if`.
//
//  `__VA_ARGS__` is "everything the caller passed after the channel" - the
//  format string plus any values - handed straight to std::format.
// ----------------------------------------------------------------------------
#define ENGINE_LOG(channel, level, ...)                                        \
    do {                                                                       \
        if (::eng::Log::ShouldLog(level)) {                                    \
            ::eng::Log::Write((channel), (level), ::std::format(__VA_ARGS__));  \
        }                                                                      \
    } while (false)

#define ENGINE_LOG_INFO(channel, ...)  ENGINE_LOG(channel, ::eng::LogLevel::Info,    __VA_ARGS__)
#define ENGINE_LOG_WARN(channel, ...)  ENGINE_LOG(channel, ::eng::LogLevel::Warning, __VA_ARGS__)
#define ENGINE_LOG_ERROR(channel, ...) ENGINE_LOG(channel, ::eng::LogLevel::Error,   __VA_ARGS__)
