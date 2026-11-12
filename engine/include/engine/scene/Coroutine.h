#pragma once

// ============================================================================
//  Coroutine.h - Unity's StartCoroutine, spelled with C++20's co_yield.
//
//      Coroutine Blink() {
//          while (true) {
//              flash->SetTint(Color::Red());
//              co_yield WaitForSeconds(0.5f);           // yield return new WaitForSeconds(.5f)
//              flash->SetTint(Color::White());
//              co_yield WaitForSecondsRealtime(0.5f);   // ignores timeScale
//              co_yield nullptr;                        // yield return null -> next frame
//              co_yield 0;                              // yield return 0    -> next frame
//              co_yield WaitForEndOfFrame();            // after every OnUpdate, after every draw
//          }
//      }
//
//      void OnStart() { StartCoroutine(Blink()); }
//
//  ==========================================================================
//  THERE IS NO THREAD, AND THERE MUST NOT BE ONE
//
//  This is resumed ON THE MAIN THREAD, from the same place OnUpdate runs -
//  which is what Unity does too: `yield return` suspends a state machine and
//  the player loop resumes it between updates. Nothing about a coroutine is
//  concurrent.
//
//  THERE ARE TWO RESUME POINTS IN A DRAWN FRAME, and any one coroutine uses
//  exactly one of them: the ordinary pass inside the per-frame script update,
//  and the end-of-frame pass in Engine::PresentFrame that only
//  WaitForEndOfFrame uses. Both are on this thread and both are once a frame.
//
//  That is not an implementation detail to be optimised later. A coroutine
//  body moves transforms, reads components, spawns prefabs and destroys
//  entities, and none of that is safe from another thread - this engine has
//  DeferredOps because it is not even safe from the MAIN thread partway
//  through a frame. A coroutine may do exactly what OnUpdate may do, no more
//  and no less, and that equivalence is the whole value of it.
//
//  ==========================================================================
//  WHY co_yield AND NOT co_await
//
//  Because Unity's keyword is `yield`. A script author transliterating
//  `yield return new WaitForSeconds(.5f)` writes `co_yield WaitForSeconds(0.5f)`
//  and is right the first time.
//
//  It is also less machinery: co_yield needs ONE function per waitable
//  (promise_type::yield_value); co_await needs three (await_ready,
//  await_suspend, await_resume) and buys a result value that `yield return`
//  never had. co_await is left deliberately unused, so it is still free for
//  something that genuinely produces a value later.
//
//  ==========================================================================
//  WHAT A WAIT MEASURES
//
//    WaitForSeconds          GAME time. Stops dead at timeScale 0 and while
//                            the game is paused, which is what makes it the
//                            right one for anything the player can see.
//    WaitForSecondsRealtime  REAL time. Ignores timeScale, the same split
//                            Time::DeltaTime and Time::UnscaledDeltaTime make.
//    nullptr / 0             Next frame.
//    WaitForEndOfFrame()     NO TIME AT ALL. Still THIS frame, after every
//                            script's OnUpdate and after everything has been
//                            drawn. Read the section on it below before
//                            reaching for it: it is an ordering primitive, it
//                            is not Unity's, and nothing drawn there is seen.
//
//  NONE OF THEM TICK WHILE THE GAME IS PAUSED, the realtime one included, and
//  that is a real difference from Unity. Both resume passes sit behind the same
//  gate, which the engine decides ONCE in Engine::Simulate and remembers: while
//  the clock is paused with no simulation steps asked for, neither pass runs.
//  A paused game is frozen, and a coroutine is part of the game.
//
//  A DISABLED ENTITY PAUSES ITS COROUTINES; it does not lose them. Unity
//  destroys a GameObject's coroutines when it is deactivated. This engine's
//  own rule for a switched-off entity is that it pauses and nothing is torn
//  down, which the component honours everywhere else, so coroutines follow
//  the local rule rather than Unity's. Switch the entity back on and the
//  coroutine carries on from where it was.
//
//  ==========================================================================
//  WaitForEndOfFrame - WHAT IT IS, AND WHAT IT IS NOT
//
//  It resumes at the one moment per frame that deserves the name: immediately
//  before the finished picture is shown, from Engine::PresentFrame, which the
//  editor and the standalone player each call exactly once a frame. It is
//  therefore the SAME INSTANT IN BOTH, which is the whole reason it is worth
//  having - and the reason it is not hung off a render stage, which the editor
//  would run twice.
//
//  WHAT IT BUYS IS ORDERING. `co_yield nullptr` resumes a coroutine inside its
//  OWN component's frame tick, so it lands after that component's OnUpdate but
//  may still land BEFORE a later component's. An end-of-frame resume is after
//  every script's OnUpdate, without exception. Reach for it when a coroutine
//  reads something any script might still have been changing this frame - a
//  score several objects add to, a flag another script raises in its OnUpdate -
//  where the answer otherwise depends on the order things were attached in.
//
//  THE PICTURE IS ALREADY FINISHED, and that is the part to understand:
//  NOTHING DRAWN HERE BELONGS TO THIS FRAME. Both hosts have drawn everything
//  by the time this runs, and Gizmos::EndFrame has already aged the gizmo
//  queue, so a Gizmos call from an end-of-frame resume appears on the NEXT
//  frame - in the editor and in the player alike, so it is not an editor
//  quirk. A direct Renderer:: call is worse than late: it lands on top of
//  everything already drawn, which in the editor means on top of the editor's
//  own interface. Draw from OnUpdate.
//
//  WHICH MEANS IT IS NOT DOING UNITY'S JOB. Unity's WaitForEndOfFrame exists so
//  ScreenCapture and Texture2D.ReadPixels can read the finished back buffer,
//  and this engine has no framebuffer readback at all. When it gains one this
//  is the hook that will carry it, and none of the semantics above will have to
//  change. Until then: use it for ordering, not for pixels.
//
//  IT PROMISES A PHASE, NOT A PRIORITY. "After every OnUpdate" is exact and
//  host-independent. Which of two coroutines both waiting here goes first is
//  not: that is the order their components were attached in, which is an
//  accident of the scene file, is reset wholesale by every script rebuild, and
//  has no API to read or set. Use this to be after EVERYBODY, never to be
//  before one particular script.
//
//  Time::DeltaTime() answers the SAME NUMBER OnUpdate saw this frame, because
//  the frame is over and the clock has not moved on. So a coroutine that
//  resumes in both passes of one frame and adds DeltaTime() each time HAS
//  COUNTED THAT FRAME TWICE. The end of a frame is not a new slice of time.
//
//  AND IT DOES NOT RUN ON A FRAME THAT DID NOT UPDATE. The editor sitting in
//  edit mode runs no script updates, so it reaches no ends of frames either -
//  the same gate, decided once, that OnUpdate is behind. See Engine::Simulate.
//
//  ==========================================================================
//  LIFETIME - the thing to understand before writing one
//
//  A coroutine belongs to the BEHAVIOUR that started it and dies with it:
//  when the entity is destroyed, when the component is removed, when the
//  scene is replaced or reloaded, when play stops, on every undo step, and on
//  every script rebuild. There is no way to outlive your own script, which
//  matters because a coroutine that is a member function holds `this` inside
//  its frame - an orphaned one would be a dangling `this`, not merely a leak.
//
//  A Coroutine that is never started destroys itself. `Blink();` on its own
//  line runs nothing at all and frees the frame immediately, so a forgotten
//  StartCoroutine is a no-op rather than a leak - and [[nodiscard]] makes the
//  compiler mention it.
//
//  DO NOT destroy your own entity from inside a coroutine with
//  Scene::DestroyEntityImmediate - the frame being resumed would be freed
//  underneath the resume. That is the same rule Scene.h already states for
//  every other piece of game code, for the same reason, and the end-of-frame
//  pass is a SECOND place it can now be broken from.
//
//  Use DestroySelf(). From an ordinary resume it is carried out later the same
//  frame, at stage 600. FROM AN END-OF-FRAME RESUME IT IS CARRIED OUT ON THE
//  NEXT FRAME, because the queue is drained inside Engine::Simulate and the
//  end-of-frame pass runs after that - so the entity gets one more complete
//  frame first, OnUpdate and ordinary coroutine resumes included. Harmless for
//  a dying object; worth knowing if its last act counts something.
// ============================================================================

