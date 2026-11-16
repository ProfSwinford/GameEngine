// ============================================================================
//  Tests for eng::InputActions and eng::ActionMapState - the event-driven
//  half of the input system.
//
//  THE PRESS EDGE IS THE HEADLINE. Until this file, no test in this suite
//  could produce one: EventPump's list is private and only Poll() fills it,
//  Poll() needs SDL, and the test program deliberately links the engine and
//  nothing else. InputMap::InjectAction is the documented way in and cannot
//  make an edge either - it writes its state BETWEEN frames, and Update's
//  first step has copied that into "last frame" before the next frame looks,
//  so an injected action reads Idle, Held, Idle and is never once Pressed.
//  Measured: inject(true), Update x3, inject(false), Update x2 gives
//  "Idle Held Held Held Idle Idle".
//
//  The new system has no equivalent because its input IS the frame's list:
//  what a test hands it is the same thing the pump would have handed it, and
//  the edge is worked out by the same code at the same moment. There is no
//  second path to keep in step, which is why this is a seam and not a hole.
//
//  THE OTHER ONE THAT MATTERS IS "polling answers the same". Every script in
//  every project polls InputMap today, and the event layer earns its place
//  only if switching it on cannot change what those scripts see.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Json.h>
#include <engine/input/InputActionAsset.h>
#include <engine/input/InputActions.h>
#include <engine/input/InputMap.h>
#include <engine/platform/EventPump.h>

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace {

eng::RawEvent Key(const char* name, bool down) {
    eng::RawEvent event{};
    event.code = eng::EventPump::KeyCodeFromName(name);
    event.kind = down ? eng::RawEventKind::KeyDown : eng::RawEventKind::KeyUp;
    return event;
}

eng::RawEvent MouseButton(const char* name, bool down) {
    eng::RawEvent event{};
    event.code = eng::EventPump::MouseButtonFromName(name);
    event.kind = down ? eng::RawEventKind::MouseButtonDown
                      : eng::RawEventKind::MouseButtonUp;
    return event;
}

// One frame's worth of raw input, exactly as the pump would have handed it
// over in a shipped game - where nothing is ever marked consumed, because a
// standalone player installs no GuiHooks.
void Feed(std::initializer_list<eng::RawEvent> events) {
    eng::InputActions::Sample(std::span<const eng::RawEvent>{events.begin(), events.size()});
}

void FeedNothing() { eng::InputActions::Sample(std::span<const eng::RawEvent>{}); }

// FILLED BY REFERENCE rather than returned, because an InputActionAsset is
// deliberately non-copyable - the library hands out pointers and compares
// identity by them, so a copy would be a second asset claiming to be the
// same one. TileLibrary::Reload holds its probe the same way.
void ParseAssetInto(const char* text, eng::InputActionAsset& into) {
    std::string     parseError;
    const eng::Json node = eng::ParseJson(text, parseError);
    REQUIRE(parseError.empty());

    std::string error, warnings;
    REQUIRE(into.Deserialize(node, error, warnings));
    REQUIRE(warnings.empty());
}

struct Recorder {
    std::vector<std::string> log;
    std::vector<eng::Vec2>   values;

    void Attach(eng::ActionMapState& state) {
        state.Subscribe([this](const eng::ActionEvent& event) {
            log.emplace_back(std::string(event.action) + ":" + eng::ToString(event.phase));
            values.push_back(event.vector);
        });
    }
};

constexpr const char* kPlayer = R"({
  "defaultMap": "Gameplay",
  "maps": [
    { "name": "Gameplay", "actions": [
        { "name": "Jump",  "type": "Button", "bindings": ["Key.Space"] },
        { "name": "Fire",  "type": "Button", "bindings": ["Mouse.Left", "Key.Return"] },
        { "name": "Pause", "type": "Button", "bindings": ["Key.Escape"] },
        { "name": "Move",  "type": "Value",  "bindings": [
            { "composite": "2DVector", "up": "Key.W",  "down": "Key.S",    "left": "Key.A",    "right": "Key.D" },
            { "composite": "2DVector", "up": "Key.Up", "down": "Key.Down", "left": "Key.Left", "right": "Key.Right" }
        ]}
    ]},
    { "name": "UI", "actions": [
        { "name": "Cancel", "type": "Button", "bindings": ["Key.Escape"] },
        { "name": "Nudge",  "type": "Value",  "bindings": [
            { "composite": "2DVector", "up": "Key.W", "down": "Key.S", "left": "Key.A", "right": "Key.D" }
        ]}
    ]}
  ]
})";

} // namespace

