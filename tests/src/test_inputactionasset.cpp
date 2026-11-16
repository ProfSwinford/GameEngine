// ============================================================================
//  Tests for eng::InputActionAsset - the .oinput file.
//
//  ALL OF THIS RUNS WITH NO FILESYSTEM, because Deserialize takes a parsed
//  Json and reading the file is the library's job. That split is the house
//  convention for every asset here, and this is what it buys: every refusal
//  and every warn-and-skip gets a case, written as the JSON somebody would
//  actually type, with no asset root and no ENGINE_ASSET_ROOT to set up.
//
//  THE TWO-TIER RULE IS WHAT MOST OF THESE ARE ABOUT. A structural problem
//  refuses the file; a local one warns and skips the one thing that is wrong.
//  Getting that boundary right is the difference between a typo in one
//  binding costing you that binding, and it costing you the other forty.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Json.h>
#include <engine/input/InputActionAsset.h>
#include <engine/input/InputActions.h>

#include <string>

namespace {

// Parses the text and deserializes it, the way the library would.
bool Load(const char* text, eng::InputActionAsset& into, std::string& outError,
          std::string& outWarnings) {
    std::string parseError;
    const eng::Json node = eng::ParseJson(text, parseError);
    if (!parseError.empty()) {
        outError = parseError;
        return false;
    }
    return into.Deserialize(node, outError, outWarnings);
}

// The file from the documentation, as somebody would write it.
constexpr const char* kPlayer = R"({
  "_comment": "the player's controls",
  "inputActions": 1,
  "defaultMap": "Gameplay",
  "maps": [
    {
      "name": "Gameplay",
      "actions": [
        { "name": "Jump",  "type": "Button", "bindings": ["Key.Space"] },
        { "name": "Fire",  "type": "Button", "bindings": ["Mouse.Left", "Key.Return"] },
        { "name": "Pause", "type": "Button", "bindings": ["Key.Escape"] },
        { "name": "Move",  "type": "Value",
          "bindings": [
            { "composite": "2DVector",
              "up": "Key.W",  "down": "Key.S",    "left": "Key.A",    "right": "Key.D" },
            { "composite": "2DVector",
              "up": "Key.Up", "down": "Key.Down", "left": "Key.Left", "right": "Key.Right" }
          ] }
      ]
    },
    {
      "name": "UI",
      "actions": [
        { "name": "Submit", "type": "Button", "bindings": ["Key.Return", "Key.Space"] },
        { "name": "Cancel", "type": "Button", "bindings": ["Key.Escape"] }
      ]
    }
  ]
})";

} // namespace

TEST_CASE("one file holds several maps, and names which one starts current") {
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(kPlayer, asset, error, warnings));
    CHECK(error.empty());
    CHECK(warnings.empty());

    REQUIRE(asset.Maps().size() == 2);
    CHECK(asset.Maps()[0].Name() == "Gameplay");
    CHECK(asset.Maps()[1].Name() == "UI");

    // ORDER IS THE AUTHOR'S. Keyed by name they would come back alphabetical,
    // which would reorder the editor's list under the cursor on every save.
    CHECK(asset.FindMap("UI") == &asset.Maps()[1]);
    CHECK(asset.FindMap("Vehicle") == nullptr);

    REQUIRE(asset.DefaultMap() != nullptr);
    CHECK(asset.DefaultMap()->Name() == "Gameplay");
}

TEST_CASE("the same key means different things in two maps, with nothing to arbitrate") {
    // THE REASON THE FILE HOLDS SEVERAL MAPS. Escape is Pause while playing
    // and Cancel while the menu is up; no context stack is involved, because
    // a map REPLACES rather than layering.
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(kPlayer, asset, error, warnings));

    const eng::InputAction* pause  = asset.FindMap("Gameplay")->Find("Pause");
    const eng::InputAction* cancel = asset.FindMap("UI")->Find("Cancel");
    REQUIRE(pause != nullptr);
    REQUIRE(cancel != nullptr);

    REQUIRE(pause->bindings.size() == 1);
    REQUIRE(cancel->bindings.size() == 1);
    CHECK(pause->bindings[0].control == cancel->bindings[0].control);

    // And an action is found only in its own map.
    CHECK(asset.FindMap("Gameplay")->Find("Cancel") == nullptr);
    CHECK(asset.FindMap("UI")->Find("Pause") == nullptr);
}

