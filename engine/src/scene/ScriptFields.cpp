// ============================================================================
//  ScriptFields.cpp - the non-template half of script variables. See
//  ScriptFields.h for why the list is written by hand and why the scene file
//  stores only what somebody changed.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/ScriptFields.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eng {
namespace {

// Rounds to nearest and clamps into what an int can hold.
//
// The clamp is not theoretical. A scene file is text, and 1e30 is a perfectly
// well-formed number to find in one; casting that straight to int is undefined
// behaviour, which on this compiler produces a large negative number for no
// visible reason.
int ToInt(double number) {
    const double rounded = std::round(number);
    if (rounded <= static_cast<double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    if (rounded >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
}

// One colour channel from a JSON number: 0-255, anything outside pulled in.
unsigned char ToChannel(const Json& number) {
    if (!number.is_number()) {
        return 0;
    }
    const int value = ToInt(number.get<double>());
    return static_cast<unsigned char>(std::clamp(value, 0, 255));
}

} // namespace

const char* ScriptFieldTypeName(ScriptFieldType type) {
    switch (type) {
        case ScriptFieldType::Float:  return "float";
        case ScriptFieldType::Int:    return "int";
        case ScriptFieldType::Bool:   return "bool";
        case ScriptFieldType::String: return "text";
        case ScriptFieldType::Vec2:   return "position";
        case ScriptFieldType::Color:  return "colour";
        case ScriptFieldType::Entity: return "entity";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
//  ScriptField - typed access
//
//  The type is checked before the cast in every one of these. See the note in
//  the header: reading a std::string as a float does not fail, it corrupts.
// ---------------------------------------------------------------------------

float* ScriptField::AsFloat() const {
    return type == ScriptFieldType::Float ? static_cast<float*>(value) : nullptr;
}
int* ScriptField::AsInt() const {
    return type == ScriptFieldType::Int ? static_cast<int*>(value) : nullptr;
}
bool* ScriptField::AsBool() const {
    return type == ScriptFieldType::Bool ? static_cast<bool*>(value) : nullptr;
}
std::string* ScriptField::AsString() const {
    return type == ScriptFieldType::String ? static_cast<std::string*>(value) : nullptr;
}
Vec2* ScriptField::AsVec2() const {
    return type == ScriptFieldType::Vec2 ? static_cast<Vec2*>(value) : nullptr;
}
Color* ScriptField::AsColor() const {
    return type == ScriptFieldType::Color ? static_cast<Color*>(value) : nullptr;
}
EntityRef* ScriptField::AsEntityRef() const {
    return type == ScriptFieldType::Entity ? static_cast<EntityRef*>(value) : nullptr;
}

// ---------------------------------------------------------------------------
//  ScriptFields
// ---------------------------------------------------------------------------

void ScriptFields::Add(std::string_view name, ScriptFieldType type, void* value,
                       std::string_view tooltip, bool ranged, float min, float max) {
    if (value == nullptr) {
        return;
    }

    // An unnamed variable has nowhere to be saved - the name is the key in the
    // scene file - so it is refused rather than drawn as a blank row.
    if (name.empty()) {
        ENGINE_LOG_ERROR(Channels::kScene,
                         "a script exposed a variable with no name, which cannot be "
                         "shown or saved - every Var() needs one: "
                         "fields.Var(\"speed\", speed)");
        return;
    }

    // ======================================================================
    //  TWO VARIABLES WITH THE SAME NAME, which has to be refused.
    //
    //  The name is the key in the scene file, so a duplicate means two
    //  variables sharing one saved value: editing either would change both on
    //  the next load, and the Inspector would show two identical rows that
    //  disagree. Keeping the first and reporting the second is the only
    //  outcome that is stable, and silence here would cost an afternoon.
    // ======================================================================
    if (Find(name) != nullptr) {
        ENGINE_LOG_ERROR(Channels::kScene,
                         "a script exposed two variables both called '{}' - only the "
                         "first is used. Two variables cannot share one name, because "
                         "the name is how the value is saved.",
                         name);
        return;
    }

    ScriptField field;
    field.name    = std::string(name);
    field.tooltip = std::string(tooltip);
    field.type    = type;
    field.value   = value;
    field.ranged  = ranged;
    field.min     = min;
    field.max     = max;

    // A range with its ends the wrong way round, or both the same, would draw
    // a slider that cannot move. Reported and demoted to an ordinary drag box,
    // because the variable itself is fine and dropping it would be worse.
    if (ranged && !(min < max)) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "the script variable '{}' has a Range of {} to {}, which is "
                        "not a range - it is shown as an ordinary number instead",
                        name, min, max);
        field.ranged = false;
    }

    m_fields.push_back(std::move(field));
}

void ScriptFields::AddRef(std::string_view name, EntityRef* value,
                          std::string_view requiredType, std::string_view tooltip) {
    // ======================================================================
    //  A CONSTRAINT THAT NAMES NOTHING, reported rather than left to be
    //  discovered.
    //
    //  The mistake this catches is specific and easy to make:
    //
    //      fields.Ref("target", target, "what to chase");
    //
    //  which reads perfectly well and asks for entities carrying a component
    //  type called "what to chase". There are none, so the picker is empty
    //  and every reference shows as wrong - with nothing anywhere saying why.
    //
    //  A WARNING AND NOT A REFUSAL, because false here has an innocent cause:
    //  a script that is not compiled into this build is not registered, so a
    //  correct constraint naming it looks exactly like a typo. The reference
    //  is kept either way and the message names both possibilities.
    // ======================================================================
    if (!requiredType.empty() && !IsKnownComponentOrScriptName(requiredType)) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "the reference '{}' asks for '{}', which is not a component "
                        "type or a script in this build. Either that script is not "
                        "compiled in yet, or a tooltip has been passed where the "
                        "required type goes - fields.Ref takes the type THEN the "
                        "tooltip, and fields.Var takes just the tooltip.",
                        name, requiredType);
    }

