// WEEK 8 - the input abstraction layer. See InputMap.h for the two recorded
// decisions (context stack resolution order, radial dead zone).
//
// This file knows about key codes. Nothing above it does.

#include <engine/core/Assert.h>
#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/input/InputMap.h>
#include <engine/platform/EventPump.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace eng {
namespace {

// What a binding string resolved to. Kept as a small POD so the whole binding
// table is contiguous and comparing one is an integer compare.
enum class Device : u8 { None, Key, MouseButton };

struct Binding {
    Device device = Device::None;
    i32    code   = 0;
};

struct ActionEntry {
    std::string          name;         // kept for the editor's binding table
    std::vector<Binding> bindings;
    ActionState          state    = ActionState::Idle;
    f32                  axis     = 0.0f;
    bool                 downNow  = false;
    bool                 downLast = false;
};

struct Context {
    std::string                        name;
    std::map<u64, ActionEntry>         actions;   // keyed by StringId value
};

std::map<u64, Context>   g_contexts;    // every context that has bindings
std::vector<u64>         g_stack;       // active, bottom first
f32                      g_deadZone = 0.18f;

Context& GetOrCreateContext(StringId id, std::string_view name) {
    Context& context = g_contexts[id.Value()];
    if (context.name.empty()) {
        context.name.assign(name);
    }
    return context;
}

Binding ParseBinding(std::string_view text, std::string& outWarning) {
    Binding binding;

    const usize dot = text.find('.');
    if (dot == std::string_view::npos) {
        outWarning = "binding '" + std::string(text) +
                     "' has no device prefix (expected Key./Mouse./Gamepad.)";
        return binding;
    }

    const std::string_view device = text.substr(0, dot);
    const std::string      name(text.substr(dot + 1));

    if (device == "Key") {
        const i32 code = EventPump::KeyCodeFromName(name.c_str());
        if (code < 0) {
            outWarning = "unknown key name '" + name + "'";
            return binding;
        }
        binding.device = Device::Key;
        binding.code   = code;
        return binding;
    }

    if (device == "Mouse") {
        const i32 code = EventPump::MouseButtonFromName(name.c_str());
        if (code < 0) {
            outWarning = "unknown mouse button '" + name + "'";
            return binding;
        }
        binding.device = Device::MouseButton;
        binding.code   = code;
        return binding;
    }

    if (device == "Gamepad") {
        // RECOGNISED AND IGNORED, deliberately and quietly. The example config
        // ships gamepad bindings; a gamepad is Week 8 stretch goal 2 and is not
        // wired up. Treating these as errors would fill the log with warnings
        // about a file that is correct, and silently failing to parse them
        // would be worse. This branch is the "if the layer is built correctly
        // it is close to free" hook: adding gamepads means filling it in and
        // touching nothing above.
        return binding;
    }

    outWarning = "unknown device '" + std::string(device) + "' in binding";
    return binding;
}

// Resolution: top of the stack downward, FIRST context that binds this key
// wins. See the header - this function is the recorded decision, executable.
ActionEntry* FindOwningAction(Device device, i32 code) {
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); ++it) {
        auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end()) {
            continue;
        }
        for (auto& [id, action] : contextIt->second.actions) {
            for (const Binding& binding : action.bindings) {
                if (binding.device == device && binding.code == code) {
                    return &action;
                }
            }
        }
    }
    return nullptr;
}