TEST_CASE("a string is one control and an object is a composite") {
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(kPlayer, asset, error, warnings));

    const eng::InputAction* fire = asset.FindMap("Gameplay")->Find("Fire");
    REQUIRE(fire->bindings.size() == 2);
    CHECK(fire->bindings[0].shape == eng::InputBinding::Shape::Control);
    CHECK(fire->bindings[0].control.device == eng::ControlDevice::MouseButton);
    CHECK(fire->bindings[1].control.device == eng::ControlDevice::Key);

    const eng::InputAction* move = asset.FindMap("Gameplay")->Find("Move");
    REQUIRE(move->bindings.size() == 2);
    for (const eng::InputBinding& binding : move->bindings) {
        CHECK(binding.shape == eng::InputBinding::Shape::Vector2Composite);
        // Unity's default, and the one GetAxis2D already applies - so the two
        // halves of the engine cannot disagree about what a diagonal is worth.
        CHECK(binding.mode == eng::CompositeMode::DigitalNormalized);
        CHECK(binding.up);
        CHECK(binding.down);
        CHECK(binding.left);
        CHECK(binding.right);
    }
    CHECK(move->bindings[0].up.code != move->bindings[1].up.code);  // W, then Up
}

TEST_CASE("the shape of the value is worked out from the bindings when unstated") {
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(kPlayer, asset, error, warnings));

    // Derived rather than written down twice and allowed to drift. The file
    // says nothing about controlType anywhere in it.
    CHECK(asset.FindMap("Gameplay")->Find("Move")->controlType == eng::ControlType::Vector2);
    CHECK(asset.FindMap("Gameplay")->Find("Jump")->controlType == eng::ControlType::Button);

    eng::InputActionAsset axis;
    REQUIRE(Load(R"({ "maps": [ { "name": "M", "actions": [
        { "name": "Lean", "type": "Value",
          "bindings": [ { "composite": "1DAxis", "positive": "Key.E", "negative": "Key.Q" } ] } ] } ] })",
                 axis, error, warnings));
    CHECK(axis.FindMap("M")->Find("Lean")->controlType == eng::ControlType::Axis);
}

TEST_CASE("Unity's composite mode numbers are accepted, and words are written back") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    // A file exported from Unity, or written by somebody reading Unity's docs,
    // spells the mode as 0, 1 or 2.
    REQUIRE(Load(R"({ "maps": [ { "name": "M", "actions": [
        { "name": "A", "bindings": [ { "composite": "2DVector", "mode": 2, "up": "Key.W" } ] },
        { "name": "B", "bindings": [ { "composite": "2DVector", "mode": 1, "up": "Key.W" } ] },
        { "name": "C", "bindings": [ { "composite": "2DVector", "mode": "digital", "up": "Key.W" } ] }
    ] } ] })",
                 asset, error, warnings));
    CHECK(warnings.empty());

    const eng::ActionMap* map = asset.FindMap("M");
    CHECK(map->Find("A")->bindings[0].mode == eng::CompositeMode::Analog);
    CHECK(map->Find("B")->bindings[0].mode == eng::CompositeMode::Digital);
    CHECK(map->Find("C")->bindings[0].mode == eng::CompositeMode::Digital);
}

TEST_CASE("a structural problem refuses the whole file") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    SUBCASE("not an object") {
        CHECK_FALSE(Load(R"([])", asset, error, warnings));
        CHECK_FALSE(error.empty());
    }
    SUBCASE("no maps array") {
        CHECK_FALSE(Load(R"({ "inputActions": 1 })", asset, error, warnings));
        // The message says what the thing IS, not only that it is absent.
        CHECK(error.find("maps") != std::string::npos);
    }
    SUBCASE("maps is empty") {
        CHECK_FALSE(Load(R"({ "maps": [] })", asset, error, warnings));
    }
    SUBCASE("a map with no name") {
        CHECK_FALSE(Load(R"({ "maps": [ { "actions": [] } ] })", asset, error, warnings));
        // Because the name is what SwitchCurrentActionMap asks for, and a map
        // nothing can name is a map nothing can reach.
        CHECK(error.find("name") != std::string::npos);
    }
    SUBCASE("a map whose name is empty") {
        CHECK_FALSE(Load(R"({ "maps": [ { "name": "", "actions": [] } ] })", asset,
                         error, warnings));
    }
}

