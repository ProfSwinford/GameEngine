// ============================================================================
//  InputMap.cpp - named actions on top of raw key codes. See InputMap.h.
//
//  This is the last file in the engine that knows a key has a number. Nothing
//  above it does.
//
//  THE DATA
//    g_contexts   every context that has bindings, looked up by name
//    g_stack      which contexts are currently active, bottom of the stack first
//
//  std::map is used rather than std::unordered_map so that the editor's
//  binding list comes out in alphabetical order without having to sort it.
//  There are a few dozen actions at most, so the speed difference is
//  irrelevant here.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/input/InputMap.h>
#include <engine/platform/EventPump.h>

#include <algorithm>
#include <map>

namespace eng {
namespace {

// Which kind of thing a binding refers to.
enum class Device { None, Key, MouseButton };

struct Binding {
    Device device = Device::None;
    int    code   = 0;      // the key number or the mouse button number
};

struct ActionEntry {
    // One action can have several bindings, which is how "MoveLeft" ends up
    // on both A and the left arrow.
    std::vector<Binding> bindings;

    ActionState state = ActionState::Idle;

    // Pressed/Held/Released are worked out by comparing these two. Keeping
    // last frame's value is the whole trick, and it is why the engine does not
    // depend on the operating system's key-repeat rate - which is a personal
    // setting that differs from machine to machine.
    bool downNow  = false;
    bool downLast = false;
};

struct Context {
    std::map<std::string, ActionEntry> actions;
};

std::map<std::string, Context> g_contexts;
std::vector<std::string>       g_stack;

// Finds the action that owns a physical key, searching from the top of the
// stack downwards and stopping at the first match. This function IS the
// shadowing rule described in the header.
ActionEntry* FindOwningAction(Device device, int code) {
    // rbegin/rend walk the vector backwards, i.e. from the top of the stack.
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); ++it) {
        const auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end()) {
            continue;
        }
        for (auto& [name, action] : contextIt->second.actions) {
            for (const Binding& binding : action.bindings) {
                if (binding.device == device && binding.code == code) {
                    return &action;
                }
            }
        }
    }
    return nullptr;
}

// Finds an action by name, again from the top of the stack down.
ActionEntry* FindAction(std::string_view action) {
    const std::string key(action);
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); ++it) {
        const auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end()) {
            continue;
        }
        const auto actionIt = contextIt->second.actions.find(key);
        if (actionIt != contextIt->second.actions.end()) {
            return &actionIt->second;
        }
    }
    return nullptr;
}

// Turns "Key.Space" or "Mouse.Left" into a Binding.
Binding ParseBinding(std::string_view text, std::string& outWarning) {
    Binding binding;

    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        outWarning = "binding '" + std::string(text) +
                     "' is missing its device prefix (expected Key. or Mouse.)";
        return binding;
    }

    const std::string_view device = text.substr(0, dot);
    const std::string      name(text.substr(dot + 1));

    if (device == "Key") {
        const int code = EventPump::KeyCodeFromName(name.c_str());
        if (code < 0) {
            outWarning = "there is no key called '" + name + "'";
            return binding;
        }
        binding.device = Device::Key;
        binding.code   = code;
        return binding;
    }

    if (device == "Mouse") {
        const int code = EventPump::MouseButtonFromName(name.c_str());
        if (code < 0) {
            outWarning = "there is no mouse button called '" + name +
                         "' (try Left, Right or Middle)";
            return binding;
        }
        binding.device = Device::MouseButton;
        binding.code   = code;
        return binding;
    }

    outWarning = "unknown device '" + std::string(device) + "' in a binding";
    return binding;
}

} // namespace

const char* ToString(ActionState state) {
    switch (state) {
        case ActionState::Idle:     return "Idle";
        case ActionState::Pressed:  return "Pressed";
        case ActionState::Held:     return "Held";
        case ActionState::Released: return "Released";
    }
    return "?";
}

void InputMap::PushContext(std::string_view context) {
    g_stack.emplace_back(context);
}

void InputMap::PopContext() {
    if (g_stack.empty()) {
        ENGINE_LOG_WARN(Channels::kInput, "PopContext called when no context is active");
        return;
    }
    g_stack.pop_back();
}

void InputMap::ClearContexts() { g_stack.clear(); }

std::string InputMap::ActiveContext() {
    return g_stack.empty() ? std::string{} : g_stack.back();
}

std::size_t InputMap::ContextDepth() { return g_stack.size(); }

bool InputMap::IsPressed(std::string_view action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Pressed;
}

bool InputMap::IsHeld(std::string_view action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Held;
}

bool InputMap::IsReleased(std::string_view action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Released;
}

bool InputMap::IsDown(std::string_view action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr &&
           (entry->state == ActionState::Pressed || entry->state == ActionState::Held);
}

ActionState InputMap::GetState(std::string_view action) {
    const ActionEntry* entry = FindAction(action);
    return (entry != nullptr) ? entry->state : ActionState::Idle;
}

float InputMap::GetAxis(std::string_view action) {
    return IsDown(action) ? 1.0f : 0.0f;
}

Vec2 InputMap::GetAxis2D(std::string_view negX, std::string_view posX,
                         std::string_view negY, std::string_view posY) {
    const Vec2 raw{GetAxis(posX) - GetAxis(negX), GetAxis(posY) - GetAxis(negY)};

    // Holding right and up at once gives (1, 1), which is about 1.41 units
    // long - so a diagonal would be 41% faster than a straight line. Cutting
    // it back to length 1 fixes that. Normalized() already returns (0, 0) for
    // a zero-length vector, so no special case is needed for "no keys held".
    return raw.Normalized();
}

