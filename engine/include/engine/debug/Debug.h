#pragma once

// ============================================================================
//  Debug.h - the things you reach for when a game is misbehaving.
//
//  Unity's Debug class, with Unity's names, for the same reason Unity has one:
//  printing a line and drawing a shape are the two things every bug hunt
//  starts with, and both should be one short call from anywhere.
//
//      Debug::Log("player spawned");
//      Debug::Log("hp {} of {}", hp, maxHp);
//      Debug::LogWarning("no target to shoot at");
//      Debug::DrawLine(transform->WorldPosition(), target, Color::Red());
//      Debug::Watch("speed", body->Velocity().Length());
//
//  Nothing here is new machinery. Every line above ends up in eng::Log or
//  eng::Gizmos, which already existed and already work. This file exists
//  because those two are correct and not friendly, and the gap between the
//  two is where a debugging session is actually spent.
//
//  ==========================================================================
//  WHAT WAS WRONG WITH THE MACROS
//
//  The log is reached by three macros, and every one of them demands a
//  channel:
//
//      ENGINE_LOG_INFO(eng::Channels::kGame, "collected '{}'", name);
//
//  A channel is the right idea - it is what the Console's filter buttons are
//  built out of, and engine code that says "Physics" or "Resource" earns its
//  keep the first time somebody needs to see only one of them. But a game
//  script has exactly one channel it could possibly mean, so naming it on
//  every call is ceremony: twenty-eight characters of it, in front of the
//  message, every single time.
//
//  So Debug::Log takes no channel and uses "Game". The channel did not go
//  away - when you want another one it is one call away:
//
//      Debug::In("AI").LogWarning("no path from {} to {}", from, to);
//
//  and the macros are still there, unchanged, for the engine's own code,
//  which has twelve channels and means a different one on almost every line.
//
//  ==========================================================================
//  ONE THING THE MACROS DO THAT A FUNCTION CANNOT
//
//  A macro can skip evaluating its arguments. These functions cannot:
//
//      Debug::Log("{}", DescribeEverything());   // always runs
//
//  The FORMATTING is still skipped when the message is below the threshold -
//  the level is checked before std::format runs, exactly as the macro checks
//  it before std::format runs - so a filtered message costs a call and a
//  comparison. But the ARGUMENTS are evaluated either way, because that is
//  what calling a function means. When an argument is genuinely expensive,
//  ask first:
//
//      if (Debug::WillLog()) {
//          Debug::Log("{}", DescribeEverything());
//      }
//
//  That is the only reason left to reach past this file for logging.
//
//  ==========================================================================
//  THREE SHAPES OF Log, AND WHY THERE ARE THREE
//
//      Debug::Log("ready");                      // text, printed as it is
//      Debug::Log("hp {} of {}", hp, maxHp);     // a format and its values
//      Debug::Log(transform->WorldPosition());   // one value, as {}
//
//  The first two are different functions on purpose. The second checks the
//  format string against its arguments AT COMPILE TIME, which is the whole
//  reason std::format was chosen over printf - and that check needs the
//  string to be a literal. The first takes any string at all, including one
//  built a moment ago, and prints it verbatim. So:
//
//      Debug::Log(whatWentWrong);         // fine, any std::string
//      Debug::Log("50% {done}");          // fine, braces are just text
//      Debug::Log(whatWentWrong, value);  // does NOT compile, and must not:
//                                         // the compiler cannot check a
//                                         // format string it cannot see
//
//  The third shape is Unity's Debug.Log(object). It takes any single value
//  std::format knows how to print, so "what IS this number right now" needs
//  no format string at all. Every built-in type works, and so does every
//  engine type with a formatter - Vec2 and EntityId among them.
//
//  ==========================================================================
//  THE TWO MISTAKES THE COMPILER CANNOT CATCH, AND IT IS WORTH KNOWING WHICH
//
//  The format shape is checked at compile time, and thoroughly: a wrong
//  argument count, a wrong type, a malformed specification and a format
//  string that is not a literal are all build errors. Two things are not.
//
//    1. A FORGOTTEN ARGUMENT IS NOT AN ERROR.
//
//           Debug::Log("x={}");          // prints  x={}
//
//       With nothing after it, that is the plain-string overload doing
//       exactly its job - printing the text as written - and no overload
//       trick can tell it apart from somebody who meant the braces. The
//       macros do catch this, because a macro has no overload to fall back
//       on. It is the one thing they are stricter about.
//
//    2. SPARE ARGUMENTS ARE IGNORED.
//
//           Debug::Log("{}", 1, 2);      // prints  1
//
//       That is std::format's own behaviour, not this file's.
//
//  ==========================================================================
//  WHAT CANNOT BE PRINTED
//
//  std::format has no formatter for an ENUM, scoped or not - an unscoped one
//  does not quietly promote to int - and none for an ordinary pointer. So:
//
//      Debug::Log(facing);                        // does NOT compile
//      Debug::Log(static_cast<int>(facing));      // fine
//      Debug::Log(enemy);                         // Entity*: does NOT compile
//      Debug::Log(enemy->Name());                 // fine
//
//  The error for a type with no formatter names the WRONG overload if it is
//  left to the compiler - "cannot convert argument 1 ... to
//  std::string_view", because the value shape has dropped out of the
//  candidate set and only the string one is left to complain. So this file
//  catches that case itself and says what is really wrong; see the note
//  beside the three catch-alls below.
//
//  ==========================================================================
//  LogOnce AND LogEvery, WHICH UNITY DOES NOT HAVE
//
//  A message logged from OnUpdate arrives sixty times a second, which is not
//  logging; it is a denial of service against the Console. Every game ends up
//  writing a bool or a timer to stop it, so the two are here instead:
//
//      Debug::LogErrorOnce("no Rigidbody2D on '{}'", name);
//      Debug::LogEvery(0.5f, "pos {}", transform->WorldPosition());
//
//  THEY ARE KEYED BY THE FORMAT STRING, NOT BY THE FINISHED MESSAGE. The
//  first line prints once however many entities are missing a body; the
//  second prints twice a second however far the position moved. That is the
//  useful behaviour - a throttle keyed on the text would not throttle
//  anything, because the text is the part that keeps changing - and it is the
//  surprising one, so it is written down here rather than left to be found.
//
//  Two calls that share a format string therefore share a key, and count as
//  one message. Give them different words if you need to tell them apart.
//
//  The keys are forgotten whenever a scene is replaced, which includes
//  pressing Play and pressing Stop. A once-only message is a once-PER-PLAY
//  message, because the alternative is a second play session with no output.
//
//  ==========================================================================
//  THE DRAWING IS Gizmos, AND THAT HAS THREE CONSEQUENCES
//
//    * WHERE IT SHOWS. The Scene view draws gizmos and so does a built game.
//      The editor's GAME view does not, because gizmos are not part of the
//      game. Unity does the same thing, with the same button in the same
//      place - so while you are in the editor, look at the Scene view.
//
//    * HOW LONG IT LASTS. The last argument is a lifetime in seconds and it
//      defaults to 0, meaning this frame only. A shape drawn from an update
//      has to be drawn again next update. Pass a longer life to mark
//      something that happened once - where a bullet hit, which cell a path
//      went through - and the mark stays while you go and look at it.
//
//    * WHAT CAN SWITCH IT OFF. The Scene view's Gizmos menu, as a group.
//      These go in GizmoCategory::Default, with the shapes other game code
//      draws, rather than in the engine's Grid/Axes/Bounds/Colliders groups.
//
//  Positions are in WORLD units, which in this project means pixels.
//
//  AND ONE THING THAT WILL CATCH YOU ONCE: a lifetime is counted in REAL
//  seconds, not game seconds - Engine::RenderFrame ages the queue with the
//  clock's real delta. So a three-second marker goes on expiring while the
//  game is paused, including while it is paused by Debug::Break below. Mark
//  the spot, then look at it; do not pause and then go for coffee.
//
//  ==========================================================================
//  Watch AND ScreenText: THE NUMBERS, ON THE SCREEN
//
//  The Console tells you what happened. A number that changes every frame is
//  not something that happened - it is a state, and reading a state in the
//  Console means reading a thousand lines of it. So it goes on the screen:
//
//      void OnUpdate() {
//          Debug::Watch("fps", 1.0f / Time::UnscaledDeltaTime());
//          Debug::Watch("pos", transform->WorldPosition());
//          Debug::Watch("grounded", m_grounded);
//      }
//
//  Three lines, top-left of the view, replaced every frame. Each call takes
//  the next line down, and the line counter goes back to the top when a new
//  frame starts - so the order on screen is the order of the calls, and
//  nothing has to be registered, cleared or owned by anybody.
//
//  Call it once per frame from OnUpdate. From OnFixedUpdate it prints twice
//  in a frame that stepped twice, which is honest and not useful.
//
//  THIS IS A GIZMO AND NOT A UI. It appears exactly where Debug::DrawLine
//  appears and disappears with it. The in-game interface - a score, a health
//  bar, anything a player is meant to see - is scene/UI.h, and it is a
//  different thing on purpose; the long note at the top of that file says
//  why, and this is the debug overlay it is contrasting itself with.
//
//  ==========================================================================
//  NOTHING IS APPEARING. IN ORDER OF HOW OFTEN IT IS THIS.
//
//  A MESSAGE THAT DOES NOT SHOW UP ANYWHERE - not the Console, not the
//  terminal, not logs/engine.log - is the THRESHOLD. config/engine.json's
//  logging.threshold hides everything less important than the level it names,
//  and the test happens before the message is recorded, so a threshold of
//  Error makes Debug::Log and Debug::LogWarning produce nothing at all. It
//  ships at Info; somebody may have raised it. Debug::WillLog() answers.
//
//  A SHAPE OR A WATCH LINE THAT DOES NOT SHOW UP is one of four things, all
//  of them about gizmos rather than about this file:
//
//    1. You are looking at the GAME view. It draws the world without gizmos.
//       Look at the Scene view, or run the built game.
//    2. The Scene view is not visible. In the default layout Scene and Game
//       are two tabs of one panel, and the hidden one does not draw at all.
//    3. The Scene view's Gizmos menu is unticked. It switches off the whole
//       Default category, which is where these land.
//    4. The script is not running. Scripts update in play mode only, so
//       nothing draws while you are building a scene.
//
//  ==========================================================================
//  WHAT IS DELIBERATELY NOT HERE
//
//  No LogFormat, LogWarningFormat or LogErrorFormat. Unity needs them because
//  C# Debug.Log takes a single object, so formatting had to be a separate
//  entry point. Debug::Log already formats, and Unity's {0}-style
//  placeholders are not std::format's {} ones - so a LogFormat here would be
//  a synonym that quietly changed what the braces mean.
//
//  No LogException. This engine does not use C++ exceptions.
//
//  No depthTest on the drawing calls. That is Unity's switch for drawing a
//  line behind 3D geometry, and there is no depth buffer here to test.
//
//  No assertion that stops the program. Assert LOGS, exactly as Unity's does;
//  see the note on it below.
//
//  ==========================================================================
//  ONE NOTE FOR ANYONE WHO HAS READ THE `Transform` STORY IN Engine.h
//
//  `Debug` is a global name in every script, because Engine.h ends with
//  `using namespace eng;`. A script that declares its own member or local
//  called `Debug` hides this one, exactly as ScriptBehaviour::Transform()
//  hides the `Transform` alias.
//
//  THE DIFFERENCE, AND IT IS THE WHOLE DIFFERENCE: `eng::Debug` really does
//  exist, so qualifying is a complete escape. `eng::Transform` does not,
//  which is what made that one a dead end.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Overlap.h>      // AABB, for the DrawBox that takes one
#include <engine/math/Vec2.h>
#include <engine/render/Renderer.h>   // Color

