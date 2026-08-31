#pragma once

// =============================================================================
//  WEEK 3 - the assert macros.
//
//  An assert is a claim about a thing that must be true. When it is false, the
//  program is already wrong and continuing only moves the eventual symptom
//  further from the cause.
//
//  Ch. 3.2's three-way split, as this engine applies it:
//    - assert       -> a programmer error. Cannot happen in correct code.
//    - error return -> an environment failure. File missing, device gone.
//    - exceptions   -> not used in engine code. Not in the hot path, and not
//                      anywhere the C# layer will eventually call across.
//
//  ---------------------------------------------------------------------------
//  THE TWO TRAPS, both met deliberately:
//
//  1. Turning `x > 0` into the string "x > 0" is the preprocessor's
//     STRINGISING operator, `#`. One character. See ENGINE_ASSERT below.
//
//  2. A macro that expands to a bare `if` breaks inside an if/else:
//
//         if (a) ENGINE_ASSERT(b); else DoThing();
//
//     expands to `if (a) if (!(b)) Report(); ; else DoThing();` - the `else`
//     binds to the inner `if` and the trailing `;` from the call site becomes
//     an empty statement, so this is a syntax error (and without the `;` it
//     would silently attach `else` to the wrong `if`). The standard idiom is
//     `do { ... } while (false)`, which is one statement and still requires
//     the caller's semicolon.
//
//  RELEASE BEHAVIOUR: in a release build ENGINE_ASSERT expands to
//  `do { } while (false)`. The expression is not named anywhere in the
//  expansion, so it is not evaluated and generates no code. Verified by
//  reading the preprocessed output (`/EP`, or `-E`) and by disassembling the
//  containing function - see docs/week03-shutdown-log.md section 3.
//
//  NEVER put anything that must happen inside an assert. In release it
//  vanishes. Use ENGINE_VERIFY when the expression has a side effect you need.
// =============================================================================

#include <engine/core/Types.h>

namespace eng::detail {

// Reports a failed assertion - expression text, file, line, optional message -
// then breaks into the debugger and halts.
//
// It writes to BOTH the logger and stderr. The logger is a subsystem, and a
// subsystem can be the thing that is broken; an assert that only works when
// the engine is healthy is not much of an assert.
[[noreturn]] void ReportAssertFailure(const char* expression,
                                      const char* file,
                                      int         line,
                                      const char* message);

// Same report, but returns instead of halting. Used by ENGINE_CHECK.
void ReportAssertFailureNonFatal(const char* expression,
                                 const char* file,
                                 int         line,
                                 const char* message);

} // namespace eng::detail

// -----------------------------------------------------------------------------
//  ENGINE_DEBUG_BREAK - stop in the debugger if one is attached.
//
//  Worth the lookup rather than calling abort(): abort() tells you the program
//  died, a debugger break tells you the call stack that got you there, which is
//  the question you actually have.
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
    #define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define ENGINE_DEBUG_BREAK() __builtin_trap()
#else
    #include <csignal>
    #define ENGINE_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

// `ENGINE_ASSERTS_ENABLED` is the single switch. NDEBUG is what CMake defines
// in Release and RelWithDebInfo.
#if defined(NDEBUG)
    #define ENGINE_ASSERTS_ENABLED 0
#else
    #define ENGINE_ASSERTS_ENABLED 1
#endif

#if ENGINE_ASSERTS_ENABLED

    // Fatal in debug, compiled out entirely in release.
    #define ENGINE_ASSERT_MSG(expr, msg)                                        \
        do {                                                                    \
            if (!(expr)) {                                                      \
                ::eng::detail::ReportAssertFailure(#expr, __FILE__, __LINE__,   \
                                                   (msg));                      \
            }                                                                   \
        } while (false)

    // Reports and keeps going. For "this should not happen, but the frame can
    // still finish" - a stale handle, a malformed row in a data file.
    #define ENGINE_CHECK_MSG(expr, msg)                                          \
        do {                                                                     \
            if (!(expr)) {                                                       \
                ::eng::detail::ReportAssertFailureNonFatal(#expr, __FILE__,      \
                                                           __LINE__, (msg));     \
            }                                                                    \
        } while (false)

#else

    #define ENGINE_ASSERT_MSG(expr, msg) do { } while (false)
    #define ENGINE_CHECK_MSG(expr, msg)  do { } while (false)

#endif

#define ENGINE_ASSERT(expr) ENGINE_ASSERT_MSG(expr, nullptr)
#define ENGINE_CHECK(expr)  ENGINE_CHECK_MSG(expr, nullptr)

// -----------------------------------------------------------------------------
//  WEEK 3 STRETCH 2 - the three variants, named so a reader can tell them
//  apart without reading their definitions.
//
//    ENGINE_ASSERT  - debug only, fatal.               (programmer error)
//    ENGINE_CHECK   - debug only, reports and returns. (recoverable oddity)
//    ENGINE_VERIFY  - ALWAYS evaluates the expression; fatal only in debug.
//                     For checks that are cheap and whose failure is
//                     catastrophic, and for expressions with side effects.
//    ENGINE_FATAL   - unconditional, in every configuration.
// -----------------------------------------------------------------------------
#if ENGINE_ASSERTS_ENABLED
    #define ENGINE_VERIFY_MSG(expr, msg)                                        \
        do {                                                                    \
            if (!(expr)) {                                                      \
                ::eng::detail::ReportAssertFailure(#expr, __FILE__, __LINE__,   \
                                                   (msg));                      \
            }                                                                   \
        } while (false)
#else
    // Still evaluated - that is the whole difference from ENGINE_ASSERT.
    #define ENGINE_VERIFY_MSG(expr, msg) do { (void)(expr); } while (false)
#endif

#define ENGINE_VERIFY(expr) ENGINE_VERIFY_MSG(expr, nullptr)

#define ENGINE_FATAL(msg)                                                       \
    ::eng::detail::ReportAssertFailure("ENGINE_FATAL", __FILE__, __LINE__, (msg))
