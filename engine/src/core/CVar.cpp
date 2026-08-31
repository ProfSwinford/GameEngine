// WEEK 8 - the CVar registry. See CVar.h.

#include <engine/core/CVar.h>
#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace eng {
namespace {

// Ordered by name so the editor's table and "dump current settings" both list
// in a stable, alphabetical order. With a few dozen variables the lookup cost
// difference against a hash map is not measurable, and a table whose rows move
// between frames is unusable.
struct Registry {
    std::mutex                                   mutex;
    std::map<std::string, std::unique_ptr<CVar>> byName;
    std::map<u64, CVar*>                         byId;
};

Registry& GetRegistry() {
    static Registry registry;   // constructed on first use - see StringId.cpp
    return registry;
}

} // namespace

// A private static member of CVarRegistry rather than a free helper, because
// only CVarRegistry is a friend of CVar - see the note in CVar.h.
CVar* CVarRegistry::RegisterCommon(std::string_view name, std::string_view description,
                                   CVarType type) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);

    const std::string key(name);
    if (const auto it = registry.byName.find(key); it != registry.byName.end()) {
        // Registering twice returns the EXISTING variable. A subsystem
        // re-initialised (the editor reloading a scene) must not reset a value
        // the user has spent five minutes tuning.
        return it->second.get();
    }

    auto variable            = std::make_unique<CVar>();
    variable->m_name         = key;
    variable->m_description  = std::string(description);
    variable->m_id           = Intern(name);
    variable->m_type         = type;

    CVar* raw = variable.get();
    registry.byId[raw->m_id.Value()] = raw;
    registry.byName.emplace(key, std::move(variable));
    return raw;
}

const char* ToString(CVarType type) {
    switch (type) {
        case CVarType::Bool:   return "bool";
        case CVarType::Int:    return "int";
        case CVarType::Float:  return "float";
        case CVarType::String: return "string";
    }
    return "?";
}

void CVar::SetBool(bool value) {
    m_boolValue = value;
    m_modified  = true;
}
void CVar::SetInt(i32 value) {
    m_intValue = value;
    m_modified = true;
}
void CVar::SetFloat(f32 value) {
    m_floatValue = value;
    m_modified   = true;
}
void CVar::SetString(std::string_view value) {
    m_stringValue.assign(value);
    m_modified = true;
}

bool CVar::SetFromString(std::string_view text) {
    switch (m_type) {
        case CVarType::Bool: {
            if (text == "true" || text == "1")  { SetBool(true);  return true; }
            if (text == "false" || text == "0") { SetBool(false); return true; }
            return false;
        }
        case CVarType::Int: {
            i32 value = 0;
            const auto result =
                std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{}) {
                return false;   // unchanged - a typo must not zero a tunable
            }
            SetInt(value);
            return true;
        }
        case CVarType::Float: {
            // std::strtof rather than std::stof: stof reports failure by
            // THROWING, and the engine's error policy (Ch. 3.2, Assert.h) is
            // that a malformed config value is an environment failure handled
            // by a return code. std::from_chars for floats would be nicer
            // still but libc++ has not shipped it.
            const std::string owned(text);
            char*             end   = nullptr;
            const float       value = std::strtof(owned.c_str(), &end);
            if (end == owned.c_str()) {
                return false;   // unchanged - a typo must not zero a tunable
            }
            SetFloat(value);
            return true;
        }
        case CVarType::String:
            SetString(text);
            return true;
    }
    return false;
}

std::string CVar::ValueAsString() const {
    switch (m_type) {
        case CVarType::Bool:   return m_boolValue ? "true" : "false";
        case CVarType::Int:    return std::to_string(m_intValue);
        case CVarType::Float:  return std::to_string(m_floatValue);
        case CVarType::String: return m_stringValue;
    }
    return {};
}

CVar* CVarRegistry::RegisterBool(std::string_view name, bool defaultValue,
                                 std::string_view description) {
    CVar* variable = RegisterCommon(name, description, CVarType::Bool);
    if (!variable->m_modified) {
        variable->m_boolValue = defaultValue;
    }
    return variable;
}

CVar* CVarRegistry::RegisterInt(std::string_view name, i32 defaultValue,
                                std::string_view description) {
    CVar* variable = RegisterCommon(name, description, CVarType::Int);
    if (!variable->m_modified) {
        variable->m_intValue = defaultValue;
    }
    return variable;
}

CVar* CVarRegistry::RegisterFloat(std::string_view name, f32 defaultValue,
                                  std::string_view description) {
    CVar* variable = RegisterCommon(name, description, CVarType::Float);
    if (!variable->m_modified) {
        variable->m_floatValue = defaultValue;
    }
    return variable;
}

CVar* CVarRegistry::RegisterString(std::string_view name, std::string_view defaultValue,
                                   std::string_view description) {
    CVar* variable = RegisterCommon(name, description, CVarType::String);
    if (!variable->m_modified) {
        variable->m_stringValue.assign(defaultValue);
    }
    return variable;
}

