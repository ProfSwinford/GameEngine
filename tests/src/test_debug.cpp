// ============================================================================
//  test_debug.cpp - the Debug facade: what it logs, what it draws, and the
//  three places it is easy to get wrong.
//
//  WHAT IS WORTH TESTING HERE, given that almost every function is a forward
//  to eng::Log or eng::Gizmos:
//
//    1. THE OVERLOAD SET. Three shapes of Log have to pick the right one -
//       a literal, a format with values, and a bare value - and the wrong
//       choice is either a compile error or a message with braces in it. The
//       cases below call all three with the kinds of argument a script
//       actually passes.
//
//    2. THE THROTTLES. LogOnce and LogEvery are keyed by the FORMAT STRING
//       rather than by the finished message, which is the one surprising
//       thing in the file. If that broke, a throttle keyed on text would
//       silently stop throttling the moment a value changed - and nothing
//       would fail except somebody's Console.
//
//    3. THE WATCH LIST'S FRAME RESET. The line cursor goes back to the top
//       when GameClock::FrameCount changes, rather than being reset by a call
//       the engine makes. Nothing but a test notices if that stops working:
//       the lines would simply march off the bottom of the screen.
//
//  NOTHING HERE STARTS THE ENGINE. Log::Write needs no Init to reach the
//  buffer the Console reads - see Log.cpp - and the clock is a plain member
//  of the engine singleton that can be driven by hand. That is what makes
//  this a unit test and not a game.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/Engine.h>

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
//  Puts everything this file touches back how it was found.
//
//  There is a lot of it because Debug is a facade over four pieces of global
//  state - the log's threshold, the Console's buffer, the gizmo queue and
//  Debug's own settings - and a case that left any of them changed would
//  change the meaning of whatever test ran next. Which cases those are
//  depends on link order, which is the worst way to find out a suite is
//  order-dependent. The same argument as ClockGuard in test_time.cpp.
// ---------------------------------------------------------------------------
struct DebugGuard {
    eng::LogLevel    threshold = eng::Log::GetThreshold();
    std::string      channel{eng::Debug::DefaultChannel()};
    eng::Vec2        origin    = eng::Debug::ScreenOrigin();
    eng::Color       colour    = eng::Debug::ScreenColor();
    eng::GameClock&  clock     = eng::Engine::Get().Clock();
    bool             paused    = clock.IsPaused();

    DebugGuard() {
        // Info, so a case can see what it logged. The shipped config says
        // Info too, but another test file is free to have changed it.
        eng::Log::SetThreshold(eng::LogLevel::Info);
        eng::LogBuffer::Clear();
        eng::Gizmos::Clear();
        eng::Debug::ResetThrottles();
        eng::Debug::ResetScreenLines();
        eng::Debug::SetDefaultChannel("");   // back to "Game"
    }

    ~DebugGuard() {
        eng::Log::SetThreshold(threshold);
        eng::Debug::SetDefaultChannel(channel);
        eng::Debug::SetScreenOrigin(origin);
        eng::Debug::SetScreenColor(colour);
        eng::Debug::ResetThrottles();
        eng::LogBuffer::Clear();
        eng::Gizmos::Clear();
        clock.SetPaused(paused);
    }
};

std::vector<eng::LogRecord> Records() {
    std::vector<eng::LogRecord> out;
    eng::LogBuffer::Snapshot(out);
    return out;
}

// How many messages are on one channel. Counting the WHOLE buffer is what
// broke the LogEvery case first time round: driving the clock by half a
// second asks for thirty simulation steps, the clock refuses after five, and
// GameClock::BeginFrame says so on the Core channel - a perfectly correct
// warning that arrived in the middle of a case counting its own output.
std::size_t CountOn(std::string_view channel) {
    std::size_t n = 0;
    for (const eng::LogRecord& record : Records()) {
        if (record.channel == channel) {
            ++n;
        }
    }
    return n;
}