TEST_CASE("a press raises Started then Performed, and the release raises Canceled") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    // Nothing held, however many frames go by.
    FeedNothing();
    state.Dispatch();
    state.Dispatch();
    CHECK(rec.log.empty());

    Feed({Key("Space", true)});
    state.Dispatch();
    REQUIRE(rec.log.size() == 2);
    CHECK(rec.log[0] == "Jump:Started");
    CHECK(rec.log[1] == "Jump:Performed");

    // HELD RAISES NOTHING MORE, which is Unity's rule for a Button and is
    // why a jump happens once per press instead of every frame.
    FeedNothing();
    state.Dispatch();
    state.Dispatch();
    CHECK(rec.log.size() == 2);

    Feed({Key("Space", false)});
    state.Dispatch();
    REQUIRE(rec.log.size() == 3);
    CHECK(rec.log[2] == "Jump:Canceled");

    FeedNothing();
    state.Dispatch();
    CHECK(rec.log.size() == 3);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("two bindings on one action raise it once, not twice") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    // The mouse and the keyboard both say Fire, in the same frame.
    Feed({MouseButton("Left", true), Key("Return", true)});
    state.Dispatch();
    CHECK(rec.log == std::vector<std::string>{"Fire:Started", "Fire:Performed"});

    // And letting go of only one of them is not a release.
    Feed({MouseButton("Left", false)});
    state.Dispatch();
    CHECK(rec.log.size() == 2);

    Feed({Key("Return", false)});
    state.Dispatch();
    REQUIRE(rec.log.size() == 3);
    CHECK(rec.log[2] == "Fire:Canceled");

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a diagonal is cut back to length one, exactly as GetAxis2D cuts it") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    Feed({Key("D", true)});
    state.Dispatch();
    CHECK(rec.log == std::vector<std::string>{"Move:Started", "Move:Performed"});
    CHECK(state.Vector("Move").x == doctest::Approx(1.0f));
    CHECK(state.Vector("Move").y == doctest::Approx(0.0f));

    // Up as well: without the correction this would be about 1.41 long, and a
    // diagonal would be 41% faster than a straight line.
    Feed({Key("W", true)});
    state.Dispatch();
    REQUIRE(rec.log.size() == 3);
    CHECK(rec.log[2] == "Move:Performed");          // still going, new amount
    CHECK(state.Vector("Move").x == doctest::Approx(0.70710678f));
    CHECK(state.Vector("Move").y == doctest::Approx(0.70710678f));
    CHECK(state.Vector("Move").Length() == doctest::Approx(1.0f));

    Feed({Key("D", false)});
    state.Dispatch();
    CHECK(state.Vector("Move").y == doctest::Approx(1.0f));

    Feed({Key("W", false)});
    state.Dispatch();
    CHECK(rec.log.back() == "Move:Canceled");
    CHECK(state.Vector("Move") == eng::Vec2{});

    eng::InputActions::ReleaseAll();
}

TEST_CASE("the loudest binding wins, so WASD and the arrows do not add up") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));

    // Two complete composites on one action, with one key held from each.
    Feed({Key("W", true), Key("Up", true)});
    state.Dispatch();

    // (0,1), not (0,2). Unity's conflict resolution, and the reason an action
    // can carry a second whole composite without the two interfering.
    CHECK(state.Vector("Move").y == doctest::Approx(1.0f));
    CHECK(state.Vector("Move").x == doctest::Approx(0.0f));

    eng::InputActions::ReleaseAll();
}

