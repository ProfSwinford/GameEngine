#pragma once

// =============================================================================
//  WEEK 8 - CVars. Ch. 6.5.
//
//  A CVar is a named variable a subsystem registers so that the rest of the
//  engine - and a config file, a debug key, or an editor panel - can read and
//  change it without a rebuild.
//
//  The argument, from Ch. 6.5: a hardcoded constant is a design smell, because
//  someone eventually needs to change it and "rebuild the engine" is a bad
//  answer when that someone is a designer, or is you at 2am trying twelve
//  values in a row. The smell is not aesthetic - it is ninety seconds,
//  multiplied by thirty.
//
//  ---------------------------------------------------------------------------
//  THE SUPPORTED TYPES ARE DELIBERATELY FOUR: bool, i32, f32, string.
//
//  Every type added costs you in every switch statement that handles a CVar -
//  the editor panel, the config applier, the serialiser, the console. Four
//  carries the whole of Phase 1 and Phase 2. Resist the fifth.
//
//  ---------------------------------------------------------------------------
//  THE REGISTRY MUST BE ENUMERABLE, and that is an engine requirement a TOOL
//  exposed. A registry that only answers "what is the value of x" cannot be
//  browsed, so the CVar panel could not exist - it does not know what to ask
//  for. ForEach() is in this header because of that, and it is a good example
//  of a tool requirement improving an engine API.
//
//  Week 10's verification requires toggling a collision layer mask at runtime
//  through a CVar, which is why SetInt on a live variable takes effect
//  immediately with no notification step.
// =============================================================================

#include <engine/core/StringId.h>

#include <functional>
#include <string>
#include <string_view>

namespace eng {

enum class CVarType : u8 { Bool, Int, Float, String };

const char* ToString(CVarType type);

class CVar {
public:
    const std::string& Name() const        { return m_name; }
    const std::string& Description() const { return m_description; }
    StringId           Id() const          { return m_id; }
    CVarType           Type() const        { return m_type; }

    bool               GetBool() const   { return m_boolValue; }
    i32                GetInt() const    { return m_intValue; }
    f32                GetFloat() const  { return m_floatValue; }
    const std::string& GetString() const { return m_stringValue; }

    void SetBool(bool value);
    void SetInt(i32 value);
    void SetFloat(f32 value);
    void SetString(std::string_view value);

    // Parses a value out of text, for the config file and an eventual console.
    // Returns false without changing anything if the text does not fit the
    // type - a typo in a config file must not silently zero a tunable.
    bool SetFromString(std::string_view text);
    std::string ValueAsString() const;

    // True once anything has written to it since registration. The panel marks
    // these, and Save writes only them - so saving does not bloat the config
    // file with forty defaults.
    bool IsModified() const { return m_modified; }

private:
    friend class CVarRegistry;

    std::string m_name;
    std::string m_description;
    StringId    m_id;
    CVarType    m_type = CVarType::Bool;

    bool        m_boolValue  = false;
    i32         m_intValue   = 0;
    f32         m_floatValue = 0.0f;
    std::string m_stringValue;
    bool        m_modified   = false;
};

class CVarRegistry {
public:
    // Registering the same name twice returns the EXISTING variable rather
    // than replacing it, so a subsystem that is initialised twice (the editor
    // reloading a scene) does not reset a value the user has been tuning.
    static CVar* RegisterBool(std::string_view name, bool defaultValue,
                              std::string_view description);
    static CVar* RegisterInt(std::string_view name, i32 defaultValue,
                             std::string_view description);
    static CVar* RegisterFloat(std::string_view name, f32 defaultValue,
                               std::string_view description);
    static CVar* RegisterString(std::string_view name, std::string_view defaultValue,
                                std::string_view description);

    static CVar* Find(StringId id);
    static CVar* Find(std::string_view name);

    // Convenience readers that do not make every caller null-check. They log
    // once, at Warning, for a name that was never registered - which catches
    // the typo that would otherwise silently read a default forever.
    static bool GetBool(std::string_view name, bool fallback);
    static i32  GetInt(std::string_view name, i32 fallback);
    static f32  GetFloat(std::string_view name, f32 fallback);

    // ENUMERATION. See the header comment - the panel cannot exist without it,
    // and neither can "dump current settings", which is the first thing you
    // want when a bug report arrives with no configuration attached.
    static void ForEach(const std::function<void(CVar&)>& fn);
    static usize Count();

    // Applies values from the config file's cvars section.
    //
    // WHAT HAPPENS WHEN THE FILE NAMES A CVAR THAT DOES NOT EXIST: it is
    // WARNED about, by name, and skipped. Silently ignoring it means a typo in
    // a config file is invisible - the value appears to have been set, the
    // behaviour does not change, and there is nothing to find. It is not an
    // error, because a config file shared between two builds may legitimately
    // name a variable one of them has not got.
    static void ApplyFromConfig(const class ConfigNode& cvarsNode, std::string& outWarnings);

    // Writes every modified CVar back to the config file's cvars section,
    // preserving everything else in the file. The Save button in the panel.
    static bool SaveToConfig(std::string_view virtualPath, std::string& outError);

    static void Clear();   // tests only

private:
    // The shared half of the four Register* overloads. A private STATIC MEMBER
    // rather than a free helper in the .cpp, because only CVarRegistry is a
    // friend of CVar - a namespace-scope helper could not fill in a new
    // variable's name and type. Small, and it keeps the friendship to one
    // class instead of widening CVar's interface with setters that exist only
    // for registration.
    static CVar* RegisterCommon(std::string_view name, std::string_view description,
                                CVarType type);
};

} // namespace eng