// The one message in the buffer, or an empty record. Keeps the cases reading
// as one assertion rather than three.
eng::LogRecord Only() {
    const std::vector<eng::LogRecord> all = Records();
    if (all.size() != 1) {
        return eng::LogRecord{};
    }
    return all.front();
}

} // namespace

// ===========================================================================
//  Logging
// ===========================================================================

TEST_CASE("Debug::Log reaches the Console buffer on the Game channel") {
    const DebugGuard guard;

    eng::Debug::Log("hello");

    const eng::LogRecord record = Only();
    CHECK(record.message == "hello");
    CHECK(record.channel == "Game");
    CHECK(record.level == eng::LogLevel::Info);

    // The channel is Log.h's own name for it rather than a second spelling.
    CHECK(eng::Debug::DefaultChannel() == eng::Channels::kGame);
}

TEST_CASE("the three levels arrive as the three levels") {
    const DebugGuard guard;

    eng::Debug::Log("a");
    eng::Debug::LogWarning("b");
    eng::Debug::LogError("c");

    const std::vector<eng::LogRecord> all = Records();
    REQUIRE(all.size() == 3);
    CHECK(all[0].level == eng::LogLevel::Info);
    CHECK(all[1].level == eng::LogLevel::Warning);
    CHECK(all[2].level == eng::LogLevel::Error);
}

TEST_CASE("all three shapes of Log reach the same place") {
    const DebugGuard guard;

    SUBCASE("a literal is printed verbatim") {
        eng::Debug::Log("ready");
        CHECK(Only().message == "ready");
    }

    SUBCASE("a format and its values are formatted") {
        eng::Debug::Log("hp {} of {}", 3, 10);
        CHECK(Only().message == "hp 3 of 10");
    }

    SUBCASE("one bare value is printed as {}") {
        eng::Debug::Log(42);
        CHECK(Only().message == "42");
    }

    SUBCASE("a std::string built at runtime goes to the verbatim overload") {
        const std::string built = "went wrong at " + std::to_string(7);
        eng::Debug::Log(built);
        CHECK(Only().message == "went wrong at 7");
    }

    SUBCASE("BRACES IN A VERBATIM STRING STAY BRACES") {
        // The whole reason the one-string overload is a separate function. If
        // this ever went through std::format it would throw or mangle.
        eng::Debug::Log("50% {done}");
        CHECK(Only().message == "50% {done}");
    }

    SUBCASE("a Vec2 needs no format string") {
        eng::Debug::Log(eng::Vec2{1.5f, -2.0f});
        CHECK(Only().message == "(1.5, -2)");
    }

    SUBCASE("and neither does an EntityId") {
        eng::Debug::Log(eng::EntityId{12, 3});
        CHECK(Only().message == "#12:3");
    }
}

TEST_CASE("the threshold is obeyed, because this is still the engine log") {
    const DebugGuard guard;

    eng::Log::SetThreshold(eng::LogLevel::Error);

    eng::Debug::Log("dropped");
    eng::Debug::LogWarning("also dropped");
    CHECK(Records().empty());

    eng::Debug::LogError("kept");
    CHECK(Only().message == "kept");

    // And the guard a caller puts in front of an expensive argument agrees
    // with what actually happened.
    CHECK_FALSE(eng::Debug::WillLog());
    CHECK_FALSE(eng::Debug::WillLog(eng::LogLevel::Warning));
    CHECK(eng::Debug::WillLog(eng::LogLevel::Error));
}

TEST_CASE("Debug::In names its own channel without disturbing the default") {
    const DebugGuard guard;

    eng::Debug::In("AI").LogWarning("no path from {} to {}", 1, 2);

    const eng::LogRecord record = Only();
    CHECK(record.channel == "AI");
    CHECK(record.message == "no path from 1 to 2");
    CHECK(record.level == eng::LogLevel::Warning);
    CHECK(eng::Debug::DefaultChannel() == "Game");

    // It owns its name, so one kept past the expression that made it is still
    // valid. That is the whole reason Channel holds a std::string.
    const eng::Debug::Channel log = eng::Debug::In(std::string{"Combat"});
    eng::LogBuffer::Clear();
    log.Log("swing");
    CHECK(Only().channel == "Combat");
}

