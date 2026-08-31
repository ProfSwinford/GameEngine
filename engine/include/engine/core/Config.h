#pragma once

// =============================================================================
//  WEEK 8 - configuration from a file. Ch. 6.5.
//
//  *** THE NOTE THAT MATTERED: this is the SAME loading path Week 9's scene
//  loader uses. *** It was built as a general document reader with a schema
//  laid on top, not as a "read the window size" function, and Week 9 therefore
//  only had to solve a schema. Week 8 evidence question 6 asks whether you
//  would call your own config loader again for a different schema next week;
//  the answer here is yes, and Scene::Load is the proof - it uses ConfigNode
//  and never mentions a parser.
//
//  NO JSON TYPE APPEARS IN THIS HEADER. nlohmann::json is linked PRIVATE to
//  the engine and hidden behind a pimpl. Swapping the parser means editing
//  Config.cpp and nothing else - which is the actual test of whether a
//  dependency is an implementation detail.
//
//  ---------------------------------------------------------------------------
//  ERROR HANDLING, decided and documented. Config files are edited by humans,
//  so they are wrong often. This is the ENVIRONMENT-FAILURE category from
//  Ch. 3.2, so: error returns, not asserts.
//
//    FILE MISSING       -> WARN, boot with defaults. Refusing to boot because
//                          a tuning file is absent would make a fresh clone
//                          unrunnable, and every value has a sane default.
//    MALFORMED SYNTAX   -> ERROR naming the BYTE OFFSET and the parser's
//                          message, then boot with defaults. Reported, never
//                          silently ignored: a config that is being ignored
//                          and a config that is being obeyed look identical
//                          from the outside.
//    WRONG TYPE         -> WARN naming the key and both types, use the
//                          default for that one key, keep everything else.
//                          "width": "big" should not cost you your key
//                          bindings.
//    UNKNOWN KEY        -> WARN naming the key. Could be a typo or a setting
//                          from a newer build; either way the author wants to
//                          know, and silently dropping it means a typo in a
//                          config file is invisible forever.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/core/Types.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class ConfigDocument;

// A non-owning view of one node in a loaded document. Copyable, cheap, and
// valid only while its document is alive.
class ConfigNode {
public:
    ConfigNode() = default;

    bool IsValid() const  { return m_node != nullptr; }
    bool IsObject() const;
    bool IsArray() const;
    bool IsNumber() const;
    bool IsString() const;
    bool IsBool() const;

    // Child by key. An absent key returns an invalid node rather than
    // throwing, so `node.Child("window").Child("width").AsInt(1280)` reads
    // naturally and cannot crash on a partial file.
    ConfigNode Child(std::string_view key) const;

    usize      Size() const;          // array or object element count
    ConfigNode At(usize index) const; // array element

    std::vector<std::string> Keys() const;

    // Typed reads. Each takes the value to use when the node is absent, and
    // WARNS (naming the path and both types) when the node is present with the
    // wrong type.
    i64         AsInt(i64 fallback) const;
    f64         AsFloat(f64 fallback) const;
    bool        AsBool(bool fallback) const;
    std::string AsString(std::string_view fallback) const;

    // Convenience for the very common "array of two numbers" shape used for
    // positions and scales in scene files.
    bool AsFloatArray(f32* out, usize count) const;

    // The dotted path of this node from the document root, for diagnostics.
    // This is what makes an error message say `entities[7].components[1].
    // texture` rather than "parse error", and the Week 9 instructions are
    // explicit that a good message here is worth an hour a week.
    const std::string& Path() const { return m_path; }

    // The underlying node, as an opaque pointer. The mirror of
    // ConfigWriter::NativeHandle, and it exists for exactly one reason: Scene's
    // save has to PRESERVE the keys it does not itself write, which means
    // reading the loaded document as a whole rather than key by key. Anything
    // that is not the serialiser should use the typed reads above - the return
    // type is deliberately untyped so that no public header has to name the
    // JSON library, which is the same boundary ConfigWriter draws.
    const void* NativeHandle() const { return m_node; }

private:
    friend class ConfigDocument;
    friend class ConfigWriter;
    ConfigNode(const void* node, std::string path) : m_node(node), m_path(std::move(path)) {}

    const void* m_node = nullptr;   // an nlohmann::json*, opaque here
    std::string m_path;
};

// The WRITE counterpart to ConfigNode, and the thing Week 9 left out.
//
// `Deserialize` existed on every component from Week 9; `Serialize` did not, so
// the editor could edit a scene and had no way to keep the result. Every
// Inspector edit was lost on reload, which made the whole panel a viewer with
// drag handles.
//
// Deliberately FLAT: a component's fields are key/value pairs, and none of them
// nests. That keeps this to six setters instead of a begin/end object-and-array
// builder, which is the kind of API that is easy to write and easy to get
// wrong. Scene::Save composes the flat bags into the document.
class ConfigWriter {
public:
    ConfigWriter();
    ~ConfigWriter();