void InputMap::Update(const EventPump& pump) {
    // Step 1: this frame's "down" becomes last frame's.
    for (auto& [contextName, context] : g_contexts) {
        for (auto& [actionName, action] : context.actions) {
            action.downLast = action.downNow;
        }
    }

    // Step 2: apply this frame's events.
    for (std::size_t i = 0; i < pump.Count(); ++i) {
        // Anything the editor's GUI claimed never reaches the game.
        if (pump.WasConsumed(i)) {
            continue;
        }

        const RawEvent& event = pump.At(i);
        ActionEntry*    entry = nullptr;

        switch (event.kind) {
            case RawEventKind::KeyDown:
                entry = FindOwningAction(Device::Key, event.code);
                if (entry != nullptr) { entry->downNow = true; }
                break;
            case RawEventKind::KeyUp:
                entry = FindOwningAction(Device::Key, event.code);
                if (entry != nullptr) { entry->downNow = false; }
                break;
            case RawEventKind::MouseButtonDown:
                entry = FindOwningAction(Device::MouseButton, event.code);
                if (entry != nullptr) { entry->downNow = true; }
                break;
            case RawEventKind::MouseButtonUp:
                entry = FindOwningAction(Device::MouseButton, event.code);
                if (entry != nullptr) { entry->downNow = false; }
                break;
            default:
                break;
        }
    }

    // Step 3: turn the two booleans into a state.
    for (auto& [contextName, context] : g_contexts) {
        for (auto& [actionName, action] : context.actions) {
            if (action.downNow && !action.downLast) {
                action.state = ActionState::Pressed;
            } else if (action.downNow) {
                action.state = ActionState::Held;
            } else if (action.downLast) {
                action.state = ActionState::Released;
            } else {
                action.state = ActionState::Idle;
            }
        }
    }
}

void InputMap::Bind(std::string_view context, std::string_view action,
                    std::string_view binding) {
    std::string   warning;
    const Binding parsed = ParseBinding(binding, warning);
    if (!warning.empty()) {
        ENGINE_LOG_WARN(Channels::kInput, "{}", warning);
        return;
    }
    if (parsed.device == Device::None) {
        return;
    }
    // operator[] on a std::map creates the entry if it is not there yet, which
    // is exactly what is wanted for "add a binding to this action".
    g_contexts[std::string(context)].actions[std::string(action)].bindings.push_back(parsed);
}

void InputMap::LoadBindings(const Json& inputSection, std::string& outWarnings) {
    if (!inputSection.is_object()) {
        ENGINE_LOG_WARN(Channels::kInput,
                        "the settings file has no \"input\" section, so nothing is bound");
        return;
    }

    const auto contextsIt = inputSection.find("contexts");
    if (contextsIt == inputSection.end() || !contextsIt->is_object()) {
        outWarnings += "input section has no \"contexts\"\n";
        return;
    }

    // items() walks a JSON object as name/value pairs, which is how the
    // context names and action names are discovered rather than hardcoded.
    for (const auto& [contextName, actions] : contextsIt->items()) {
        if (!actions.is_object()) {
            outWarnings += "input.contexts." + contextName + " should be a list of actions\n";
            continue;
        }

        Context& context = g_contexts[contextName];

        for (const auto& [actionName, bindings] : actions.items()) {
            ActionEntry& entry = context.actions[actionName];

            if (!bindings.is_array()) {
                outWarnings += "input.contexts." + contextName + "." + actionName +
                               " should be a list like [\"Key.A\"]\n";
                continue;
            }

            for (const Json& item : bindings) {
                if (!item.is_string()) {
                    continue;
                }
                std::string   warning;
                const Binding parsed = ParseBinding(item.get<std::string>(), warning);
                if (!warning.empty()) {
                    // Named with the context and the action, so the message
                    // says which line of the file to go and look at.
                    const std::string full =
                        contextName + "." + actionName + ": " + warning;
                    ENGINE_LOG_WARN(Channels::kInput, "{}", full);
                    outWarnings += full + "\n";
                    continue;
                }
                if (parsed.device != Device::None) {
                    entry.bindings.push_back(parsed);
                }
            }
        }

        ENGINE_LOG_INFO(Channels::kInput, "input context '{}': {} action(s)", contextName,
                        context.actions.size());
    }
}

void InputMap::InjectAction(std::string_view action, bool down) {
    if (ActionEntry* entry = FindAction(action); entry != nullptr) {
        entry->downNow = down;
    }
}

void InputMap::ClearInjectedActions() {
    // Releases everything. An autopilot that stops steering must not leave the
    // player walking into a wall forever.
    for (auto& [contextName, context] : g_contexts) {
        for (auto& [actionName, entry] : context.actions) {
            entry.downNow = false;
        }
    }
}

void InputMap::ClearBindings() {
    g_contexts.clear();
    g_stack.clear();
}

void InputMap::Snapshot(std::vector<BindingInfo>& out) {
    out.clear();
    for (const auto& [contextName, context] : g_contexts) {
        for (const auto& [actionName, action] : context.actions) {
            if (action.bindings.empty()) {
                out.push_back({contextName, actionName, "<not bound>"});
                continue;
            }
            for (const Binding& binding : action.bindings) {
                std::string text;
                switch (binding.device) {
                    case Device::Key:
                        text = std::string("Key.") + EventPump::KeyName(binding.code);
                        break;
                    case Device::MouseButton:
                        text = "Mouse." + std::to_string(binding.code);
                        break;
                    case Device::None:
                        text = "<none>";
                        break;
                }
                out.push_back({contextName, actionName, text});
            }
        }
    }
}

} // namespace eng