TEST_CASE("a typo in one binding costs that binding and nothing else") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    REQUIRE(Load(R"({ "maps": [ { "name": "Gameplay", "actions": [
        { "name": "Jump",  "bindings": ["Key.Spacebar"] },
        { "name": "Fire",  "bindings": ["Mouse.Left", "Keyboard.Q", "Key.Return"] },
        { "name": "Crouch","bindings": ["Key.Left Ctrl"] }
    ] } ] })",
                 asset, error, warnings));

    CHECK(error.empty());
    CHECK_FALSE(warnings.empty());

    const eng::ActionMap* map = asset.FindMap("Gameplay");

    // "Spacebar" is not a key, so Jump keeps its action and loses its binding.
    // The action still EXISTS - a script's OnJump() is still a valid handler,
    // it just cannot be reached until the file is fixed.
    REQUIRE(map->Find("Jump") != nullptr);
    CHECK(map->Find("Jump")->bindings.empty());

    // The two good bindings on Fire survive the bad one between them.
    REQUIRE(map->Find("Fire") != nullptr);
    CHECK(map->Find("Fire")->bindings.size() == 2);

    // And the action after the broken one is untouched.
    //
    // "Left Ctrl" IS THE SPELLING, not "LCtrl" - these are SDL's own key
    // names, whatever SDL_GetScancodeFromName answers to, and the modifiers
    // are two words. Asserted here rather than left to be discovered, because
    // it is the first thing somebody gets wrong by hand and the only symptom
    // is a binding that quietly does nothing.
    REQUIRE(map->Find("Crouch") != nullptr);
    CHECK(map->Find("Crouch")->bindings.size() == 1);

    CHECK(warnings.find("Spacebar") != std::string::npos);
    CHECK(warnings.find("Keyboard") != std::string::npos);
}

TEST_CASE("a duplicate name is skipped rather than refused") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    REQUIRE(Load(R"({ "maps": [
        { "name": "Gameplay", "actions": [ { "name": "Jump", "bindings": ["Key.Space"] } ] },
        { "name": "Gameplay", "actions": [ { "name": "Fire", "bindings": ["Key.Return"] } ] },
        { "name": "UI",       "actions": [
            { "name": "Submit", "bindings": ["Key.Return"] },
            { "name": "Submit", "bindings": ["Key.Space"] } ] }
    ] })",
                 asset, error, warnings));

    // FindMap would have to pick one and nothing in the file says which.
    REQUIRE(asset.Maps().size() == 2);
    CHECK(asset.FindMap("Gameplay")->Find("Jump") != nullptr);
    CHECK(asset.FindMap("Gameplay")->Find("Fire") == nullptr);

    REQUIRE(asset.FindMap("UI")->Actions().size() == 1);
    CHECK(asset.FindMap("UI")->Find("Submit")->bindings[0].control.device ==
          eng::ControlDevice::Key);

    CHECK(warnings.find("Gameplay") != std::string::npos);
}

TEST_CASE("a composite with no usable part is dropped, one with some is kept") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    REQUIRE(Load(R"({ "maps": [ { "name": "M", "actions": [
        { "name": "Dead",  "bindings": [ { "composite": "2DVector", "up": "Key.Nonsense" } ] },
        { "name": "Half",  "bindings": [ { "composite": "2DVector", "up": "Key.W", "down": "Key.S" } ] },
        { "name": "Alien", "bindings": [ { "composite": "3DVector", "up": "Key.W" } ] }
    ] } ] })",
                 asset, error, warnings));

    const eng::ActionMap* map = asset.FindMap("M");
    CHECK(map->Find("Dead")->bindings.empty());

    // A stick with no left and no right is odd, not wrong - it is how a
    // two-key up/down control is spelled.
    REQUIRE(map->Find("Half")->bindings.size() == 1);
    CHECK(map->Find("Half")->bindings[0].up);
    CHECK_FALSE(map->Find("Half")->bindings[0].left);

    CHECK(map->Find("Alien")->bindings.empty());
    CHECK(warnings.find("3DVector") != std::string::npos);
}