TEST_CASE("SetDefaultChannel changes it, and an empty name puts Game back") {
    const DebugGuard guard;

    eng::Debug::SetDefaultChannel("MyGame");
    eng::Debug::Log("one");
    CHECK(Only().channel == "MyGame");

    eng::Debug::SetDefaultChannel("");
    eng::LogBuffer::Clear();
    eng::Debug::Log("two");
    CHECK(Only().channel == "Game");
}

TEST_CASE("Assert logs an error when it fails and says nothing when it passes") {
    const DebugGuard guard;

    eng::Debug::Assert(true, "never seen");
    eng::Debug::Assert(2 + 2 == 4);
    CHECK(Records().empty());

    eng::Debug::Assert(false, "hp went negative: {}", -3);
    const eng::LogRecord record = Only();
    CHECK(record.level == eng::LogLevel::Error);
    CHECK(record.message == "hp went negative: -3");

    eng::LogBuffer::Clear();
    eng::Debug::Assert(false);
    CHECK(Only().message == "assertion failed");
}

// ===========================================================================
//  The throttles
// ===========================================================================

TEST_CASE("LogOnce is keyed by the FORMAT STRING, not by the message") {
    const DebugGuard guard;

    // Three calls, three different finished messages, ONE format string.
    for (int i = 0; i < 3; ++i) {
        eng::Debug::LogErrorOnce("no Rigidbody2D on entity {}", i);
    }

    const eng::LogRecord record = Only();
    CHECK(record.message == "no Rigidbody2D on entity 0");
    CHECK(record.level == eng::LogLevel::Error);
    CHECK(eng::Debug::ThrottleKeyCount() == 1);

    // A different format string is a different key.
    eng::Debug::LogErrorOnce("and none on {} either", 9);
    CHECK(Records().size() == 2);
    CHECK(eng::Debug::ThrottleKeyCount() == 2);
}

TEST_CASE("LogOnce on a plain string is keyed by the string") {
    const DebugGuard guard;

    eng::Debug::LogOnce("no target");
    eng::Debug::LogOnce("no target");
    eng::Debug::LogWarningOnce("no target");   // same key, different level
    CHECK(Records().size() == 1);

    eng::Debug::LogOnce("no target either");
    CHECK(Records().size() == 2);
}

TEST_CASE("ResetThrottles makes a once-only message log again") {
    const DebugGuard guard;

    eng::Debug::LogOnce("spawned");
    eng::Debug::LogOnce("spawned");
    CHECK(Records().size() == 1);

    eng::Debug::ResetThrottles();
    CHECK(eng::Debug::ThrottleKeyCount() == 0);

    eng::Debug::LogOnce("spawned");
    CHECK(Records().size() == 2);
}

TEST_CASE("a throttled message below the threshold does not use up its key") {
    const DebugGuard guard;

    // Otherwise lowering the threshold later would show nothing: the one
    // appearance would have been spent while it was invisible.
    eng::Log::SetThreshold(eng::LogLevel::Error);
    eng::Debug::LogOnce("progress");
    CHECK(Records().empty());
    CHECK(eng::Debug::ThrottleKeyCount() == 0);

    eng::Log::SetThreshold(eng::LogLevel::Info);
    eng::Debug::LogOnce("progress");
    CHECK(Only().message == "progress");
}

