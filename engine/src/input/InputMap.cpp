// =============================================================================
//  InputMap.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. InputMap.h is the specification; read it first.
//
//  Game code asks "does the player want to move up?", never "was W pressed?".
//  This file is the only place the two are connected.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/input/InputMap.h>
#include <engine/platform/EventPump.h>

#include <algorithm>
#include <map>

namespace eng {
namespace{

enum class Device { None, Key, MouseButton };

struct Binding {
    Device device = Device::None;
    int code = 0;
};

struct ActionEntry {

    std::vector<Binding> bindings;
    ActionState state = ActionState::Idle;

    bool downNow, downLast = false;
};

struct Context {
    std::map<std::string, ActionEntry> actions;
};

std::map<std::string, Context> g_contexts;
std::vector<std::string> g_stack;

ActionEntry* FindOwningAction(Device device, int code) {
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); it++) {
        const auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end())
            continue;
        
        for (auto& [name, action] : contextIt->second.actions)
            for (const Binding& binding : action.bindings)
                if (binding.device == device && binding.code == code)
                    return &action;
        
    }
    return nullptr;
}

ActionEntry* FindAction(std::string_view action) {
    const std::string key(action);
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); it++) {
        const auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end())
            continue;
        const auto actionIt = contextIt->second.actions.find(key);
        if (actionIt != contextIt->second.actions.end())
            return &actionIt->second;
    }
    return nullptr;
}

Binding ParseBinding(std::string_view text, std::string& outWarning) {
    Binding binding;

    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        outWarning = "binding '" + std::string(text) +
                     "' is missing its device prefix (expected Key. or Mouse.)";
        return binding;
    }

    const std::string_view device = text.substr(0, dot);
    const std::string name(text.substr(dot + 1));

    if (device == "Key") {
        const int code = EventPump::KeyCodeFromName(name.c_str());
        if (code < 0) {
            outWarning = "there is no key called '" + name + "'";
            return binding;
        }
        binding.device = Device::Key;
        binding.code = code;
        return binding;
    }

    if (device == "Mouse") {
        const int code = EventPump::MouseButtonFromName(name.c_str());
        if (code < 0) {
            outWarning =
                "there is no mouse button called '" + name + "' (try Left, Right or Middle)";
            return binding;
        }
        binding.device = Device::MouseButton;
        binding.code = code;
        return binding;
    }

    outWarning = "unknown device '" + std::string(device) + "' in a binding";
    return binding;
}
} // namespace

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