#include <concepts>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eng {

// ----------------------------------------------------------------------------
//  "std::format knows how to print a T, and T is not already a string."
//
//  This is what lets Debug::Log(value) - the one-value shape, Unity's
//  Debug.Log(object) - take an int, a float, a Vec2 or an EntityId while
//  leaving every string-like type to the plain std::string_view overload. Two
//  overloads that both matched a const char* would be ambiguous, and the
//  string one has to be the one that wins: it is the one that prints braces
//  as braces.
//
//  WHY IT IS WRITTEN THIS WAY. C++23 has std::formattable and this would be
//  one line. A project's scripts are compiled at /std:c++20 while the engine
//  is built at /std:c++latest, so every public header has to be C++20-clean.
//  The standard requires the formatter for a type it cannot print to be
//  DISABLED - not default-constructible, not assignable - so asking whether
//  that formatter is semiregular is asking whether the type is printable. It
//  is the pre-C++23 idiom for exactly this question.
// ----------------------------------------------------------------------------
template <class T>
concept DebugPrintable =
    !std::convertible_to<const std::remove_cvref_t<T>&, std::string_view> &&
    std::semiregular<std::formatter<std::remove_cvref_t<T>, char>>;

class Debug {
public:
    // ======================================================================
    //  THE CHANNEL
    // ======================================================================