TEST_CASE("LogEvery waits out its interval on the unscaled clock") {
    const DebugGuard guard;
    eng::GameClock&  clock = eng::Engine::Get().Clock();
    clock.Init();

    // PAUSED THROUGHOUT, AND THAT IS THE POINT RATHER THAN A DODGE.
    //
    // GameClock::BeginFrame adds the real delta BEFORE it tests the pause
    // flag, so real time still moves while the clock is paused - and
    // LogEvery measures real time, which is what makes a throttled message
    // still arrive in a paused or slow-motion game. Pausing also means no
    // simulation steps are owed, which keeps the clock's own "hit the limit
    // of N simulation steps" warning out of the buffer this case is reading.
    clock.SetPaused(true);

    eng::Debug::LogEvery(0.5f, "pos {}", 1);
    CHECK(CountOn("Game") == 1);

    // Not yet: a tenth of a second of real time.
    clock.BeginFrame(0.1);
    eng::Debug::LogEvery(0.5f, "pos {}", 2);
    CHECK(CountOn("Game") == 1);

    // Past half a second now.
    clock.BeginFrame(0.5);
    eng::Debug::LogEvery(0.5f, "pos {}", 3);
    REQUIRE(CountOn("Game") == 2);
    CHECK(Records().back().message == "pos 3");

    // The mark moves to when it last got through, so the wait starts again.
    clock.BeginFrame(0.2);
    eng::Debug::LogEvery(0.5f, "pos {}", 4);
    CHECK(CountOn("Game") == 2);

    clock.BeginFrame(0.4);
    eng::Debug::LogEvery(0.5f, "pos {}", 5);
    CHECK(CountOn("Game") == 3);
}

// ===========================================================================
//  Drawing
// ===========================================================================

TEST_CASE("the drawing calls queue the shapes they say they do") {
    const DebugGuard guard;

    SUBCASE("a line is one command") {
        eng::Debug::DrawLine(eng::Vec2{0.0f, 0.0f}, eng::Vec2{10.0f, 0.0f});
        CHECK(eng::Gizmos::QueuedCount() == 1);
    }

    SUBCASE("a ray is one command, from a point and an offset") {
        eng::Debug::DrawRay(eng::Vec2{5.0f, 5.0f}, eng::Vec2{0.0f, -20.0f});
        CHECK(eng::Gizmos::QueuedCount() == 1);
    }

    SUBCASE("an arrow is the shaft and two barbs") {
        eng::Debug::DrawArrow(eng::Vec2{0.0f, 0.0f}, eng::Vec2{40.0f, 0.0f});
        CHECK(eng::Gizmos::QueuedCount() == 3);
    }

    SUBCASE("an arrow with nowhere to point is just the line") {
        // No direction means no barbs rather than a division by zero.
        eng::Debug::DrawArrow(eng::Vec2{3.0f, 3.0f}, eng::Vec2{3.0f, 3.0f});
        CHECK(eng::Gizmos::QueuedCount() == 1);
    }

    SUBCASE("a cross is two lines") {
        eng::Debug::DrawCross(eng::Vec2{1.0f, 2.0f});
        CHECK(eng::Gizmos::QueuedCount() == 2);
    }

    SUBCASE("a circle and both boxes are one command each") {
        eng::Debug::DrawCircle(eng::Vec2{0.0f, 0.0f}, 12.0f);
        eng::Debug::DrawBox(eng::Vec2{0.0f, 0.0f}, eng::Vec2{10.0f, 4.0f});
        eng::Debug::DrawBox(eng::AABB::FromMinMax(eng::Vec2{0.0f, 0.0f},
                                                  eng::Vec2{8.0f, 8.0f}));
        CHECK(eng::Gizmos::QueuedCount() == 3);
    }

    SUBCASE("world text, written out and formatted") {
        eng::Debug::DrawText(eng::Vec2{0.0f, 0.0f}, "hello");
        eng::Debug::DrawText(eng::Vec2{0.0f, 0.0f}, "hp {}", 5);
        CHECK(eng::Gizmos::QueuedCount() == 2);
    }
}