TEST_CASE("defaultMap naming a map that is not there falls back and says so") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    REQUIRE(Load(R"({ "defaultMap": "Vehicle", "maps": [
        { "name": "Gameplay", "actions": [ { "name": "Jump", "bindings": ["Key.Space"] } ] }
    ] })",
                 asset, error, warnings));

    REQUIRE(asset.DefaultMap() != nullptr);
    CHECK(asset.DefaultMap()->Name() == "Gameplay");
    CHECK(warnings.find("Vehicle") != std::string::npos);
}

TEST_CASE("a file with one map does not have to name it twice") {
    eng::InputActionAsset asset;
    std::string           error, warnings;

    REQUIRE(Load(R"({ "maps": [
        { "name": "Only", "actions": [ { "name": "Go", "bindings": ["Key.Space"] } ] } ] })",
                 asset, error, warnings));

    CHECK(asset.DefaultMapName().empty());
    REQUIRE(asset.DefaultMap() != nullptr);
    CHECK(asset.DefaultMap()->Name() == "Only");
}

TEST_CASE("the default a new file starts as is usable with no warnings") {
    // WHAT THE EDITOR WRITES WHEN YOU MAKE ONE. An empty template would be
    // correct and useless; this one binds what config/engine.json binds, so a
    // brand-new project can be played straight away. And because it ships as
    // prose with escapes and nested composites in it, the one thing it must
    // not be allowed to do is quietly stop parsing - which is the whole
    // reason it lives in the engine rather than in the editor, where no test
    // could see it.
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(eng::DefaultActionsJson().c_str(), asset, error, warnings));
    CHECK(error.empty());
    CHECK(warnings == "");    // spelled out, so a failure prints the text

    REQUIRE(asset.Maps().size() == 2);
    REQUIRE(asset.DefaultMap() != nullptr);
    CHECK(asset.DefaultMap()->Name() == "Gameplay");
    CHECK(asset.FindMap("Menu") != nullptr);

    const eng::ActionMap* play = asset.FindMap("Gameplay");

    // THE FOUR MOVE ACTIONS ARE ONE, which is the point of the format and so
    // has to be what the default demonstrates.
    CHECK(play->Find("MoveLeft") == nullptr);
    const eng::InputAction* move = play->Find("Move");
    REQUIRE(move != nullptr);
    CHECK(move->controlType == eng::ControlType::Vector2);
    REQUIRE(move->bindings.size() == 2);   // WASD and the arrows
    for (const eng::InputBinding& binding : move->bindings) {
        CHECK(binding.shape == eng::InputBinding::Shape::Vector2Composite);
        CHECK(binding.up);
        CHECK(binding.down);
        CHECK(binding.left);
        CHECK(binding.right);
    }

    // Everything else config/engine.json's gameplay context binds.
    for (const char* name : {"Jump", "Fire", "Interact", "Pause", "Quit"}) {
        const eng::InputAction* action = play->Find(name);
        REQUIRE_MESSAGE(action != nullptr, name);
        CHECK(action->type == eng::ActionType::Button);
        CHECK_FALSE(action->bindings.empty());
    }

    // And the menu map, with the overlap that is the reason both are here.
    const eng::ActionMap* menu = asset.FindMap("Menu");
    REQUIRE(menu->Find("Back") != nullptr);
    REQUIRE(menu->Find("Confirm") != nullptr);
    REQUIRE(menu->Find("Navigate") != nullptr);
    CHECK(play->Find("Pause")->bindings[0].control ==
          menu->Find("Back")->bindings[0].control);

    // EVERY ACTION CAN BE NAMED AFTER. An action whose name yields no legal
    // method would be a binding nothing could ever answer, and the default
    // must not ship one.
    for (const eng::ActionMap& map : asset.Maps()) {
        for (const eng::InputAction& action : map.Actions()) {
            CHECK_FALSE(eng::ActionHandlerMethodName(action.name).empty());
        }
    }
}