TEST_CASE("alt-tabbing lets go of everything") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    Feed({Key("W", true), Key("Space", true)});
    state.Dispatch();
    CHECK(eng::InputActions::DownCount() == 2);
    rec.log.clear();

    // THE KeyUp GOES TO WHATEVER WINDOW TOOK THE FOCUS and never arrives
    // here, so this is the only safe reading of losing it. Without this, a
    // key held while alt-tabbing is held for the rest of the session.
    eng::RawEvent lost{};
    lost.kind = eng::RawEventKind::WindowFocusLost;
    Feed({lost});
    state.Dispatch();

    CHECK(eng::InputActions::DownCount() == 0);

    // BOTH of them, in the map's own order - the jump and the movement.
    CHECK(rec.log == std::vector<std::string>{"Jump:Canceled", "Move:Canceled"});
    CHECK(state.Vector("Move") == eng::Vec2{});

    eng::InputActions::ReleaseAll();
}

TEST_CASE("switching maps with a key held does the two different right things") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    // Escape opens the menu, and W is held while it happens.
    Feed({Key("Escape", true), Key("W", true)});
    state.Dispatch();
    CHECK(rec.log == std::vector<std::string>{"Pause:Started", "Pause:Performed",
                                              "Move:Started", "Move:Performed"});
    rec.log.clear();

    state.SetMap(asset.FindMap("UI"));

    // Everything the old map had running is told to stop.
    CHECK(rec.log == std::vector<std::string>{"Pause:Canceled", "Move:Canceled"});
    rec.log.clear();

    state.Dispatch();

    // A VALUE ACTION ADOPTS WHAT IS ALREADY HELD - Unity's initial state
    // check, always on for Value - so Nudge starts at once rather than
    // waiting for W to be let go of and pressed again.
    REQUIRE(rec.log.size() == 2);
    CHECK(rec.log[0] == "Nudge:Started");
    CHECK(state.Vector("Nudge").y == doctest::Approx(1.0f));

    // A BUTTON DOES NOT, and this is the case that makes it matter: Cancel is
    // bound to the same Escape that opened the menu, and an initial state
    // check would close the menu with the press that opened it.
    CHECK(rec.log[1] != "Cancel:Started");
    CHECK_FALSE(state.IsDown("Cancel"));

    rec.log.clear();
    Feed({Key("Escape", false)});
    state.Dispatch();
    CHECK(rec.log.empty());          // no phantom cancel for a press it never saw

    Feed({Key("Escape", true)});
    state.Dispatch();
    CHECK(rec.log == std::vector<std::string>{"Cancel:Started", "Cancel:Performed"});

    eng::InputActions::ReleaseAll();
}

TEST_CASE("two players hold two different maps without hearing each other") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState one, two;
    one.SetMap(asset.FindMap("Gameplay"));
    two.SetMap(asset.FindMap("UI"));

    Recorder recOne, recTwo;
    recOne.Attach(one);
    recTwo.Attach(two);

    Feed({Key("Space", true)});
    one.Dispatch();
    two.Dispatch();

    // Space is Jump in Gameplay and nothing at all in UI.
    CHECK(recOne.log == std::vector<std::string>{"Jump:Started", "Jump:Performed"});
    CHECK(recTwo.log.empty());

    // The per-action history lives on the state, not in a file-scope value,
    // which is the whole reason this is representable.
    CHECK(one.SubscriberCount() == 1);
    CHECK(two.SubscriberCount() == 1);
    CHECK(one.Map() != two.Map());

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a frame nobody dispatched is not a frame nobody noticed") {
    // THE PAUSE BUG THIS DESIGN EXISTS TO NOT HAVE. Sampling is ungated and
    // dispatch is gated, so letting go of a key while the game is paused is
    // seen even though nothing is told about it until play resumes. An edge
    // is a comparison and not a moment, so the first dispatch afterwards
    // raises exactly the one change that happened in between.
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));
    Recorder rec;
    rec.Attach(state);

    Feed({Key("W", true)});
    state.Dispatch();
    CHECK(state.Vector("Move").y == doctest::Approx(1.0f));
    rec.log.clear();

    // ...the game is paused here, so Dispatch is not called for a while...
    Feed({Key("W", false)});
    FeedNothing();
    FeedNothing();
    FeedNothing();

    // ...and resumes.
    state.Dispatch();
    CHECK(rec.log == std::vector<std::string>{"Move:Canceled"});
    CHECK(state.Vector("Move") == eng::Vec2{});

    eng::InputActions::ReleaseAll();
}

