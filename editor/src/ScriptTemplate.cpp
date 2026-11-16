// ============================================================================
//  ScriptTemplate.cpp - the text of a new script. See ScriptTemplate.h.
// ============================================================================

#include "ScriptTemplate.h"

#include <engine/tools/ScriptVars.h>   // ScriptNameShadowsFieldType

#include <cctype>
#include <format>
#include <set>

namespace editor {
namespace {

// The C++ keywords somebody might plausibly type as a script name. Not a
// complete list - one that slips through produces a compile error in a file
// you can see and rename, which is recoverable. The point is to catch the
// likely ones at the moment of typing.
const std::set<std::string>& ReservedNames() {
    static const std::set<std::string> reserved = {
        "class",  "struct", "union",  "enum",   "namespace", "template", "typename",
        "public", "private", "protected", "virtual", "static", "const",  "constexpr",
        "int",    "float",  "double", "char",   "bool",      "void",     "auto",
        "if",     "else",   "for",    "while",  "do",        "switch",   "case",
        "return", "new",    "delete", "this",   "operator",  "friend",   "using",
    };
    return reserved;
}

} // namespace

bool IsValidScriptName(std::string_view name, std::string& outError) {
    if (name.empty()) {
        outError = "a script needs a name";
        return false;
    }
    if (name.size() > 64) {
        outError = "that name is longer than 64 characters";
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        outError = "a script name cannot start with a digit, because it becomes a C++ "
                   "class name";
        return false;
    }
    for (const char c : name) {
        const bool ok = (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
        if (!ok) {
            outError = std::format("'{}' is not allowed in a script name - letters, "
                                   "digits and underscores only, because the name "
                                   "becomes a C++ class name", c);
            return false;
        }
    }
    if (ReservedNames().contains(std::string(name))) {
        outError = std::format("'{}' is a C++ keyword and cannot be a class name", name);
        return false;
    }

    // ======================================================================
    //  A NAME THAT WOULD SHADOW A TYPE OTHER SCRIPTS DECLARE VARIABLES OF.
    //
    //  REFUSED HERE BECAUSE THIS IS THE LAST MOMENT IT IS FREE. A script
    //  called `Color` compiles, loads and runs - what breaks is every OTHER
    //  script in the project, quietly: `Color tint;` in one of them stops
    //  meaning a colour and starts meaning a reference to this script, the
    //  Inspector draws an entity slot where a colour picker was, and nothing
    //  reports it. Renaming afterwards means renaming the class, the file and
    //  whatever a scene file already saved under the old name.
    //
    //  THE ENGINE ANSWERS, not a list kept here. See
    //  ScriptNameShadowsFieldType in tools/ScriptVars.h, which reads the same
    //  table the script parser matches declarations against - so a type
    //  spelling added to the engine is covered by this refusal without
    //  anybody remembering to come back here.
    // ======================================================================
    if (const std::string_view shadowed = eng::ScriptNameShadowsFieldType(name);
        !shadowed.empty()) {
        outError = std::format(
            "'{}' is the name of a type scripts declare variables of, so a script "
            "called that would quietly change what '{} something;' means in every "
            "other script - from a value into a reference to this one. Pick another "
            "name.",
            name, shadowed);
        return false;
    }

    outError.clear();
    return true;
}

std::string DefaultScriptText(std::string_view scriptName) {
    // std::format is used with a positional argument, {0}, so the script's
    // name can appear as many times as the template needs from one value.
    //
    // The braces of the C++ being GENERATED have to be doubled - {{ and }} -
    // because a single brace is how std::format marks a place to substitute.
    // That is the one genuinely awkward part of generating code this way.
    //
    // R"(...)" is a raw string: everything between the quotes is taken
    // literally, including newlines and backslashes, which is what makes it
    // possible to write a whole file inside one string.
    return std::format(R"(//  {0}

#include <engine/Engine.h>
using namespace eng;

class {0} final : public MyScriptBehaviour {{
public:
    // Happens once when the script is initialized
    void OnStart() {{
        
    }}

    // Happens once per frame
    void OnUpdate() {{
        
    }}

private:

}};

ENGINE_REGISTER_SCRIPT({0})
)",
                       scriptName);
}

} // namespace editor