TEST_CASE("the default survives being saved over by the editor") {
    // The Actions window writes through Serialize, so anything the template
    // says that Serialize cannot reproduce is lost the first time somebody
    // presses Save. The "_" prose keys are expected to go - nlohmann keeps no
    // comments and Serialize writes a clean document - but nothing that
    // BINDS anything may change.
    eng::InputActionAsset first;
    std::string           error, warnings;
    REQUIRE(Load(eng::DefaultActionsJson().c_str(), first, error, warnings));

    eng::Json written;
    REQUIRE(first.Serialize(written));

    eng::InputActionAsset second;
    std::string           error2, warnings2;
    REQUIRE(second.Deserialize(written, error2, warnings2));
    CHECK(warnings2.empty());

    REQUIRE(second.Maps().size() == first.Maps().size());
    for (std::size_t m = 0; m < first.Maps().size(); ++m) {
        CHECK(second.Maps()[m].Name() == first.Maps()[m].Name());
        REQUIRE(second.Maps()[m].Actions().size() == first.Maps()[m].Actions().size());
        for (std::size_t a = 0; a < first.Maps()[m].Actions().size(); ++a) {
            const eng::InputAction& was = first.Maps()[m].Actions()[a];
            const eng::InputAction& now = second.Maps()[m].Actions()[a];
            CHECK(now.name == was.name);
            CHECK(now.type == was.type);
            CHECK(now.controlType == was.controlType);
            CHECK(now.bindings.size() == was.bindings.size());
        }
    }
    CHECK(second.DefaultMapName() == first.DefaultMapName());
}

TEST_CASE("what is written out reads back as the same thing") {
    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(Load(kPlayer, asset, error, warnings));

    eng::Json written;
    REQUIRE(asset.Serialize(written));

    eng::InputActionAsset again;
    std::string           error2, warnings2;
    REQUIRE(again.Deserialize(written, error2, warnings2));

    // NO WARNINGS THE SECOND TIME. A round trip that re-reports the problems
    // it just wrote out would mean Serialize had produced something this
    // parser does not fully accept.
    CHECK(warnings2.empty());

    eng::Json writtenAgain;
    REQUIRE(again.Serialize(writtenAgain));
    CHECK(written == writtenAgain);

    // And spot-check that the controls really survived rather than both
    // rounds producing the same empty thing.
    CHECK(again.Maps().size() == 2);
    CHECK(again.FindMap("Gameplay")->Find("Move")->bindings.size() == 2);
    CHECK(again.FindMap("Gameplay")->Find("Fire")->bindings[0].control.device ==
          eng::ControlDevice::MouseButton);
}

// ---------------------------------------------------------------------------
//  WHAT THE ACTIONS WINDOW CAN BUILD, read back through the same parser.
//
//  Every case above starts from a file somebody wrote by hand, and that is
//  the wrong starting point for this question. The window does not edit TEXT.
//  It edits an InputActionAsset through Maps() and Actions() and then writes
//  it with Serialize - so the states it can reach are the ones reachable by
//  push_back-ing a DEFAULT ActionMap, InputAction or InputBinding, and those
//  are not states a hand-written file is ever likely to be in. A round-trip
//  test fed only valid prose cannot see any of them.
//
//  Each of these builds the asset exactly as one of the window's three "+"
//  buttons builds it, with no editor linked, and asks the one question that
//  matters: does reading back what Serialize wrote give the same asset?
// ---------------------------------------------------------------------------

// The window's own "+" buttons, as plain calls on the asset.
namespace {

eng::ActionMap& AddMap(eng::InputActionAsset& asset, const char* name) {
    eng::ActionMap added;
    added.SetName(name);
    asset.Maps().push_back(added);
    return asset.Maps().back();
}

eng::InputAction& AddAction(eng::ActionMap& map, const char* name) {
    eng::InputAction added;
    added.name = name;
    map.Actions().push_back(added);
    return map.Actions().back();
}

} // namespace