TEST_CASE("switching the event layer on cannot change what polling answers") {
    // Runs an identical InputMap sequence twice and writes down every answer
    // polling gave - once with no action map anywhere, and once with one
    // loaded, subscribed to and dispatching on the same frames.
    auto run = [](bool withActionMap) {
        eng::InputActions::ReleaseAll();
        eng::InputMap::ClearBindings();
        eng::InputMap::Bind("gameplay", "MoveLeft", "Key.A");
        eng::InputMap::Bind("gameplay", "MoveRight", "Key.D");
        eng::InputMap::Bind("gameplay", "MoveUp", "Key.W");
        eng::InputMap::Bind("gameplay", "MoveDown", "Key.S");
        eng::InputMap::Bind("gameplay", "Fire", "Mouse.Left");
        eng::InputMap::PushContext("gameplay");

        eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);
        eng::ActionMapState         state;
        Recorder                    rec;
        if (withActionMap) {
            state.SetMap(asset.FindMap("Gameplay"));
            rec.Attach(state);
        }

        eng::EventPump           pump;
        std::vector<std::string> answers;

        const char* script[] = {"MoveRight", "MoveUp", "Fire", "MoveLeft", ""};
        for (const char* held : script) {
            for (int repeat = 0; repeat < 3; ++repeat) {
                eng::InputMap::ClearInjectedActions();
                if (held[0] != 0) {
                    eng::InputMap::InjectAction(held, true);
                }

                eng::InputMap::Update(pump);
                if (withActionMap) {
                    // The new system driven on the very same frames, with real
                    // events of its own so it is genuinely doing work.
                    Feed({Key("W", repeat == 0), Key("W", repeat != 0)});
                    state.Dispatch();
                }

                for (const char* action :
                     {"MoveLeft", "MoveRight", "MoveUp", "MoveDown", "Fire"}) {
                    answers.emplace_back(eng::ToString(eng::InputMap::GetState(action)));
                }
                const eng::Vec2 axis = eng::InputMap::GetAxis2D();
                answers.emplace_back(std::to_string(axis.x) + "," + std::to_string(axis.y));
                answers.emplace_back(eng::InputMap::ActiveContext());
                answers.emplace_back(std::to_string(eng::InputMap::ContextDepth()));
            }
        }
        eng::InputMap::ClearBindings();
        return answers;
    };

    const std::vector<std::string> quiet     = run(false);
    const std::vector<std::string> listening = run(true);

    REQUIRE(quiet.size() == listening.size());
    CHECK(quiet == listening);

    eng::InputActions::ReleaseAll();
    eng::InputMap::ClearBindings();
}

TEST_CASE("a subscription can be taken off again, and an empty one is refused") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));

    CHECK(state.SubscriberCount() == 0);

    const auto a = state.Subscribe([](const eng::ActionEvent&) {});
    const auto b = state.Subscribe([](const eng::ActionEvent&) {});
    CHECK(state.SubscriberCount() == 2);
    CHECK(a != b);
    CHECK(a != 0u);

    // An empty std::function is refused rather than stored, so dispatch has
    // no null to step around and the caller is told by getting 0 back.
    CHECK(state.Subscribe(eng::ActionMapState::Callback{}) == 0u);
    CHECK(state.SubscriberCount() == 2);

    state.Unsubscribe(a);
    CHECK(state.SubscriberCount() == 1);

    state.Unsubscribe(0);
    state.Unsubscribe(a);        // already gone, not an error
    CHECK(state.SubscriberCount() == 1);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("no map means no events and no crash") {
    eng::InputActions::ReleaseAll();

    eng::ActionMapState state;
    Recorder            rec;
    rec.Attach(state);

    Feed({Key("Space", true)});
    state.Dispatch();
    state.Reset();
    state.SetMap(nullptr);

    CHECK(rec.log.empty());
    CHECK(state.Map() == nullptr);
    CHECK(state.Value("Jump") == doctest::Approx(0.0f));
    CHECK_FALSE(state.IsDown("Jump"));

    eng::InputActions::ReleaseAll();
}