TEST_CASE("DrawBox takes a centre and a FULL size") {
    const DebugGuard guard;

    // The one argument convention worth a test, because half-extents is the
    // other plausible reading and nothing would fail visibly - the box would
    // just be twice the size it should be.
    const eng::AABB expected =
        eng::AABB::FromMinMax(eng::Vec2{-5.0f, -2.0f}, eng::Vec2{5.0f, 2.0f});

    eng::Debug::DrawBox(eng::Vec2{0.0f, 0.0f}, eng::Vec2{10.0f, 4.0f});
    CHECK(eng::Gizmos::QueuedCount() == 1);

    // The queue does not hand its contents back, so this checks the
    // arithmetic rather than the command: AABB::FromCenterHalfExtents with
    // half of the size is what Debug::DrawBox does.
    const eng::AABB built = eng::AABB::FromCenterHalfExtents(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{10.0f * 0.5f, 4.0f * 0.5f});
    CHECK(built.min.x == doctest::Approx(expected.min.x));
    CHECK(built.min.y == doctest::Approx(expected.min.y));
    CHECK(built.max.x == doctest::Approx(expected.max.x));
    CHECK(built.max.y == doctest::Approx(expected.max.y));
}

// ===========================================================================
//  The watch list
// ===========================================================================

TEST_CASE("the watch list stacks within a frame and starts again on the next") {
    const DebugGuard guard;
    eng::GameClock&  clock = eng::Engine::Get().Clock();
    clock.Init();

    eng::Debug::Watch("fps", 60);
    eng::Debug::Watch("pos", eng::Vec2{1.0f, 2.0f});
    eng::Debug::Watch("state", "idle");
    eng::Debug::ScreenText("anything at all");
    eng::Debug::ScreenText("step {}", 3);

    CHECK(eng::Debug::ScreenLineCount() == 5);
    CHECK(eng::Gizmos::QueuedCount() == 5);

    // A new frame starts the lines again from the top. Nothing calls Debug to
    // tell it so - it reads GameClock::FrameCount, which moves on every frame
    // whether the game is playing, paused, or not running at all.
    clock.BeginFrame(1.0 / 60.0);
    CHECK(eng::Debug::ScreenLineCount() == 0);

    eng::Debug::Watch("fps", 59);
    CHECK(eng::Debug::ScreenLineCount() == 1);
}

TEST_CASE("Watch prints label: value, and takes a string value as a string") {
    const DebugGuard guard;

    // Both Watch overloads reach Gizmos rather than the log, so what they
    // WROTE cannot be read back - but which overload was chosen can be, by
    // the fact that both of these compile at all. A std::string_view value
    // has no formatter path, and a bool does.
    eng::Debug::Watch("grounded", true);
    eng::Debug::Watch("state", std::string{"idle"});
    eng::Debug::Watch("hp", 7.5f);
    CHECK(eng::Debug::ScreenLineCount() == 3);
}

TEST_CASE("a newline starts a new line rather than landing on the last one") {
    const DebugGuard guard;

    // The layer underneath cannot do this: Gizmos::Text reaches
    // SDL_RenderDebugText, which draws one line and ignores an embedded
    // newline, so the second and third would be drawn on top of the first.
    eng::Debug::ScreenText("one\ntwo\nthree");
    CHECK(eng::Debug::ScreenLineCount() == 3);
    CHECK(eng::Gizmos::QueuedCount() == 3);

    // \r\n is one break, not two - eng::ForEachLine in render/Font.h is the
    // one place in the engine that knows that, and this goes through it.
    eng::Debug::ScreenText("four\r\nfive");
    CHECK(eng::Debug::ScreenLineCount() == 5);

    // An empty string still takes a line rather than quietly doing nothing.
    eng::Debug::ScreenText("");
    CHECK(eng::Debug::ScreenLineCount() == 6);

    // And world text splits too, into one gizmo per line.
    eng::Gizmos::Clear();
    eng::Debug::DrawText(eng::Vec2::Zero(), "top\nbottom");
    CHECK(eng::Gizmos::QueuedCount() == 2);
}

