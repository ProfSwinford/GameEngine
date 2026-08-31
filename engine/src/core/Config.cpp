// WEEK 8 - configuration. See Config.h for the error-handling table.
//
// This is the ONLY file besides CVar.cpp and Scene.cpp that includes a JSON
// header, and nlohmann_json is linked PRIVATE. Nothing in engine/include
// mentions it.

#include <engine/core/CVar.h>
#include <engine/core/Config.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputMap.h>

#include <nlohmann/json.hpp>

#include <string>

namespace eng {
namespace {

using Json = nlohmann::json;

const Json* AsJson(const void* node) {
    return static_cast<const Json*>(node);
}

// Every known key in the boot schema, so that an unknown one can be warned
// about by name. Listed rather than derived because the point is to catch a
// TYPO, and a scheme that accepts anything cannot.
constexpr const char* kKnownTopLevel[] = {"window", "logging", "tunables",
                                          "cvars",  "input",   "startup"};

void WarnUnknownKeys(const ConfigNode& root) {
    for (const std::string& key : root.Keys()) {
        if (key.starts_with('_')) {
            continue;   // "_comment" and friends are documentation, by convention
        }
        bool known = false;
        for (const char* candidate : kKnownTopLevel) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            // UNKNOWN KEY: warned about, by name, and ignored. Could be a typo
            // or a setting from a newer build; either way the author wants to
            // know, because a silently dropped key is invisible forever.
            ENGINE_LOG_WARN(Channels::kConfig,
                            "unknown top-level config key '{}' - a typo, or a setting "
                            "from a newer build?", key);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
//  ConfigNode
// ---------------------------------------------------------------------------

bool ConfigNode::IsObject() const { return m_node != nullptr && AsJson(m_node)->is_object(); }
bool ConfigNode::IsArray() const  { return m_node != nullptr && AsJson(m_node)->is_array(); }
bool ConfigNode::IsNumber() const { return m_node != nullptr && AsJson(m_node)->is_number(); }
bool ConfigNode::IsString() const { return m_node != nullptr && AsJson(m_node)->is_string(); }
bool ConfigNode::IsBool() const   { return m_node != nullptr && AsJson(m_node)->is_boolean(); }

ConfigNode ConfigNode::Child(std::string_view key) const {
    if (m_node == nullptr) {
        return ConfigNode{};
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_object()) {
        return ConfigNode{};
    }
    const auto it = json.find(std::string(key));
    if (it == json.end()) {
        return ConfigNode{};   // absent, not an error - see the header
    }
    return ConfigNode(&(*it), m_path.empty() ? std::string(key)
                                             : m_path + "." + std::string(key));
}

usize ConfigNode::Size() const {
    return (m_node != nullptr) ? AsJson(m_node)->size() : 0;
}

ConfigNode ConfigNode::At(usize index) const {
    if (m_node == nullptr) {
        return ConfigNode{};
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_array() || index >= json.size()) {
        return ConfigNode{};
    }
    return ConfigNode(&json[index], m_path + "[" + std::to_string(index) + "]");
}

std::vector<std::string> ConfigNode::Keys() const {
    std::vector<std::string> keys;
    if (m_node == nullptr) {
        return keys;
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_object()) {
        return keys;
    }
    for (const auto& [key, value] : json.items()) {
        keys.push_back(key);
    }
    return keys;
}

i64 ConfigNode::AsInt(i64 fallback) const {
    if (m_node == nullptr) {
        return fallback;
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_number_integer()) {
        // WRONG TYPE: warn naming the key and both types, use the default for
        // THIS key only. "width": "big" must not cost you your key bindings.
        ENGINE_LOG_WARN(Channels::kConfig, "config '{}' should be an integer, found {}",
                        m_path, json.type_name());
        return fallback;
    }
    return json.get<i64>();
}

f64 ConfigNode::AsFloat(f64 fallback) const {
    if (m_node == nullptr) {
        return fallback;
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_number()) {
        ENGINE_LOG_WARN(Channels::kConfig, "config '{}' should be a number, found {}",
                        m_path, json.type_name());
        return fallback;
    }
    return json.get<f64>();
}

bool ConfigNode::AsBool(bool fallback) const {
    if (m_node == nullptr) {
        return fallback;
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_boolean()) {
        ENGINE_LOG_WARN(Channels::kConfig, "config '{}' should be a boolean, found {}",
                        m_path, json.type_name());
        return fallback;
    }
    return json.get<bool>();
}

std::string ConfigNode::AsString(std::string_view fallback) const {
    if (m_node == nullptr) {
        return std::string(fallback);
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_string()) {
        ENGINE_LOG_WARN(Channels::kConfig, "config '{}' should be a string, found {}",
                        m_path, json.type_name());
        return std::string(fallback);
    }
    return json.get<std::string>();
}

bool ConfigNode::AsFloatArray(f32* out, usize count) const {
    if (m_node == nullptr || out == nullptr) {
        return false;
    }
    const Json& json = *AsJson(m_node);
    if (!json.is_array() || json.size() < count) {
        ENGINE_LOG_WARN(Channels::kConfig, "config '{}' should be an array of {} numbers",
                        m_path, count);
        return false;
    }
    for (usize i = 0; i < count; ++i) {
        if (!json[i].is_number()) {
            ENGINE_LOG_WARN(Channels::kConfig, "config '{}[{}]' is not a number", m_path, i);
            return false;
        }
        out[i] = json[i].get<f32>();
    }
    return true;
}

// ---------------------------------------------------------------------------
//  ConfigWriter
// ---------------------------------------------------------------------------

struct ConfigWriter::Impl {
    Json root = Json::object();
};

ConfigWriter::ConfigWriter() : m_impl(std::make_unique<Impl>()) {}
ConfigWriter::~ConfigWriter() = default;
ConfigWriter::ConfigWriter(ConfigWriter&&) noexcept = default;
ConfigWriter& ConfigWriter::operator=(ConfigWriter&&) noexcept = default;

void ConfigWriter::SetBool(std::string_view key, bool value) {
    m_impl->root[std::string(key)] = value;
}

void ConfigWriter::SetInt(std::string_view key, i64 value) {
    m_impl->root[std::string(key)] = value;
}

void ConfigWriter::SetFloat(std::string_view key, f64 value) {
    m_impl->root[std::string(key)] = value;
}

void ConfigWriter::SetString(std::string_view key, std::string_view value) {
    m_impl->root[std::string(key)] = std::string(value);
}

void ConfigWriter::SetFloatArray(std::string_view key, const f32* values, usize count) {
    if (values == nullptr) {
        return;
    }
    Json array = Json::array();
    for (usize i = 0; i < count; ++i) {
        // Written as a double that round-trips: nlohmann emits the shortest
        // representation that reads back identically, so 0.6f does not become
        // 0.60000002384185791 in the file. A scene file a human has to edit is
        // worth this much care.
        array.push_back(static_cast<f64>(values[i]));
    }
    m_impl->root[std::string(key)] = std::move(array);
}

void ConfigWriter::SetStringArray(std::string_view key,
                                  const std::vector<std::string>& values) {
    Json array = Json::array();
    for (const std::string& value : values) {
        array.push_back(value);
    }
    m_impl->root[std::string(key)] = std::move(array);
}

bool ConfigWriter::IsEmpty() const {
    return m_impl->root.empty();
}

std::string ConfigWriter::ToJson(int indent) const {
    return m_impl->root.dump(indent);
}

ConfigNode ConfigWriter::AsNode(std::string_view pathForDiagnostics) const {
    return ConfigNode(&m_impl->root, std::string(pathForDiagnostics));
}

void*       ConfigWriter::NativeHandle()       { return &m_impl->root; }
const void* ConfigWriter::NativeHandle() const { return &m_impl->root; }

// ---------------------------------------------------------------------------
//  ConfigDocument
// ---------------------------------------------------------------------------

struct ConfigDocument::Impl {
    Json root;
    bool loaded = false;
};

ConfigDocument::ConfigDocument() : m_impl(std::make_unique<Impl>()) {}
ConfigDocument::~ConfigDocument() = default;
ConfigDocument::ConfigDocument(ConfigDocument&&) noexcept = default;
ConfigDocument& ConfigDocument::operator=(ConfigDocument&&) noexcept = default;

bool ConfigDocument::LoadFromVirtualPath(std::string_view virtualPath,
                                         std::string& outError) {
    std::vector<u8> bytes;
    if (!FileSystem::ReadFile(virtualPath, bytes, outError)) {
        return false;
    }
    return LoadFromText(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                         bytes.size()),
                        outError);
}

bool ConfigDocument::LoadFromText(std::string_view text, std::string& outError) {
    // MALFORMED SYNTAX: parse without throwing, then report WHERE. The byte
    // offset is the closest thing nlohmann gives to a line number, and it is
    // enough to find the problem in an editor that shows offsets.
    m_impl->root   = Json::parse(text, nullptr, /*allow_exceptions=*/false,
                                 /*ignore_comments=*/true);
    m_impl->loaded = !m_impl->root.is_discarded();

    if (!m_impl->loaded) {
        // Re-parse with exceptions on, purely to get the diagnostic text out.
        // Parsing twice only happens on the failure path, which is not hot.
        try {
            (void)Json::parse(text, nullptr, true, true);
        } catch (const Json::parse_error& error) {
            outError = std::string("JSON parse error at byte ") +
                       std::to_string(error.byte) + ": " + error.what();
            return false;
        }
        outError = "JSON parse failed";
        return false;
    }

    outError.clear();
    return true;
}

bool ConfigDocument::IsLoaded() const {
    return m_impl->loaded;
}

ConfigNode ConfigDocument::Root() const {
    if (!m_impl->loaded) {
        return ConfigNode{};
    }
    return ConfigNode(&m_impl->root, std::string{});
}

bool ConfigDocument::SetAndSave(
    std::string_view virtualPath,
    const std::vector<std::pair<std::string, std::string>>& dottedValues,
    std::string& outError) {
    // Load the file fresh rather than writing this in-memory document out.
    // Somebody may have edited it since boot, and clobbering their edits
    // because the CVar panel's Save button was pressed would be rude.
    ConfigDocument onDisk;
    std::string    loadError;
    if (!onDisk.LoadFromVirtualPath(virtualPath, loadError)) {
        // A missing file is fine here: start from an empty object and write a
        // new one.
        onDisk.m_impl->root   = Json::object();
        onDisk.m_impl->loaded = true;
    }

    for (const auto& [dotted, value] : dottedValues) {
        // "cvars.debug.drawColliders" -> root["cvars"]["debug.drawColliders"],
        // because CVar names themselves contain dots and splitting on every
        // one would bury each variable four objects deep. Only the FIRST
        // segment is a section.
        const usize firstDot = dotted.find('.');
        if (firstDot == std::string::npos) {
            onDisk.m_impl->root[dotted] = Json::parse(value, nullptr, false);
            continue;
        }
        const std::string section = dotted.substr(0, firstDot);
        const std::string key     = dotted.substr(firstDot + 1);

        Json parsed = Json::parse(value, nullptr, false);
        if (parsed.is_discarded()) {
            parsed = value;   // not valid JSON on its own: store it as a string
        }
        onDisk.m_impl->root[section][key] = parsed;
    }

    const std::string text = onDisk.m_impl->root.dump(2);
    return FileSystem::WriteFile(virtualPath, text.data(), text.size(), outError);
}

// ---------------------------------------------------------------------------
//  The boot schema
// ---------------------------------------------------------------------------

bool LoadBootConfig(std::string_view virtualPath, BootConfig& outConfig,
                    std::string& outError) {
    ConfigDocument document;

    if (!FileSystem::Exists(virtualPath)) {
        // FILE MISSING: warn, boot with defaults. Refusing to boot because a
        // tuning file is absent would make a fresh clone unrunnable.
        outError = "config file '" + std::string(virtualPath) +
                   "' not found; booting with built-in defaults";
        ENGINE_LOG_WARN(Channels::kConfig, "{}", outError);
        return true;
    }

    if (!document.LoadFromVirtualPath(virtualPath, outError)) {
        // The file EXISTS and could not be parsed. This is the one case that
        // returns false: booting with defaults here would silently ignore an
        // author's intent, and a config that is being ignored looks exactly
        // like a config that is being obeyed.
        ENGINE_LOG_ERROR(Channels::kConfig, "{}", outError);
        return false;
    }

    const ConfigNode root = document.Root();
    WarnUnknownKeys(root);

    const ConfigNode window = root.Child("window");
    outConfig.windowWidth  = static_cast<i32>(window.Child("width").AsInt(outConfig.windowWidth));
    outConfig.windowHeight = static_cast<i32>(window.Child("height").AsInt(outConfig.windowHeight));
    outConfig.windowTitle  = window.Child("title").AsString(outConfig.windowTitle);

    const ConfigNode logging = root.Child("logging");
    outConfig.logFile = logging.Child("file").AsString(outConfig.logFile);
    if (const ConfigNode threshold = logging.Child("threshold"); threshold.IsValid()) {
        const std::string text = threshold.AsString("Info");
        if (!ParseLogLevel(text, outConfig.logThreshold)) {
            ENGINE_LOG_WARN(Channels::kConfig,
                            "unrecognised log threshold '{}'; keeping {}", text,
                            ToString(outConfig.logThreshold));
        }
    }

    const ConfigNode tunables = root.Child("tunables");
    outConfig.workerThreadCount =
        static_cast<i32>(tunables.Child("workerThreadCount").AsInt(outConfig.workerThreadCount));
    outConfig.debugCircleSegments =
        static_cast<i32>(tunables.Child("debugCircleSegments").AsInt(outConfig.debugCircleSegments));
    outConfig.frameAllocatorBytes = static_cast<usize>(
        tunables.Child("frameAllocatorBytes").AsInt(static_cast<i64>(outConfig.frameAllocatorBytes)));
    outConfig.entityPoolBlocks = static_cast<usize>(
        tunables.Child("entityPoolBlocks").AsInt(static_cast<i64>(outConfig.entityPoolBlocks)));
    outConfig.logBufferCapacity = static_cast<usize>(
        tunables.Child("logBufferCapacity").AsInt(static_cast<i64>(outConfig.logBufferCapacity)));
    outConfig.fixedTimestepSeconds = static_cast<f32>(
        tunables.Child("fixedTimestepSeconds").AsFloat(outConfig.fixedTimestepSeconds));
    outConfig.maxStepsPerFrame =
        static_cast<i32>(tunables.Child("maxStepsPerFrame").AsInt(outConfig.maxStepsPerFrame));

    const ConfigNode input = root.Child("input");
    outConfig.inputDeadZone =
        static_cast<f32>(input.Child("deadZone").AsFloat(outConfig.inputDeadZone));

    const ConfigNode startup = root.Child("startup");
    outConfig.startupScene = startup.Child("scene").AsString(outConfig.startupScene);

    outError.clear();
    return true;
}

bool ApplyConfigToRegistries(const ConfigDocument& document, std::string& outWarnings) {
    if (!document.IsLoaded()) {
        return false;
    }
    const ConfigNode root = document.Root();

    CVarRegistry::ApplyFromConfig(root.Child("cvars"), outWarnings);
    InputMap::LoadBindings(root.Child("input"), outWarnings);
    return true;
}

} // namespace eng