TEST_CASE("a binding added and not yet filled in survives a save") {
    // "+ binding" pushes a DEFAULT InputBinding and leaves the control field
    // to fill it in. Pressing Save before typing anything is the ordinary
    // mid-edit state - and Unity keeps that binding too, as <No Binding>.
    eng::InputActionAsset asset;
    eng::ActionMap&       map = AddMap(asset, "Gameplay");
    AddAction(map, "Jump").bindings.push_back(eng::InputBinding{});

    eng::Json written;
    REQUIRE(asset.Serialize(written));

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(again.Deserialize(written, error, warnings));

    REQUIRE(again.FindMap("Gameplay") != nullptr);
    REQUIRE(again.FindMap("Gameplay")->Find("Jump") != nullptr);
    REQUIRE(again.FindMap("Gameplay")->Find("Jump")->bindings.size() == 1);
    CHECK_FALSE(again.FindMap("Gameplay")->Find("Jump")->bindings[0].control);

    // NOTHING TO WARN ABOUT. An empty binding is not a typo - there is no
    // text in it to be wrong - so re-reading one is not a complaint.
    CHECK(warnings.empty());
}

TEST_CASE("a composite with nothing typed into it yet survives a save") {
    // Switching a binding to 2DVector in the Binding dropdown gives four
    // empty part fields, and the same is true of 1DAxis's two.
    eng::InputActionAsset asset;
    eng::ActionMap&       map = AddMap(asset, "Gameplay");

    eng::InputBinding stick;
    stick.shape = eng::InputBinding::Shape::Vector2Composite;
    AddAction(map, "Move").bindings.push_back(stick);

    eng::InputBinding axis;
    axis.shape = eng::InputBinding::Shape::AxisComposite;
    AddAction(map, "Lean").bindings.push_back(axis);

    eng::Json written;
    REQUIRE(asset.Serialize(written));

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(again.Deserialize(written, error, warnings));

    REQUIRE(again.FindMap("Gameplay") != nullptr);
    REQUIRE(again.FindMap("Gameplay")->Find("Move") != nullptr);
    REQUIRE(again.FindMap("Gameplay")->Find("Move")->bindings.size() == 1);
    CHECK(again.FindMap("Gameplay")->Find("Move")->bindings[0].shape ==
          eng::InputBinding::Shape::Vector2Composite);

    REQUIRE(again.FindMap("Gameplay")->Find("Lean") != nullptr);
    REQUIRE(again.FindMap("Gameplay")->Find("Lean")->bindings.size() == 1);
    CHECK(again.FindMap("Gameplay")->Find("Lean")->bindings[0].shape ==
          eng::InputBinding::Shape::AxisComposite);

    CHECK(warnings.empty());
}

// ---------------------------------------------------------------------------
//  THE SAVE CHECK, each rule paired with the parser behaviour it predicts.
//
//  Validate's rules are Deserialize's rules restated in another function,
//  which means the two can drift apart. So every case here asserts BOTH
//  halves: that Validate says no, and that writing the asset out and reading
//  it back really does lose the thing it said would be lost.
//
//  A rule that stopped matching the parser fails the second half. A parser
//  that stopped being strict fails it too - and then this is the case that
//  says the rule can go.
// ---------------------------------------------------------------------------

namespace {

// Writes the asset out and reads it back, which is what Save does.
bool RoundTrip(const eng::InputActionAsset& from, eng::InputActionAsset& into,
               std::string& outError, std::string& outWarnings) {
    eng::Json written;
    REQUIRE(from.Serialize(written));
    return into.Deserialize(written, outError, outWarnings);
}

} // namespace

TEST_CASE("a map left without a name is caught before it costs the whole file") {
    eng::InputActionAsset asset;
    AddAction(AddMap(asset, "Gameplay"), "Jump");
    AddMap(asset, "");                       // the Name field cleared

    std::string problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK(problems.find("action map 2 has no name") != std::string::npos);

    // THE WORST OF THE FOUR, and the reason the check is a refusal and not a
    // warning: this one does not thin the file out, it loses all of it.
    // Gameplay goes too, and the window cannot open what it just wrote.
    eng::InputActionAsset again;
    std::string           error, warnings;
    CHECK_FALSE(RoundTrip(asset, again, error, warnings));
    CHECK_FALSE(error.empty());
}