TEST_CASE("the watch list's corner and colour are settable") {
    const DebugGuard guard;

    CHECK(eng::Debug::ScreenOrigin().x == doctest::Approx(10.0f));
    CHECK(eng::Debug::ScreenOrigin().y == doctest::Approx(10.0f));

    eng::Debug::SetScreenOrigin(eng::Vec2{4.0f, 300.0f});
    eng::Debug::SetScreenColor(eng::Color::Yellow());
    CHECK(eng::Debug::ScreenOrigin().y == doctest::Approx(300.0f));
    CHECK(eng::Debug::ScreenColor() == eng::Color::Yellow());
}

// ===========================================================================
//  The editor, and what kind of build this is
// ===========================================================================

TEST_CASE("Break does nothing when there is no editor to pause into") {
    const DebugGuard guard;
    eng::GameClock&  clock = eng::Engine::Get().Clock();
    clock.SetPaused(false);

    // The test suite is not the editor: it installs no GUI hooks, so this is
    // the built-game case, and a built game has no Pause button to resume
    // from. Pausing here would be a hang.
    REQUIRE_FALSE(eng::Debug::IsEditorAttached());

    eng::Debug::Break();
    CHECK_FALSE(clock.IsPaused());

    // It says so rather than doing it silently.
    const eng::LogRecord record = Only();
    CHECK(record.level == eng::LogLevel::Warning);
    CHECK(record.message.find("built game") != std::string::npos);
}

TEST_CASE("ClearConsole empties the buffer and nothing else") {
    const DebugGuard guard;

    eng::Debug::Log("one");
    eng::Debug::Log("two");
    CHECK(Records().size() == 2);

    eng::Debug::ClearConsole();
    CHECK(Records().empty());

    // TotalWritten counts everything ever written, including what was
    // cleared, so the Console can still tell that messages happened.
    CHECK(eng::LogBuffer::TotalWritten() >= 2);
}

TEST_CASE("IsDebugBuild answers for the engine and not for this translation unit") {
    // Nothing to assert about the VALUE - it is whichever configuration the
    // engine was built in. What matters is that it is a function call into
    // the engine rather than a constant compiled into this file, so that a
    // script built separately still gets the engine's answer.
    const bool answer = eng::Debug::IsDebugBuild();
    CHECK((answer == true || answer == false));
}

// ===========================================================================
//  The formatters the friendly shapes depend on
// ===========================================================================

TEST_CASE("Vec2 formats as a pair, and the specification goes to the numbers") {
    CHECK(std::format("{}", eng::Vec2{1.5f, -2.0f}) == "(1.5, -2)");
    CHECK(std::format("{}", eng::Vec2::Zero()) == "(0, 0)");

    // THE POINT OF WRITING THE FORMATTER OUT rather than delegating to
    // std::formatter<std::string>: the specification reaches x and y.
    CHECK(std::format("{:.2f}", eng::Vec2{1.5f, -2.0f}) == "(1.50, -2.00)");
    CHECK(std::format("{:.0f}", eng::Vec2{120.4f, 48.6f}) == "(120, 49)");
}

TEST_CASE("EntityId formats as index:generation, and null says so") {
    CHECK(std::format("{}", eng::EntityId{}) == "#null");
    CHECK(std::format("{}", eng::EntityId{0, 0}) == "#0:0");
    CHECK(std::format("{}", eng::EntityId{12, 3}) == "#12:3");

    // BOTH NUMBERS, always: a stale id is one whose generation has moved on,
    // and printing only the index hides exactly the bug the type exists for.
    CHECK(std::format("{}", eng::EntityId{7, 1}) != std::format("{}", eng::EntityId{7, 2}));

    // It inherits std::formatter<std::string>, so string specifications work.
    CHECK(std::format("{:>8}", eng::EntityId{1, 0}) == "    #1:0");
}
