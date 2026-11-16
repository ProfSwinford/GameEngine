// ============================================================================
//  Tests for the rules that decide WHICH handler is called and WHEN.
//
//  Two separate questions, and both are Unity's answers rather than ones
//  invented here:
//
//    THE NAME. The action "Jump" calls OnJump. Worked out forwards, from the
//    action, because the reverse has no single answer - OnMoveCamera could
//    have come from "Move Camera", "MoveCamera" or "move camera".
//
//    THE MOMENT. performed, plus canceled for a Value action only. So a
//    Button fires once on the press with no second call on the release, and
//    a Value also delivers the final zero. Getting this wrong called every
//    handler twice, which is how it was found: the end-to-end run logged
//    OnJump() two times for one press, because Started and Performed arrive
//    in the same breath when there are no interactions to separate them.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/input/InputActions.h>
#include <engine/input/PlayerInputComponent.h>

#include <string>

TEST_CASE("an action's method name is Unity's, worked out forwards") {
    CHECK(eng::ActionHandlerMethodName("Jump") == "OnJump");
    CHECK(eng::ActionHandlerMethodName("Fire") == "OnFire");

    // ONLY THE FIRST CHARACTER IS UPPER-CASED, which is Unity's rule and not
    // a near-miss for "capitalise each word". Somebody following Unity's
    // documentation gets the method they were promised.
    CHECK(eng::ActionHandlerMethodName("move camera") == "OnMovecamera");
    CHECK(eng::ActionHandlerMethodName("Move Camera") == "OnMoveCamera");

    // Everything that cannot be in an identifier is dropped.
    CHECK(eng::ActionHandlerMethodName("Look-Around") == "OnLookAround");
    CHECK(eng::ActionHandlerMethodName("fire!") == "OnFire");
    CHECK(eng::ActionHandlerMethodName("Use_Item") == "OnUse_Item");

    // A leading digit cannot start an identifier.
    CHECK(eng::ActionHandlerMethodName("2ndary") == "On_2ndary");

    // ALREADY CORRECT IS LEFT ALONE.
    CHECK(eng::ActionHandlerMethodName("OnJump") == "OnOnJump");
}

TEST_CASE("an action no method can be named after is reported, not invented") {
    // Unity throws from inside the input system on this, with a stack trace
    // that names none of the three places anybody would look. An empty
    // string is the caller's cue to say so itself.
    CHECK(eng::ActionHandlerMethodName("!!!").empty());
    CHECK(eng::ActionHandlerMethodName("").empty());
    CHECK(eng::ActionHandlerMethodName("   ").empty());
}

TEST_CASE("two actions that collide on one method name are detectable") {
    // Unity merges these silently and calls one method for both. The names
    // being computed by a plain function is what lets whoever loads a map
    // notice the collision and say so.
    CHECK(eng::ActionHandlerMethodName("Move Camera") ==
          eng::ActionHandlerMethodName("MoveCamera"));
    CHECK(eng::ActionHandlerMethodName("fire") == eng::ActionHandlerMethodName("Fire"));
}

namespace {

// ---------------------------------------------------------------------------
//  THE ENGINE'S OWN RULE, CALLED AND NOT COPIED.
//
//  This was a hand transcription of the three lines inside
//  PlayerInputComponent::Route, written here because Route is private and the
//  decision is independent of scenes and components. It asserted against the
//  copy, so the SHIPPED rule could have been inverted or deleted with every
//  case below still green - which was measured: reinstating the exact
//  double-call regression this file's header describes left the whole suite
//  passing.
//
//  The rule now has a name in the engine - eng::ActionSendsMessage - and this
//  is a thin adapter onto it, so there is one rule and the cases below cannot
//  drift away from it. The event is built the way Dispatch builds one; only
//  the phase and the type decide anything.
// ---------------------------------------------------------------------------
bool Sends(eng::ActionPhase phase, eng::ActionType type) {
    eng::ActionEvent event{};
    event.action = "Anything";
    event.phase  = phase;
    event.type   = type;
    return eng::ActionSendsMessage(event);
}

} // namespace

TEST_CASE("a Button sends once, on the press") {
    CHECK(Sends(eng::ActionPhase::Performed, eng::ActionType::Button));

    // NO SECOND CALL ON THE RELEASE. Unity does not send one either, and a
    // script that wants it polls IsDown - which this engine also has.
    CHECK_FALSE(Sends(eng::ActionPhase::Canceled, eng::ActionType::Button));

    // AND STARTED NEVER SENDS. It arrives in the same breath as Performed,
    // because there are no interactions yet to put a delay between them, so
    // sending on both would call every handler exactly twice.
    CHECK_FALSE(Sends(eng::ActionPhase::Started, eng::ActionType::Button));
}

TEST_CASE("a Value also sends the final zero") {
    CHECK(Sends(eng::ActionPhase::Performed, eng::ActionType::Value));

    // THE ONE THAT MATTERS. A script that started moving when OnMove gave it
    // a direction has to be told when the direction becomes nothing, or it
    // walks into a wall forever.
    CHECK(Sends(eng::ActionPhase::Canceled, eng::ActionType::Value));

    CHECK_FALSE(Sends(eng::ActionPhase::Started, eng::ActionType::Value));
}

TEST_CASE("Pass Through sends its every change") {
    // It raises nothing but Performed in the first place - no phases is what
    // distinguishes it - so the rule reduces to "always".
    CHECK(Sends(eng::ActionPhase::Performed, eng::ActionType::PassThrough));
    CHECK(Sends(eng::ActionPhase::Canceled, eng::ActionType::PassThrough));
}

TEST_CASE("an event carries the type the decision needs") {
    // Carried rather than looked up again. The default matters: a
    // default-constructed event must not claim to be a Value and get a
    // cancel message nobody expected.
    const eng::ActionEvent fresh{};
    CHECK(fresh.type == eng::ActionType::Button);
    CHECK(fresh.phase == eng::ActionPhase::Performed);
    CHECK(fresh.value == doctest::Approx(0.0f));
    CHECK(fresh.vector == eng::Vec2{});
}