    Add(name, ScriptFieldType::Entity, value, tooltip, false, 0.0f, 0.0f);

    // Written after the fact rather than threaded through Add's parameters.
    // Add already takes seven, and a constraint that only one of seven field
    // types can have does not belong in the signature every other type pays
    // for. It cannot miss: Add either appended or reported why it would not.
    if (!m_fields.empty() && m_fields.back().name == name) {
        m_fields.back().requiredType = std::string(requiredType);
    }
}

const ScriptField* ScriptFields::Find(std::string_view name) const {
    const auto it = std::find_if(m_fields.begin(), m_fields.end(),
                                 [name](const ScriptField& field) {
                                     return field.name == name;
                                 });
    return it != m_fields.end() ? &*it : nullptr;
}

// ---------------------------------------------------------------------------
//  JSON
// ---------------------------------------------------------------------------

bool JsonFitsFieldType(ScriptFieldType type, const Json& value) {
    switch (type) {
        case ScriptFieldType::Float:
        case ScriptFieldType::Int:
            // is_number() is true for both 3 and 3.0, deliberately. See the
            // note on ReadFieldFromJson in the header.
            return value.is_number();

        case ScriptFieldType::Bool:
            // STRICTLY a true or false, with 0 and 1 refused.
            //
            // The other types are read liberally because a person hand-editing
            // a file writes `120` for a float and means it. `"jumping": 1`
            // is different: it is as likely to be a leftover from a count as
            // an intended true, and guessing would hide the mistake.
            return value.is_boolean();

        case ScriptFieldType::String:
        case ScriptFieldType::Entity:
            // An entity reference IS a string in the file - the target's name.
            // Nothing here checks that anything answers to it: a scene saved
            // half-built refers to entities that do not exist yet, and
            // refusing to load the value would throw away the only record of
            // what it was meant to point at.
            return value.is_string();

        case ScriptFieldType::Vec2:
            return value.is_array() && value.size() == 2 && value[0].is_number() &&
                   value[1].is_number();

        case ScriptFieldType::Color:
            return value.is_array() && value.size() == 4 && value[0].is_number() &&
                   value[1].is_number() && value[2].is_number() && value[3].is_number();
    }
    return false;
}