    ConfigWriter(ConfigWriter&&) noexcept;
    ConfigWriter& operator=(ConfigWriter&&) noexcept;

    ConfigWriter(const ConfigWriter&)            = delete;
    ConfigWriter& operator=(const ConfigWriter&) = delete;

    void SetBool(std::string_view key, bool value);
    void SetInt(std::string_view key, i64 value);
    void SetFloat(std::string_view key, f64 value);
    void SetString(std::string_view key, std::string_view value);
    void SetFloatArray(std::string_view key, const f32* values, usize count);
    void SetStringArray(std::string_view key, const std::vector<std::string>& values);

    bool        IsEmpty() const;
    std::string ToJson(int indent = 2) const;

    // Reads back what was just written, as the same node type Deserialize
    // takes. That makes `Serialize -> Deserialize` a usable pair in memory,
    // which is what lets Scene::DuplicateEntity be a genuine deep copy of every
    // component instead of a hand-written member-by-member copy that has to be
    // extended every time a component type is added - and forgotten once.
    //
    // The node borrows this writer's storage, so it is valid only while the
    // writer is alive.
    ConfigNode AsNode(std::string_view pathForDiagnostics = "<in-memory>") const;

    // Engine-internal, for the same reason and with the same warning as
    // Window::NativeWindowHandle: Scene::Save needs to embed one of these in a
    // larger document, and the alternative is serialising to text and parsing
    // it straight back. void* rather than a named type keeps the parser out of
    // this header.
    void*       NativeHandle();
    const void* NativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class ConfigDocument {
public:
    ConfigDocument();
    ~ConfigDocument();

    ConfigDocument(ConfigDocument&&) noexcept;
    ConfigDocument& operator=(ConfigDocument&&) noexcept;

    ConfigDocument(const ConfigDocument&)            = delete;
    ConfigDocument& operator=(const ConfigDocument&) = delete;

    // Reads through FileSystem, so the path is VIRTUAL ("config/engine.json")
    // and resolves the same way on every machine. Returns false with a
    // readable diagnostic in outError.
    bool LoadFromVirtualPath(std::string_view virtualPath, std::string& outError);
    bool LoadFromText(std::string_view text, std::string& outError);

    bool IsLoaded() const;
    ConfigNode Root() const;

    // Writes a value back and saves. Used by the CVar panel's Save button.
    bool SetAndSave(std::string_view virtualPath,
                    const std::vector<std::pair<std::string, std::string>>& dottedValues,
                    std::string& outError);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---------------------------------------------------------------------------
//  The boot schema. Window size and title plus behavioural tunables; the Week 1
//  hardcoded resolution finally died here.
// ---------------------------------------------------------------------------
struct BootConfig {
    // window
    i32         windowWidth  = 1280;
    i32         windowHeight = 720;
    std::string windowTitle  = "Engine2D";

    // logging
    LogLevel    logThreshold = LogLevel::Info;
    std::string logFile      = "logs/engine.log";

    // tunables - the "at least three behavioural" ones the milestone requires
    i32   workerThreadCount    = 0;          // 0 = one per hardware thread - 1
    i32   debugCircleSegments  = 24;
    usize frameAllocatorBytes  = 1u << 20;   // 1 MiB
    usize entityPoolBlocks     = 4096;
    f32   inputDeadZone        = 0.18f;
    f32   fixedTimestepSeconds = 1.0f / 60.0f;
    i32   maxStepsPerFrame     = 5;
    usize logBufferCapacity    = 4096;

    // The scene the sandbox loads at boot, so that even the STARTING WORLD is
    // data. Week 9's milestone is zero hardcoded content, and a hardcoded
    // scene filename would be content.
    std::string startupScene = "scenes/orbit_test.json";
};

// Loads the boot section. Never returns "false" for a missing file - see the
// error table above - but always fills outError with what happened so the
// caller can log it. Returns false only when the file EXISTS and could not be
// parsed at all, which is the case where booting with defaults would silently
// ignore an author's intent.
bool LoadBootConfig(std::string_view virtualPath, BootConfig& outConfig,
                    std::string& outError);

// Applies the [cvars] and [input] sections. Separate from LoadBootConfig
// because those two are consumed by different subsystems at different points
// in the boot sequence.
bool ApplyConfigToRegistries(const ConfigDocument& document, std::string& outWarnings);

} // namespace eng