ActionEntry* FindAction(StringId action) {
    for (auto it = g_stack.rbegin(); it != g_stack.rend(); ++it) {
        auto contextIt = g_contexts.find(*it);
        if (contextIt == g_contexts.end()) {
            continue;
        }
        auto actionIt = contextIt->second.actions.find(action.Value());
        if (actionIt != contextIt->second.actions.end()) {
            return &actionIt->second;
        }
    }
    return nullptr;
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

void InputMap::PushContext(StringId context) {
    g_stack.push_back(context.Value());
    ENGINE_LOG_DEBUG(Channels::kInput, "input context pushed: '{}' (depth {})",
                     context.ToString(), g_stack.size());
}

void InputMap::PopContext() {
    if (g_stack.empty()) {
        ENGINE_LOG_WARN(Channels::kInput, "PopContext with an empty context stack");
        return;
    }
    g_stack.pop_back();
}

void InputMap::ClearContexts() {
    g_stack.clear();
}

StringId InputMap::ActiveContext() {
    return g_stack.empty() ? StringId{} : StringId::FromValue(g_stack.back());
}

usize InputMap::ContextDepth() {
    return g_stack.size();
}

std::vector<std::string> InputMap::ContextNames() {
    std::vector<std::string> names;
    names.reserve(g_stack.size());
    for (u64 id : g_stack) {
        const auto it = g_contexts.find(id);
        names.push_back(it != g_contexts.end() ? it->second.name : "<unnamed>");
    }
    return names;
}

bool InputMap::IsPressed(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Pressed;
}

bool InputMap::IsHeld(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Held;
}

bool InputMap::IsReleased(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr && entry->state == ActionState::Released;
}

bool InputMap::IsDown(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return entry != nullptr &&
           (entry->state == ActionState::Pressed || entry->state == ActionState::Held);
}

ActionState InputMap::GetState(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return (entry != nullptr) ? entry->state : ActionState::Idle;
}

f32 InputMap::GetAxis(StringId action) {
    const ActionEntry* entry = FindAction(action);
    return (entry != nullptr) ? entry->axis : 0.0f;
}

Vec2 InputMap::GetAxis2D(StringId negX, StringId posX, StringId negY, StringId posY) {
    Vec2 raw{GetAxis(posX) - GetAxis(negX), GetAxis(posY) - GetAxis(negY)};

    // THE RADIAL DEAD ZONE, applied to the PAIR rather than to each axis. See
    // the header for why a per-axis version makes diagonals feel wrong.
    //
    // The rescale matters as much as the threshold: without it, the first
    // responsive value jumps straight from 0 to the dead zone size, so the
    // stick feels like it has a step in it just off centre.
    const f32 length = raw.Length();
    if (length <= g_deadZone) {
        return Vec2{0.0f, 0.0f};
    }
    const f32 rescaled = std::min((length - g_deadZone) / (1.0f - g_deadZone), 1.0f);
    return raw * (rescaled / length);
}

void InputMap::SetDeadZone(f32 deadZone) {
    g_deadZone = std::clamp(deadZone, 0.0f, 0.95f);
}

f32 InputMap::DeadZone() {
    return g_deadZone;
}

void InputMap::Update(const EventPump& pump) {
    // Roll this frame's "down now" into last frame's, then apply events. The
    // pressed/held/released distinction is derived from the pair, which is why
    // gameplay can ask "did this go down THIS frame" without the OS key-repeat
    // rate - a user preference that differs per machine - getting involved.
    for (auto& [contextId, context] : g_contexts) {
        for (auto& [actionId, action] : context.actions) {
            action.downLast = action.downNow;
        }
    }

    for (usize i = 0; i < pump.Count(); ++i) {
        // Events the editor GUI swallowed never reach gameplay. This is the
        // Week 2 capture-flag work paying off: type in a CVar field and the
        // player does not move.
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

    for (auto& [contextId, context] : g_contexts) {
        for (auto& [actionId, action] : context.actions) {
            if (action.downNow && !action.downLast) {
                action.state = ActionState::Pressed;
            } else if (action.downNow) {
                action.state = ActionState::Held;
            } else if (action.downLast) {
                action.state = ActionState::Released;
            } else {
                action.state = ActionState::Idle;
            }
            // A key feeding an axis reports exactly 0 or 1. A real analog
            // source would write action.axis directly, before the dead zone in
            // GetAxis2D is applied.
            action.axis = action.downNow ? 1.0f : 0.0f;
        }
    }
}

void InputMap::Bind(StringId context, StringId action, std::string_view binding) {
    std::string warning;
    const Binding parsed = ParseBinding(binding, warning);
    if (!warning.empty()) {
        ENGINE_LOG_WARN(Channels::kInput, "{}", warning);
        return;
    }
    if (parsed.device == Device::None) {
        return;   // recognised but unsupported (gamepad) - already explained
    }

    Context& target = GetOrCreateContext(context, context.ToString());
    ActionEntry& entry = target.actions[action.Value()];
    if (entry.name.empty()) {
        entry.name = action.ToString();
    }
    entry.bindings.push_back(parsed);
}

void InputMap::LoadBindings(const ConfigNode& inputNode, std::string& outWarnings) {
    if (!inputNode.IsValid()) {
        ENGINE_LOG_WARN(Channels::kInput, "config has no input section; no bindings loaded");
        return;
    }

    SetDeadZone(static_cast<f32>(inputNode.Child("deadZone").AsFloat(g_deadZone)));

    const ConfigNode contexts = inputNode.Child("contexts");
    if (!contexts.IsValid()) {
        outWarnings += "config input section has no contexts\n";
        return;
    }

    for (const std::string& contextName : contexts.Keys()) {
        const StringId   contextId = Intern(contextName);
        const ConfigNode actions   = contexts.Child(contextName);
        Context&         context   = GetOrCreateContext(contextId, contextName);

        for (const std::string& actionName : actions.Keys()) {
            const StringId   actionId  = Intern(actionName);
            const ConfigNode bindings  = actions.Child(actionName);
            ActionEntry&     entry     = context.actions[actionId.Value()];
            entry.name                 = actionName;

            if (!bindings.IsArray()) {
                outWarnings += "input." + contextName + "." + actionName +
                               " should be an array of binding strings\n";
                continue;
            }

            for (usize i = 0; i < bindings.Size(); ++i) {
                const std::string text = bindings.At(i).AsString("");
                std::string       warning;
                const Binding     parsed = ParseBinding(text, warning);
                if (!warning.empty()) {
                    // Named with the context and the action, so the message
                    // says which line of the file to go and look at.
                    const std::string full = contextName + "." + actionName + ": " + warning;
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

void InputMap::InjectAction(StringId action, bool down) {
    // Resolved through the SAME context stack a real key would go through, so
    // an injected action obeys context shadowing exactly like a physical one.
    // Injecting "MoveLeft" while a menu context is on top does nothing, which
    // is correct - a replay must not be able to drive the player through a
    // pause screen.
    if (ActionEntry* entry = FindAction(action); entry != nullptr) {
        entry->downNow = down;
    }
}

void InputMap::ClearInjectedActions() {
    // Releases everything. An autopilot that stops steering must not leave the
    // player walking into a wall forever.
    for (auto& [contextId, context] : g_contexts) {
        for (auto& [actionId, entry] : context.actions) {
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
    for (const auto& [contextId, context] : g_contexts) {
        for (const auto& [actionId, action] : context.actions) {
            if (action.bindings.empty()) {
                out.push_back({context.name, action.name, "<unbound>"});
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
                out.push_back({context.name, action.name, text});
            }
        }
    }
}

} // namespace eng