bool ReadFieldFromJson(const ScriptField& field, const Json& value,
                       std::string& outError) {
    if (field.value == nullptr) {
        outError = "the variable is not bound to anything";
        return false;
    }

    // CHECKED BEFORE ANYTHING IS WRITTEN, so that a half-read value cannot be
    // left behind. A Vec2 whose second element is a string would otherwise
    // have its x already changed by the time the y was found to be wrong.
    if (!JsonFitsFieldType(field.type, value)) {
        outError = std::string("expected ") + ScriptFieldTypeName(field.type);
        switch (field.type) {
            case ScriptFieldType::Vec2:
                outError += " written as two numbers, like [12.5, -4]";
                break;
            case ScriptFieldType::Color:
                outError += " written as four numbers from 0 to 255, like "
                            "[255, 128, 0, 255]";
                break;
            case ScriptFieldType::Bool:
                outError += " written as true or false";
                break;
            case ScriptFieldType::Entity:
                outError += " written as the target entity's name, like \"Player\"";
                break;
            default:
                break;
        }
        outError += ", but found " + std::string(value.type_name());
        return false;
    }

    // ======================================================================
    //  THE try IS BELT AND BRACES, AND IT WAS EARNED.
    //
    //  Every get<> below is already protected by the check above, so none of
    //  them can throw - as long as the two functions agree about every type.
    //  A mutation test that made JsonFitsFieldType accept a number for a bool
    //  showed what happens when they stop agreeing: nlohmann throws
    //  type_error.302 straight out of the middle of loading a scene.
    //
    //  THAT IS THE WRONG FAILURE for a file a person edits by hand, and it is
    //  the failure the whole of Json.h exists to avoid - see the note at the
    //  top of that header. So this function cannot throw either, whatever the
    //  check above does or does not catch. The cost is one try block; the
    //  alternative is a level that refuses to open over a typo.
    // ======================================================================
    try {
        switch (field.type) {
            case ScriptFieldType::Float:
                *field.AsFloat() = value.get<float>();
                return true;

            case ScriptFieldType::Int:
                *field.AsInt() = ToInt(value.get<double>());
                return true;

            case ScriptFieldType::Bool:
                *field.AsBool() = value.get<bool>();
                return true;

            case ScriptFieldType::String:
                *field.AsString() = value.get<std::string>();
                return true;

            case ScriptFieldType::Entity:
                field.AsEntityRef()->Set(value.get<std::string>());
                return true;

            case ScriptFieldType::Vec2:
                *field.AsVec2() = Vec2{value[0].get<float>(), value[1].get<float>()};
                return true;

            case ScriptFieldType::Color:
                *field.AsColor() = Color{ToChannel(value[0]), ToChannel(value[1]),
                                         ToChannel(value[2]), ToChannel(value[3])};
                return true;
        }
    } catch (const Json::exception& problem) {
        outError = problem.what();
        return false;
    }

    outError = "unknown variable type";
    return false;
}

Json FieldToJson(const ScriptField& field) {
    if (field.value == nullptr) {
        return Json();
    }

    switch (field.type) {
        case ScriptFieldType::Float:
            return Json(*field.AsFloat());

        case ScriptFieldType::Int:
            return Json(*field.AsInt());

        case ScriptFieldType::Bool:
            return Json(*field.AsBool());

        case ScriptFieldType::String:
            return Json(*field.AsString());

        case ScriptFieldType::Entity:
            // THE NAME AS STORED, even when nothing currently answers to it.
            // Writing out "" for a target that has been deleted or renamed
            // would turn a reference somebody can see and fix into one that
            // was never set - and would do it the first time the scene was
            // saved, which is to say immediately.
            return Json(field.AsEntityRef()->TargetName());

        case ScriptFieldType::Vec2: {
            const Vec2 v = *field.AsVec2();
            return Json::array({v.x, v.y});
        }

        case ScriptFieldType::Color: {
            const Color c = *field.AsColor();
            // Cast to int so the file reads [255, 128, 0, 255] and not a list
            // of characters - an unsigned char handed to nlohmann is a number,
            // but writing the cast is what makes that obvious to the next
            // person to read this.
            return Json::array({static_cast<int>(c.r), static_cast<int>(c.g),
                                static_cast<int>(c.b), static_cast<int>(c.a)});
        }
    }
    return Json();
}

} // namespace eng