    // Where an unchannelled message goes. "Game" - which is
    // eng::Channels::kGame, the name every ENGINE_LOG_* call in a script was
    // passing by hand.
    static std::string_view DefaultChannel();

    // Changes it for the whole program. Worth one call at start-up if your
    // game is big enough to want its own name among the Console's filter
    // buttons; not worth calling twice, because then what channel a message
    // landed on depends on when it was logged.
    //
    // A NEW NAME NEEDS NO REGISTERING. The Console builds its filter list out
    // of the names it has actually seen - see LogBuffer::Channels - so the
    // button appears the first time something is logged.
    static void SetDefaultChannel(std::string_view channel);

    // ======================================================================
    //  LOGGING. Three shapes each; see the note at the top of the file.
    // ======================================================================

    static void Log(std::string_view message) {
        Emit(LogLevel::Info, message);
    }
    static void LogWarning(std::string_view message) {
        Emit(LogLevel::Warning, message);
    }
    static void LogError(std::string_view message) {
        Emit(LogLevel::Error, message);
    }

    template <class A, class... Rest>
    static void Log(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        Formatted(LogLevel::Info, fmt, std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogWarning(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        Formatted(LogLevel::Warning, fmt, std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogError(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        Formatted(LogLevel::Error, fmt, std::forward<A>(a), std::forward<Rest>(rest)...);
    }

    template <DebugPrintable T>
    static void Log(const T& value) { Printed(LogLevel::Info, value); }
    template <DebugPrintable T>
    static void LogWarning(const T& value) { Printed(LogLevel::Warning, value); }
    template <DebugPrintable T>
    static void LogError(const T& value) { Printed(LogLevel::Error, value); }

    // ------------------------------------------------------------------
    //  AND THE SAME THREE FOR A VALUE THAT CANNOT BE PRINTED, so that the
    //  error says so.
    //
    //  Left to the compiler, Debug::Log(facing) is reported as
    //
    //      error C2664: 'void eng::Debug::Log(std::string_view)': cannot
    //      convert argument 1 from 'Facing' to 'std::string_view'
    //
    //  which names an overload nobody meant to call. The constrained value
    //  shape has dropped out of the candidate set, leaving the string one as
    //  the only survivor to complain, and a reader quite reasonably concludes
    //  that Log wants a string. These three catch what is left and put the
    //  real answer in the message, which is the same job the static_asserts
    //  in scene/ScriptHooks.h do for a mistyped hook.
    //
    //  NOT REPEATED ON Channel BELOW, on purpose: this is where the mistake
    //  gets made, and a second copy of the same sentence is not twice as
    //  useful.
    // ------------------------------------------------------------------
    template <class T>
        requires (!std::convertible_to<const std::remove_cvref_t<T>&, std::string_view> &&
                  !DebugPrintable<T>)
    static void Log(const T&) { Unprintable<T>(); }
    template <class T>
        requires (!std::convertible_to<const std::remove_cvref_t<T>&, std::string_view> &&
                  !DebugPrintable<T>)
    static void LogWarning(const T&) { Unprintable<T>(); }
    template <class T>
        requires (!std::convertible_to<const std::remove_cvref_t<T>&, std::string_view> &&
                  !DebugPrintable<T>)
    static void LogError(const T&) { Unprintable<T>(); }

    // ======================================================================
    //  ONCE, AND NOT MORE OFTEN THAN. Keyed by the format string - see the
    //  note at the top of the file, because that part is surprising.
    // ======================================================================

    static void LogOnce(std::string_view message) {
        Once(LogLevel::Info, message, message);
    }
    static void LogWarningOnce(std::string_view message) {
        Once(LogLevel::Warning, message, message);
    }
    static void LogErrorOnce(std::string_view message) {
        Once(LogLevel::Error, message, message);
    }

    template <class A, class... Rest>
    static void LogOnce(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        FormattedOnce(LogLevel::Info, fmt,
                      std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogWarningOnce(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        FormattedOnce(LogLevel::Warning, fmt,
                      std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogErrorOnce(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        FormattedOnce(LogLevel::Error, fmt,
                      std::forward<A>(a), std::forward<Rest>(rest)...);
    }

    static void LogEvery(float seconds, std::string_view message) {
        Every(LogLevel::Info, seconds, message, message);
    }
    static void LogWarningEvery(float seconds, std::string_view message) {
        Every(LogLevel::Warning, seconds, message, message);
    }
    static void LogErrorEvery(float seconds, std::string_view message) {
        Every(LogLevel::Error, seconds, message, message);
    }

    template <class A, class... Rest>
    static void LogEvery(float seconds, std::format_string<A, Rest...> fmt,
                         A&& a, Rest&&... rest) {
        FormattedEvery(LogLevel::Info, seconds, fmt,
                       std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogWarningEvery(float seconds, std::format_string<A, Rest...> fmt,
                                A&& a, Rest&&... rest) {
        FormattedEvery(LogLevel::Warning, seconds, fmt,
                       std::forward<A>(a), std::forward<Rest>(rest)...);
    }
    template <class A, class... Rest>
    static void LogErrorEvery(float seconds, std::format_string<A, Rest...> fmt,
                              A&& a, Rest&&... rest) {
        FormattedEvery(LogLevel::Error, seconds, fmt,
                       std::forward<A>(a), std::forward<Rest>(rest)...);
    }

    // Forgets every Once and Every key, so the next call to each logs again.
    //
    // The engine calls this whenever a scene is replaced - which is also what
    // pressing Play and pressing Stop do - so a once-only message is really a
    // once-per-play message. See Engine::ForgetTheOldScene.
    static void ResetThrottles();

    // How many distinct keys are being remembered. For tests, and for
    // noticing a key set that is growing when it should not be - which means
    // something is calling LogOnce with a string it built rather than wrote.
    static std::size_t ThrottleKeyCount();

    // ======================================================================
    //  ASSERT. Unity's Debug.Assert, with Unity's behaviour: a false
    //  condition is LOGGED AS AN ERROR AND NOTHING ELSE HAPPENS.
    //
    //  It does not stop the program, it does not break into a debugger, and
    //  it is not compiled out of a release build. That is deliberate, and it
    //  is the opposite of <cassert> on both counts: an assert that kills a
    //  game is one nobody dares leave in, and one that vanishes from the
    //  build players actually run is one that never catches anything. This
    //  one stays, and it tells the Console.
    //
    //  THE CONDITION IS ALWAYS EVALUATED, for the same reason the log's
    //  arguments always are: this is a function.
    // ======================================================================
    static void Assert(bool condition) {
        if (!condition) {
            Emit(LogLevel::Error, "assertion failed");
        }
    }
    static void Assert(bool condition, std::string_view message) {
        if (!condition) {
            Emit(LogLevel::Error, message);
        }
    }
    template <class A, class... Rest>
    static void Assert(bool condition, std::format_string<A, Rest...> fmt,
                       A&& a, Rest&&... rest) {
        if (!condition) {
            Formatted(LogLevel::Error, fmt,
                      std::forward<A>(a), std::forward<Rest>(rest)...);
        }
    }

    // ======================================================================
    //  A CHANNEL OF YOUR OWN, when one name for everything is not enough.
    //
    //      Debug::In("AI").Log("{} is hunting", name);
    //
    //  Or kept, when a whole file belongs to one part of the game:
    //
    //      const Debug::Channel log = Debug::In("AI");
    //      log.Log("...");
    //      log.LogWarning("...");
    //
    //  It OWNS its name rather than pointing at one, so the second form is
    //  safe even when the name was built rather than written out.
    //
    //  Only the three plain levels are here. Once and Every are not, because
    //  their keys are global and a channel would not change them - and a
    //  method that looked per-channel while sharing one key table would be
    //  worse than not having it.
    // ======================================================================
    class Channel {
    public:
        explicit Channel(std::string_view name) : m_name(name) {}

        std::string_view Name() const { return m_name; }

        void Log(std::string_view message) const {
            EmitIn(LogLevel::Info, m_name, message);
        }
        void LogWarning(std::string_view message) const {
            EmitIn(LogLevel::Warning, m_name, message);
        }
        void LogError(std::string_view message) const {
            EmitIn(LogLevel::Error, m_name, message);
        }

        template <class A, class... Rest>
        void Log(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) const {
            FormattedIn(LogLevel::Info, m_name, fmt,
                        std::forward<A>(a), std::forward<Rest>(rest)...);
        }
        template <class A, class... Rest>
        void LogWarning(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) const {
            FormattedIn(LogLevel::Warning, m_name, fmt,
                        std::forward<A>(a), std::forward<Rest>(rest)...);
        }
        template <class A, class... Rest>
        void LogError(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) const {
            FormattedIn(LogLevel::Error, m_name, fmt,
                        std::forward<A>(a), std::forward<Rest>(rest)...);
        }

        template <DebugPrintable T>
        void Log(const T& value) const { PrintedIn(LogLevel::Info, m_name, value); }
        template <DebugPrintable T>
        void LogWarning(const T& value) const { PrintedIn(LogLevel::Warning, m_name, value); }
        template <DebugPrintable T>
        void LogError(const T& value) const { PrintedIn(LogLevel::Error, m_name, value); }

    private:
        std::string m_name;
    };

    static Channel In(std::string_view channel) { return Channel{channel}; }

    // ======================================================================
    //  DRAWING. Queued as gizmos; see the note at the top of the file for
    //  where they show up and how long they last.
    //
    //  `duration` is in seconds, and 0 means this frame only.
    // ======================================================================

    // Unity's Debug.DrawLine. Two points in the world.
    static void DrawLine(Vec2 from, Vec2 to, Color color = Color::White(),
                         float duration = 0.0f);

    // Unity's Debug.DrawRay: a point and an OFFSET from it, not a point and a
    // destination. The length of `direction` is the length of the ray, which
    // is what makes `DrawRay(pos, velocity * 0.2f)` a useful picture of where
    // something is going.
    static void DrawRay(Vec2 origin, Vec2 direction, Color color = Color::White(),
                        float duration = 0.0f);

    // A line with a head on the far end. Unity has no equivalent on Debug,
    // and in two dimensions it is the difference between "these two things
    // are related" and "this one points at that one".
    static void DrawArrow(Vec2 from, Vec2 to, Color color = Color::White(),
                          float duration = 0.0f);

    static void DrawCircle(Vec2 centre, float radius, Color color = Color::White(),
                           float duration = 0.0f);

    // Centre and FULL size, like Unity's Gizmos.DrawWireCube - not a corner
    // and a size, and not half-extents.
    static void DrawBox(Vec2 centre, Vec2 size, Color color = Color::White(),
                        float duration = 0.0f);

    // The same thing for a box you already have, which is what the physics
    // queries and the colliders hand you.
    static void DrawBox(const AABB& box, Color color = Color::White(),
                        float duration = 0.0f);

    // A small cross on a point. `size` is the arm length in pixels. For
    // marking one place - a raycast hit, a spawn point - where a circle would
    // be bigger than the thing it is pointing at.
    static void DrawCross(Vec2 point, float size = 6.0f, Color color = Color::White(),
                          float duration = 0.0f);

    // Text in the WORLD, so it moves with the camera: a label over an enemy's
    // head, a damage number where the damage happened.
    //
    // A newline starts a new line here too, dropping by one line height in
    // WORLD units - so unlike the screen-space list, the spacing tightens and
    // widens as the camera zooms. There is no pixel measure to use: a world
    // gizmo is queued before anything knows which camera will draw it.
    static void DrawText(Vec2 worldPosition, std::string_view text,
                         Color color = Color::White(), float duration = 0.0f);

    // The same with a format. It is white and lasts one frame, because a
    // colour and a lifetime cannot follow a parameter pack - so when you want
    // either, format the text yourself and use the overload above.
    template <class A, class... Rest>
    static void DrawText(Vec2 worldPosition, std::format_string<A, Rest...> fmt,
                         A&& a, Rest&&... rest) {
        const std::string text =
            std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...);
        DrawText(worldPosition, std::string_view{text}, Color::White(), 0.0f);
    }

    // ======================================================================
    //  THE ON-SCREEN WATCH LIST. One line per call, top-left of the view,
    //  cleared and rebuilt every frame. See the note at the top of the file.
    // ======================================================================

    // `label: value`, which is what you almost always want.
    template <DebugPrintable T>
    static void Watch(std::string_view label, const T& value) {
        const std::string line = std::format("{}: {}", label, value);
        ScreenText(std::string_view{line});
    }
    static void Watch(std::string_view label, std::string_view value);

    // One line of whatever you like.
    //
    // A NEWLINE IN IT BECOMES A NEW LINE on screen, and takes its own slot in
    // the list. That is worth saying because the layer underneath cannot do
    // it: the gizmo text path draws one line and ignores an embedded newline,
    // so everything after it would land on top of the first. This splits.
    static void ScreenText(std::string_view line);

    template <class A, class... Rest>
    static void ScreenText(std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        const std::string line =
            std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...);
        ScreenText(std::string_view{line});
    }

    // Where the first line goes, in pixels from the top-left corner of the
    // view, and what colour the lines are. Set either once at start-up if the
    // default corner is in the way of something.
    static void  SetScreenOrigin(Vec2 topLeft);
    static Vec2  ScreenOrigin();
    static void  SetScreenColor(Color color);
    static Color ScreenColor();

    // How many lines have been queued this frame. Nothing in the engine reads
    // it; it is for tests, and for answering "did my watch call run at all" -
    // the same job Gizmos::QueuedCount does for shapes.
    static std::size_t ScreenLineCount();

    // Puts the cursor back to the top without waiting for the next frame.
    //
    // Nothing needs this during a game: the cursor resets itself when the
    // frame number changes. It is here because a test that drives the clock
    // by hand can run two cases inside one frame, and the second would
    // otherwise start its list halfway down the screen.
    static void ResetScreenLines();

    // ======================================================================
    //  THE EDITOR, AND WHAT KIND OF BUILD THIS IS
    // ======================================================================

    // Unity's Debug.Break: stops the game where it is, leaving everything
    // alive to be looked at. The editor's Pause button lights up and its Step
    // button then advances one frame at a time.
    //
    // IT DOES NOTHING IN A BUILT GAME, and that is deliberate rather than
    // unfinished: there is no Pause button in a shipped player, so pausing
    // there would be a hang with no way out. Unity's Break has the same
    // not-in-a-player behaviour for the same reason. A warning is logged
    // saying so, rather than nothing happening silently.
    static void Break();

    // Whether the editor is attached, as opposed to this being a built game.
    // What Break tests, exposed because a script sometimes wants to know -
    // Unity's Application.isEditor.
    static bool IsEditorAttached();

    // Empties the Console. Unity's Debug.ClearDeveloperConsole.
    //
    // The log FILE is not touched: logs/engine.log is the copy that survives
    // a crash, and a script that could erase it would be a script that could
    // destroy the evidence.
    static void ClearConsole();

    // Whether the engine was built with debugging on. Unity's
    // Debug.isDebugBuild.
    //
    // It is a function and not a constant on purpose. A project's scripts are
    // compiled separately from the engine, so a constant here would record
    // what the SCRIPT was built as, which is not the question; this answers
    // for the engine those scripts are loaded into.
    static bool IsDebugBuild();

    // Whether a message at this level would get through the threshold as
    // things stand. The guard to put in front of an expensive argument; see
    // the note at the top of the file.
    static bool WillLog(LogLevel level = LogLevel::Info);

private:
    // ------------------------------------------------------------------
    //  The non-template ends of everything above, so that the threshold test,
    //  the channel, the throttle table and the write exist once in Debug.cpp
    //  rather than once per instantiation.
    //
    //  THE DEFAULT CHANNEL IS NEVER PASSED IN FROM A CALLER. The versions
    //  without a channel look it up inside Debug.cpp, so no std::string_view
    //  of a settable global ever crosses a call boundary - which is the one
    //  way SetDefaultChannel could have been made to dangle.
    // ------------------------------------------------------------------
    static void Emit(LogLevel level, std::string_view message);
    static void EmitIn(LogLevel level, std::string_view channel,
                       std::string_view message);

    static void Once(LogLevel level, std::string_view key, std::string_view message);
    static void Every(LogLevel level, float seconds, std::string_view key,
                      std::string_view message);

    // True when `key` has not been seen before, and when it is due again.
    static bool FirstTime(std::string_view key);
    static bool DueNow(std::string_view key, float seconds);

    template <class A, class... Rest>
    static void Formatted(LogLevel level, std::format_string<A, Rest...> fmt,
                          A&& a, Rest&&... rest) {
        if (!::eng::Log::ShouldLog(level)) {
            return;
        }
        Emit(level, std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...));
    }

    template <class A, class... Rest>
    static void FormattedIn(LogLevel level, std::string_view channel,
                            std::format_string<A, Rest...> fmt, A&& a, Rest&&... rest) {
        if (!::eng::Log::ShouldLog(level)) {
            return;
        }
        EmitIn(level, channel,
               std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...));
    }

    // Fires whenever one of the three catch-alls above is instantiated,
    // which is only ever for a type the constraint has already ruled out.
    template <class T>
    static void Unprintable() {
        static_assert(DebugPrintable<T>,
                      "Debug::Log/LogWarning/LogError: std::format cannot print this "
                      "type, so there is nothing to log. An enum needs "
                      "static_cast<int>; a pointer needs the thing it points at, or "
                      "static_cast<const void*> for the address; a type of your own "
                      "needs a std::formatter specialisation - math/Vec2.h and "
                      "scene/EntityId.h are two short worked examples. Or format it "
                      "yourself and log the string.");
    }

    template <class T>
    static void Printed(LogLevel level, const T& value) {
        if (!::eng::Log::ShouldLog(level)) {
            return;
        }
        Emit(level, std::format("{}", value));
    }

    template <class T>
    static void PrintedIn(LogLevel level, std::string_view channel, const T& value) {
        if (!::eng::Log::ShouldLog(level)) {
            return;
        }
        EmitIn(level, channel, std::format("{}", value));
    }

    template <class A, class... Rest>
    static void FormattedOnce(LogLevel level, std::format_string<A, Rest...> fmt,
                              A&& a, Rest&&... rest) {
        if (!::eng::Log::ShouldLog(level) || !FirstTime(fmt.get())) {
            return;
        }
        Emit(level, std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...));
    }

    template <class A, class... Rest>
    static void FormattedEvery(LogLevel level, float seconds,
                               std::format_string<A, Rest...> fmt,
                               A&& a, Rest&&... rest) {
        if (!::eng::Log::ShouldLog(level) || !DueNow(fmt.get(), seconds)) {
            return;
        }
        Emit(level, std::format(fmt, std::forward<A>(a), std::forward<Rest>(rest)...));
    }
};

} // namespace eng