#include <coroutine>
#include <cstddef>
#include <cstdint>

namespace eng {

// Unity: yield return new WaitForSeconds(0.5f)
struct WaitForSeconds {
    explicit WaitForSeconds(float seconds) : seconds(seconds) {}
    float seconds;
};

// Unity: yield return new WaitForSecondsRealtime(0.5f)
struct WaitForSecondsRealtime {
    explicit WaitForSecondsRealtime(float seconds) : seconds(seconds) {}
    float seconds;
};

// Unity: yield return new WaitForEndOfFrame()
//
// EMPTY, AND THE PARENTHESES ARE THE POINT. `co_yield WaitForEndOfFrame()` is a
// value; `co_yield WaitForEndOfFrame` names a type and does not compile, which
// is an error a reader can act on. It takes no argument on purpose - the end of
// the frame is a moment, not a length of time - and asking for one says so,
// because a float will not convert to it.
struct WaitForEndOfFrame {};

namespace detail {
// Always false, but only when instantiated - which is what lets the catch-all
// yield_value below produce its message instead of a wall of template noise.
template <class>
inline constexpr bool kNotYieldable = false;
}   // namespace detail

class ScriptComponent;

// What StartCoroutine hands back, for StopCoroutine. An ID and not a pointer,
// for the reason every handle in this engine is one: a behaviour and its
// frames are replaced wholesale on every rebuild. Ids are never reused, so
// stopping one that already finished is a no-op rather than a wrong hit.
enum class CoroutineId : std::uint64_t { None = 0 };

class [[nodiscard]] Coroutine {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        // APPENDED TO, NEVER REORDERED. This promise lives in a coroutine frame
        // allocated inside the SCRIPT'S library, while the engine reads
        // promise().wait through the handle - so the numbering crosses the
        // library boundary, for the same reason ScriptFieldType is append-only.
        enum class Wait : unsigned char { NextFrame, Scaled, Unscaled, EndOfFrame };