CVar* CVarRegistry::Find(StringId id) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    const auto it = registry.byId.find(id.Value());
    return (it != registry.byId.end()) ? it->second : nullptr;
}

CVar* CVarRegistry::Find(std::string_view name) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    const auto it = registry.byName.find(std::string(name));
    return (it != registry.byName.end()) ? it->second.get() : nullptr;
}

bool CVarRegistry::GetBool(std::string_view name, bool fallback) {
    CVar* variable = Find(name);
    if (variable == nullptr) {
        ENGINE_LOG_WARN(Channels::kConfig, "CVar '{}' is not registered", name);
        return fallback;
    }
    return variable->GetBool();
}

i32 CVarRegistry::GetInt(std::string_view name, i32 fallback) {
    CVar* variable = Find(name);
    if (variable == nullptr) {
        ENGINE_LOG_WARN(Channels::kConfig, "CVar '{}' is not registered", name);
        return fallback;
    }
    return variable->GetInt();
}

f32 CVarRegistry::GetFloat(std::string_view name, f32 fallback) {
    CVar* variable = Find(name);
    if (variable == nullptr) {
        ENGINE_LOG_WARN(Channels::kConfig, "CVar '{}' is not registered", name);
        return fallback;
    }
    return variable->GetFloat();
}

void CVarRegistry::ForEach(const std::function<void(CVar&)>& fn) {
    // Snapshot the pointers under the lock, call outside it. The callback is
    // ImGui panel code and may register CVars of its own, which would deadlock
    // on a non-recursive mutex this thread already holds.
    std::vector<CVar*> snapshot;
    {
        Registry& registry = GetRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        snapshot.reserve(registry.byName.size());
        for (auto& [name, variable] : registry.byName) {
            snapshot.push_back(variable.get());
        }
    }
    for (CVar* variable : snapshot) {
        fn(*variable);
    }
}

usize CVarRegistry::Count() {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.byName.size();
}

void CVarRegistry::ApplyFromConfig(const ConfigNode& cvarsNode, std::string& outWarnings) {
    if (!cvarsNode.IsValid()) {
        return;
    }

    for (const std::string& key : cvarsNode.Keys()) {
        if (key.starts_with('_')) {
            continue;
        }
        CVar* variable = Find(key);
        if (variable == nullptr) {
            // WARNED, not silently ignored. See the note in CVar.h: a typo in
            // a config file that is silently dropped is invisible forever.
            const std::string warning =
                "config names CVar '" + key + "', which is not registered";
            ENGINE_LOG_WARN(Channels::kConfig, "{}", warning);
            outWarnings += warning + "\n";
            continue;
        }

        // A wrong-typed value is warned about by ConfigNode's readers, which
        // name the key and both types, and the CVar keeps its current value.
        const ConfigNode value = cvarsNode.Child(key);
        switch (variable->Type()) {
            case CVarType::Bool:   variable->SetBool(value.AsBool(variable->GetBool())); break;
            case CVarType::Int:    variable->SetInt(static_cast<i32>(value.AsInt(variable->GetInt()))); break;
            case CVarType::Float:  variable->SetFloat(static_cast<f32>(value.AsFloat(variable->GetFloat()))); break;
            case CVarType::String: variable->SetString(value.AsString(variable->GetString())); break;
        }
    }
}

bool CVarRegistry::SaveToConfig(std::string_view virtualPath, std::string& outError) {
    // Read the file as it is on disk, replace only the cvars section's
    // modified entries, write it back. Everything else in the file - the
    // comments, the input bindings, keys this build does not know about -
    // survives.
    nlohmann::json root = nlohmann::json::object();

    std::vector<u8> bytes;
    std::string     readError;
    if (FileSystem::ReadFile(virtualPath, bytes, readError)) {
        auto parsed = nlohmann::json::parse(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            nullptr, false, true);
        if (!parsed.is_discarded() && parsed.is_object()) {
            root = std::move(parsed);
        }
    }

    ForEach([&root](CVar& variable) {
        if (!variable.IsModified()) {
            return;   // do not bloat the file with forty defaults
        }
        switch (variable.Type()) {
            case CVarType::Bool:   root["cvars"][variable.Name()] = variable.GetBool(); break;
            case CVarType::Int:    root["cvars"][variable.Name()] = variable.GetInt(); break;
            case CVarType::Float:  root["cvars"][variable.Name()] = variable.GetFloat(); break;
            case CVarType::String: root["cvars"][variable.Name()] = variable.GetString(); break;
        }
    });

    const std::string text = root.dump(2);
    if (!FileSystem::WriteFile(virtualPath, text.data(), text.size(), outError)) {
        return false;
    }
    ENGINE_LOG_INFO(Channels::kConfig, "saved CVars to '{}'", virtualPath);
    return true;
}

void CVarRegistry::Clear() {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.byId.clear();
    registry.byName.clear();
}

} // namespace eng
