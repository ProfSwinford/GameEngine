// WEEK 3 - assertion reporting. See Assert.h.
//
// ReportAssertFailure does three things, in this order:
//   1. Emits the failure through the logger at Fatal, if the logger is up.
//   2. ALSO emits it to stderr directly. The logger is a subsystem, and
//      subsystems can be the thing that is broken. An assert that only works
//      when the engine is healthy is not much of an assert.
//   3. Breaks into the debugger, then halts.
//
// (3) is a debugger break rather than abort() on purpose: abort() tells you the
// program died, a break tells you the call stack that got you there.

#include <engine/core/Assert.h>
#include <engine/core/Log.h>

#include <cstdio>
#include <cstdlib>

namespace eng::detail {
namespace {

void Emit(const char* expression, const char* file, int line, const char* message,
          const char* prefix) {
    // stderr first and unconditionally, because this path must work when the
    // logger does not.
    std::fprintf(stderr, "\n%s: %s\n  at %s:%d\n", prefix, expression, file, line);
    if (message != nullptr) {
        std::fprintf(stderr, "  %s\n", message);
    }
    std::fflush(stderr);

    if (Log::IsInitialised()) {
        if (message != nullptr) {
            ENGINE_LOG_FATAL(Channels::kCore, "{}: {} at {}:{} - {}", prefix, expression,
                             file, line, message);
        } else {
            ENGINE_LOG_FATAL(Channels::kCore, "{}: {} at {}:{}", prefix, expression, file,
                             line);
        }
        Log::Flush();
    }
}

} // namespace

void ReportAssertFailure(const char* expression, const char* file, int line,
                         const char* message) {
    Emit(expression, file, line, message, "ASSERTION FAILED");
    ENGINE_DEBUG_BREAK();
    // Reached only when no debugger is attached and the break did not halt.
    std::abort();
}

void ReportAssertFailureNonFatal(const char* expression, const char* file, int line,
                                 const char* message) {
    Emit(expression, file, line, message, "CHECK FAILED");
}

} // namespace eng::detail