// ===========================================================================
//  A HANDLER THAT LEAVES THE MAP IT WAS CALLED FROM.
//
//  This is the pattern the docs advertise - OnPause switching to the menu -
//  and it used to walk off the end of the parallel readings array. Dispatch
//  holds a reference to the action vector of the map it STARTED on and sizes
//  m_last to match; SetMap re-sizes m_last to the new map's length while the
//  walk is still in progress, so the next index was out of range whenever the
//  map being left was the longer of the two.
//
//  The fixture below is that shape on purpose: Gameplay has four actions with
//  Pause third, UI has two. Before the fix this test did not fail, it
//  ABORTED - MSVC debug raising "vector subscript out of range" from
//  <vector> - which is why the check is written as "nothing from the old map
//  arrives afterwards" rather than as a value comparison.
// ===========================================================================

TEST_CASE("a handler that switches maps ends the walk instead of running off it") {
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    const eng::ActionMap* const gameplay = asset.FindMap("Gameplay");
    const eng::ActionMap* const ui       = asset.FindMap("UI");
    REQUIRE(gameplay != nullptr);
    REQUIRE(ui != nullptr);

    // THE SHAPE IS THE TEST. If these ever stop holding, the case below goes
    // green without exercising anything.
    REQUIRE(gameplay->Actions().size() == 4);
    REQUIRE(ui->Actions().size() == 2);
    REQUIRE(gameplay->Actions()[2].name == "Pause");

    eng::ActionMapState state;
    state.SetMap(gameplay);

    std::vector<std::string> afterSwitch;
    bool                     switched = false;

    state.Subscribe([&](const eng::ActionEvent& event) {
        if (!switched && event.action == "Pause" &&
            event.phase == eng::ActionPhase::Performed) {
            // SetMap raises Canceled for what the old map had in flight, and
            // those arrive while `switched` is still false - so they are not
            // counted as "after", which is right: they belong to the switch.
            state.SetMap(ui);
            switched = true;
            return;
        }
        if (switched) {
            afterSwitch.emplace_back(std::string(event.action) + ":" +
                                     eng::ToString(event.phase));
        }
    });

    FeedNothing();
    state.Dispatch();

    Feed({Key("Escape", true)});
    state.Dispatch();
    REQUIRE(switched);

    // Move is the action AFTER Pause in the map we left. Nothing belonging to
    // that map may be delivered once something has switched away from it,
    // which is also Unity's rule.
    for (const std::string& late : afterSwitch) {
        MESSAGE("delivered after the switch: " << late);
    }
    CHECK(afterSwitch.empty());

    // AND THE SWITCH REALLY TOOK, rather than the walk merely surviving.
    // Jump belongs to Gameplay alone, so a press of Space reaching a
    // subscriber would mean the old map was still live.
    Feed({Key("Escape", false)});
    state.Dispatch();
    afterSwitch.clear();

    Feed({Key("Space", true)});
    state.Dispatch();
    CHECK(afterSwitch.empty());
    CHECK_FALSE(state.IsDown("Jump"));

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a handler that disables its map mid-walk is survivable too") {
    // Disable() is SetMap(nullptr), so it empties the readings array rather
    // than shortening it - every remaining index is then out of range, which
    // is the same bug with a worse blast radius.
    eng::InputActions::ReleaseAll();
    eng::InputActionAsset asset;
    ParseAssetInto(kPlayer, asset);

    eng::ActionMapState state;
    state.SetMap(asset.FindMap("Gameplay"));

    bool switched = false;
    state.Subscribe([&](const eng::ActionEvent& event) {
        if (!switched && event.action == "Pause" &&
            event.phase == eng::ActionPhase::Performed) {
            state.SetMap(nullptr);
            switched = true;
        }
    });

    FeedNothing();
    state.Dispatch();

    Feed({Key("Escape", true)});
    state.Dispatch();

    CHECK(switched);
    CHECK_FALSE(state.IsDown("Jump"));
    CHECK_FALSE(state.IsDown("Pause"));

    eng::InputActions::ReleaseAll();
}
