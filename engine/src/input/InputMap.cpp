// =============================================================================
//  InputMap.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. InputMap.h is the specification; read it first.
//
//  Game code asks "does the player want to move up?", never "was W pressed?".
//  This file is the only place the two are connected.
// =============================================================================

#include <engine/input/InputMap.h>

namespace eng {

// Turns an action's state into a readable name, for the log.
const char* ToString(ActionState /*state*/) {
    return "Idle";
}

// Pushes a set of bindings on top of the stack - a pause menu opened over the
// game. A key is looked up from the top down, and the first context that binds
// it wins, so the menu can take Escape while W still reaches the game.
void InputMap::PushContext(std::string_view /*context*/) {
}

// Removes the top set of bindings, going back to whatever was underneath.
void InputMap::PopContext() {
}

// Empties the stack entirely.
void InputMap::ClearContexts() {
}

// The name of the set of bindings currently on top.
std::string InputMap::ActiveContext() {
    return {};
}

// How many sets of bindings are stacked up.
std::size_t InputMap::ContextDepth() {
    return 0;
}

// Did this action go down THIS step? True for one step only - the right question
// for a jump.
bool InputMap::IsPressed(std::string_view /*action*/) {
    return false;
}

// Has this action been held since before this step?
bool InputMap::IsHeld(std::string_view /*action*/) {
    return false;
}

// Did this action come up THIS step?
bool InputMap::IsReleased(std::string_view /*action*/) {
    return false;
}

// Is this action down at all, whether it started this step or earlier? The right
// question for walking.
bool InputMap::IsDown(std::string_view /*action*/) {
    return false;
}

// The full state of an action, for code that needs to tell the four apart.
ActionState InputMap::GetState(std::string_view /*action*/) {
    return ActionState::Idle;
}

// An action as a number: 1 when it is down, 0 when it is not.
float InputMap::GetAxis(std::string_view /*action*/) {
    return 0.0f;
}

// Four actions as one direction, already normalised - so holding two keys does
// not move a character diagonally about 40% faster than one key does.
Vec2 InputMap::GetAxis2D(std::string_view /*negX*/, std::string_view /*posX*/,
                         std::string_view /*negY*/, std::string_view /*posY*/) {
    return Vec2{};
}

// Reads this frame's raw events and works out what every action is now doing.
// This is the function that makes all the questions above start answering.
void InputMap::Update(const EventPump& /*pump*/) {
}

// Binds one key to one action inside one context, from code rather than a file.
void InputMap::Bind(std::string_view /*context*/, std::string_view /*action*/,
                    std::string_view /*binding*/) {
}

// Reads the "input" section of the settings file, so the controls can be
// rebound without recompiling anything. Bad entries are reported in
// outWarnings rather than silently ignored.
void InputMap::LoadBindings(const Json& /*inputSection*/, std::string& /*outWarnings*/) {
}

// Forgets every binding.
void InputMap::ClearBindings() {
}

// Pretends an action was pressed or released. This is what lets an automatic
// playthrough drive the game through the same path a keyboard does, rather than
// going round it.
void InputMap::InjectAction(std::string_view /*action*/, bool /*down*/) {
}

// Stops pretending, handing control back to the real keyboard.
void InputMap::ClearInjectedActions() {
}

// Lists every binding, for anything that wants to show the current controls.
void InputMap::Snapshot(std::vector<BindingInfo>& /*out*/) {
}

} // namespace eng
