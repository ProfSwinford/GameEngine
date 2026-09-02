// =============================================================================
//  InputMap.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/input/InputMap.h>

namespace eng {

const char* ToString(ActionState /*state*/) { return "Idle"; }

// TODO: contexts are a STACK, not a single current one - a pause menu opened
// over the game needs menu controls on top with game controls underneath. See
// InputMap.h for why the first context that binds a key wins.
void        InputMap::PushContext(std::string_view) {}
void        InputMap::PopContext() {}
void        InputMap::ClearContexts() {}
std::string InputMap::ActiveContext()  { return {}; }
std::size_t InputMap::ContextDepth()   { return 0; }

// TODO: the questions game code actually asks. Everything returning "not
// pressed" is why nothing responds to the keyboard yet.
bool        InputMap::IsPressed(std::string_view)  { return false; }
bool        InputMap::IsHeld(std::string_view)     { return false; }
bool        InputMap::IsReleased(std::string_view) { return false; }
bool        InputMap::IsDown(std::string_view)     { return false; }
ActionState InputMap::GetState(std::string_view)   { return ActionState::Idle; }

float InputMap::GetAxis(std::string_view) { return 0.0f; }
Vec2  InputMap::GetAxis2D(std::string_view, std::string_view, std::string_view,
                          std::string_view) {
    return Vec2{0.0f, 0.0f};
}

// TODO: read the raw events out of the pump and turn them into named actions.
// This is the function that makes every one of the above start working.
void InputMap::Update(const EventPump& /*pump*/) {}

void InputMap::Bind(std::string_view, std::string_view, std::string_view) {}

// TODO: read the "input" section of config/engine.json, so the controls can be
// rebound without recompiling anything.
void InputMap::LoadBindings(const Json& /*inputSection*/, std::string& outWarnings) {
    outWarnings.clear();
}
void InputMap::ClearBindings() {}

void InputMap::InjectAction(std::string_view, bool) {}
void InputMap::ClearInjectedActions() {}

void InputMap::Snapshot(std::vector<BindingInfo>& out) { out.clear(); }

} // namespace eng
