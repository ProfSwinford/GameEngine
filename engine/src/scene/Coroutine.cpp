// ============================================================================
//  Coroutine.cpp - the two pieces of eng::Coroutine that are not inline.
//
//  Both are out of line so that scene/Coroutine.h pulls in <coroutine> and
//  nothing else. It is included by every script through Engine.h, and a header
//  on that path earns its includes.
// ============================================================================

#include <engine/scene/Coroutine.h>

#include <engine/core/Log.h>

namespace eng {

void Coroutine::promise_type::unhandled_exception() noexcept {
    // SWALLOWED, NEVER RETHROWN, and the alternative is worse than it sounds.
    //
    // The frame has already unwound by the time this runs, and the resume that
    // reached it came from engine code calling into the script's DLL. Letting
    // the exception continue would carry it back across that boundary mid-walk
    // through the component's coroutine list, which is a crash with a stack
    // that names none of the script that caused it.
    //
    // So the coroutine stops here: it does not resume again, the frame is
    // destroyed by the next resume pass because done() answers true, and the
    // Console says which it was. That is the same bargain the hook dispatch
    // makes - one misbehaving script must not take the editor with it.
    try {
        throw;
    } catch (const std::exception& error) {
        ENGINE_LOG_ERROR(Channels::kGame,
                         "a coroutine threw '{}' and has been stopped. The frame is "
                         "discarded; nothing else in the script is affected.",
                         error.what());
    } catch (...) {
        ENGINE_LOG_ERROR(Channels::kGame,
                         "a coroutine threw something that is not a std::exception and "
                         "has been stopped. The frame is discarded; nothing else in the "
                         "script is affected.");
    }
}

std::suspend_always Coroutine::promise_type::yield_value(int frames) noexcept {
    wait      = Wait::NextFrame;
    remaining = 0.0f;

    // `yield return 0` is the spelling worth having. Any OTHER number reads as
    // "wait this many frames" and does not mean that - not here and not in
    // Unity, where `yield return 5` also waits exactly one frame. Said once,
    // because a coroutine yielding in a loop would otherwise say it forever.
    static bool reported = false;
    if (frames != 0 && !reported) {
        reported = true;
        ENGINE_LOG_WARN(Channels::kGame,
                        "a coroutine wrote `co_yield {}`, which waits ONE frame - the "
                        "number is ignored, exactly as Unity ignores it. For a count of "
                        "frames, loop on `co_yield 0`; for a length of time, "
                        "`co_yield WaitForSeconds({}.0f)`. Said once.",
                        frames, frames);
    }
    return {};
}

}   // namespace eng