        Wait  wait      = Wait::NextFrame;
        float remaining = 0.0f;

        Coroutine get_return_object() { return Coroutine{Handle::from_promise(*this)}; }

        // NOTHING RUNS UNTIL IT IS STARTED. Calling Blink() builds the frame
        // and stops; StartCoroutine resumes it once straight away, which is
        // what makes Unity's "the body begins at StartCoroutine" true here.
        // It also means a Coroutine nobody started has no half-done side
        // effect to explain.
        std::suspend_always initial_suspend() noexcept { return {}; }

        // ALSO suspend_always, and it is load-bearing. suspend_never frees the
        // frame the instant the body returns, leaving the handle the component
        // holds pointing at nothing. Suspended at the end, the frame stays
        // valid, done() answers true, and the resume pass destroys it.
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        // Logged and swallowed - NEVER rethrown. An exception escaping here
        // would cross back into engine code from the script's DLL during a
        // resume, and the frame is already unwound. Defined out of line so
        // this header pulls in no logging.
        void unhandled_exception() noexcept;

        std::suspend_always yield_value(WaitForSeconds wait_) noexcept {
            wait      = Wait::Scaled;
            remaining = wait_.seconds;
            return {};
        }
        std::suspend_always yield_value(WaitForSecondsRealtime wait_) noexcept {
            wait      = Wait::Unscaled;
            remaining = wait_.seconds;
            return {};
        }

        // Unity's `yield return null`.
        std::suspend_always yield_value(std::nullptr_t) noexcept {
            wait      = Wait::NextFrame;
            remaining = 0.0f;
            return {};
        }

        // Unity's `yield return new WaitForEndOfFrame()`. See the section on it
        // above: it is for ordering, and the picture is already drawn.
        std::suspend_always yield_value(WaitForEndOfFrame) noexcept {
            wait      = Wait::EndOfFrame;
            remaining = 0.0f;
            return {};
        }

        // Unity's `yield return 0`. Both spellings resolve without ambiguity:
        // for `co_yield 0` this is an exact match and nullptr_t is only
        // reachable through a pointer conversion; for `co_yield nullptr` the
        // reverse, and int is not viable at all. Checked by compiling all four
        // before any of this was written.
        //
        // THE NUMBER IS IGNORED, exactly as Unity ignores it - `yield return 5`
        // is next frame there too, which is the most misread thing about Unity
        // coroutines. Anything but 0 says so once, out of line.
        std::suspend_always yield_value(int frames) noexcept;

        // EVERYTHING ELSE IS A NAMED COMPILE ERROR, which is where this
        // departs from Unity on purpose: Unity treats an unrecognised object
        // as "next frame", so `yield return 0.5f` silently waits one frame
        // instead of half a second. Here it does not build.
        template <class T>
        std::suspend_always yield_value(const T&) {
            static_assert(detail::kNotYieldable<T>,
                          "co_yield takes WaitForSeconds(seconds), "
                          "WaitForSecondsRealtime(seconds), WaitForEndOfFrame(), "
                          "nullptr, or 0. A bare number is not a wait - "
                          "`co_yield 0.5f` looks like half a second and is not; "
                          "write co_yield WaitForSeconds(0.5f). There is no "
                          "WaitUntil or WaitWhile, and a coroutine cannot yield "
                          "another one yet - see docs/scripting-reference.md, "
                          "Coroutines.");
            return {};
        }
    };

    Coroutine(const Coroutine&)            = delete;
    Coroutine& operator=(const Coroutine&) = delete;

    Coroutine(Coroutine&& other) noexcept : m_handle(other.m_handle) { other.m_handle = {}; }

    Coroutine& operator=(Coroutine&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle       = other.m_handle;
            other.m_handle = {};
        }
        return *this;
    }

    // A coroutine nobody started frees its own frame. Nothing ran, so there is
    // nothing to answer for but the memory.
    ~Coroutine() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

private:
    friend class ScriptComponent;

    explicit Coroutine(Handle handle) : m_handle(handle) {}

    // Hands the frame to the component, which owns it from then on. Private,
    // and ScriptComponent is the only friend, so a script can never hold a
    // raw handle and resume or destroy one behind the engine's back.
    Handle Release() noexcept {
        Handle handle = m_handle;
        m_handle      = {};
        return handle;
    }

    Handle m_handle{};
};

}   // namespace eng