TEST_CASE("two maps by one name are caught before one of them is dropped") {
    eng::InputActionAsset asset;
    AddAction(AddMap(asset, "New Map"), "Jump");
    AddAction(AddMap(asset, "New Map"), "Fire");

    std::string problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK(problems.find("two action maps called 'New Map'") != std::string::npos);

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(RoundTrip(asset, again, error, warnings));
    REQUIRE(again.Maps().size() == 1);
    CHECK(again.FindMap("New Map")->Find("Fire") == nullptr);   // the loss
}

TEST_CASE("an action left without a name is caught before it is dropped") {
    eng::InputActionAsset asset;
    eng::ActionMap&       map = AddMap(asset, "Gameplay");
    AddAction(map, "Jump");
    AddAction(map, "");

    std::string problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK(problems.find("'Gameplay': action 2 has no name") != std::string::npos);

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(RoundTrip(asset, again, error, warnings));
    REQUIRE(again.FindMap("Gameplay") != nullptr);
    CHECK(again.FindMap("Gameplay")->Actions().size() == 1);
}

TEST_CASE("two actions in one map by one name are caught before one is dropped") {
    eng::InputActionAsset asset;
    eng::ActionMap&       map = AddMap(asset, "Gameplay");
    AddAction(map, "NewAction");
    AddAction(map, "NewAction");

    std::string problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK(problems.find("two actions called 'NewAction'") != std::string::npos);

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(RoundTrip(asset, again, error, warnings));
    REQUIRE(again.FindMap("Gameplay") != nullptr);
    CHECK(again.FindMap("Gameplay")->Actions().size() == 1);
}

TEST_CASE("the same name in two different maps is not a problem") {
    // Unity's rule and this engine's: names are unique WITHIN a map. Two maps
    // may each have a Fire and they are different actions - which is most of
    // the reason one file holds several maps.
    eng::InputActionAsset asset;
    AddAction(AddMap(asset, "Gameplay"), "Fire");
    AddAction(AddMap(asset, "UI"), "Fire");

    std::string problems;
    CHECK(asset.Validate(problems));
    CHECK(problems.empty());
}

TEST_CASE("half-finished work saves, because half-finished is the normal state") {
    // None of this would be lost, so none of it is Validate's business: an
    // action with no bindings at all, a binding with no control chosen yet, a
    // composite with nothing typed into it. Refusing to save these would make
    // the window useless for the state it is in most of the time.
    eng::InputActionAsset asset;
    eng::ActionMap&       map = AddMap(asset, "Gameplay");
    AddAction(map, "Jump");
    AddAction(map, "Fire").bindings.push_back(eng::InputBinding{});

    eng::InputBinding stick;
    stick.shape = eng::InputBinding::Shape::Vector2Composite;
    AddAction(map, "Move").bindings.push_back(stick);

    std::string problems;
    CHECK(asset.Validate(problems));
    CHECK(problems.empty());

    eng::InputActionAsset again;
    std::string           error, warnings;
    REQUIRE(RoundTrip(asset, again, error, warnings));
    CHECK(warnings.empty());
    REQUIRE(again.FindMap("Gameplay") != nullptr);
    CHECK(again.FindMap("Gameplay")->Actions().size() == 3);
}

TEST_CASE("a file with no maps left in it is caught, since the parser refuses one") {
    eng::InputActionAsset asset;
    std::string           problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK_FALSE(problems.empty());
}

TEST_CASE("every problem is reported, not the first one") {
    // Somebody who added two maps and two actions wants all four names at
    // once, rather than fixing one, pressing Save, and being told the next.
    eng::InputActionAsset asset;
    eng::ActionMap&       first = AddMap(asset, "Gameplay");
    AddAction(first, "Fire");
    AddAction(first, "Fire");
    AddMap(asset, "Gameplay");
    AddMap(asset, "");

    std::string problems;
    CHECK_FALSE(asset.Validate(problems));
    CHECK(problems.find("two actions called 'Fire'") != std::string::npos);
    CHECK(problems.find("two action maps called 'Gameplay'") != std::string::npos);
    CHECK(problems.find("action map 3 has no name") != std::string::npos);
}
