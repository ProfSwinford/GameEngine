// ============================================================================
//  ScriptTemplate.cpp - the text of a new script. See ScriptTemplate.h.
// ============================================================================

#include "ScriptTemplate.h"

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
    return std::format(R"(// =============================================================================
//  {0} - a script.
//
//  Attach it by dragging this file from the Assets panel onto an entity in the
//  Hierarchy or the Inspector.
//
//  -----------------------------------------------------------------------------
//  THIS IS COMPILED C++, NOT AN INTERPRETED SCRIPT.
//
//  Saving this file changes nothing in a running editor. Build the project and
//  start the editor again, and it connects itself - the scene already refers to
//  "{0}" by name, so nothing needs reattaching.
//
//  Until then the Inspector shows this script as NOT FOUND, which is the editor
//  telling you the truth rather than pretending.
//
//  -----------------------------------------------------------------------------
//  THE LIFECYCLE. Every one of these is optional; delete the ones you do not
//  need.
//
//    OnStart()            Once, on the first simulation step after this script
//                         is attached and its entity is fully built. NOT at
//                         attach time: while a scene loads, components are
//                         attached one at a time, so another component you look
//                         for at attach time may not exist yet.
//
//    OnUpdate(dt)         Every FIXED simulation step. NOT once per drawn frame -
//                         this engine simulates at a steady rate and draws
//                         separately, so this runs a whole number of times per
//                         frame, sometimes twice and sometimes not at all. That
//                         is what makes the game behave the same on every
//                         machine, and it is why you multiply by dt instead of
//                         assuming a frame rate.
//
//    OnDestroy()          The entity is going away. It is still safe to touch
//                         here and not afterwards.
//
//    OnCollisionEnter     ENTER fires once when an overlap begins, STAY every
//    OnCollisionStay      step it continues, and EXIT once when it ends -
//    OnCollisionExit      including when the other entity is destroyed while
//                         still overlapping.
//
//                         `other` is an EntityId, not a pointer, and the thing
//                         it refers to may already be gone. Look it up through
//                         the scene every time; never keep a pointer to it.
//
//                         These need a collider on BOTH entities, and each one's
//                         "collides with" list has to include the other's layer.
//
//  -----------------------------------------------------------------------------
//  WHAT YOU CAN REACH from inside any of them:
//
//    Owner()       Entity*        this script's entity
//    Transform()   Transform2D*   its position, rotation and scale
//    GetScene()    Scene*         to find or create other entities
//    OwnerId()     EntityId       this entity's id, for sending messages
//
//  All four work from OnStart onwards. Any of them can return null if the
//  entity has been destroyed, so check before using one.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

namespace {{

class {0} final : public eng::ScriptBehaviour {{
public:
    void OnStart() override {{
        ENGINE_LOG_INFO(eng::Channels::kGame, "{0} started on '{{}}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }}

    void OnUpdate(float deltaSeconds) override {{
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {{
            return;
        }}

        // Replace this with whatever your script should do. It is here so that
        // a brand new script does something visible the first time you press
        // Play - a template that compiles and then appears to do nothing is
        // indistinguishable from one that failed to attach.
        m_secondsAlive += deltaSeconds;
    }}

    void OnDestroy() override {{
        ENGINE_LOG_INFO(eng::Channels::kGame, "{0} lived {{:.2f}} seconds",
                        m_secondsAlive);
    }}

    void OnCollisionEnter(eng::EntityId other) override {{
        // The other entity is looked up fresh rather than remembered, because
        // it may already have been destroyed this step.
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {{
            return;
        }}
        const eng::Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(eng::Channels::kGame, "{0} touched '{{}}'",
                        partner != nullptr ? partner->Name() : "<already gone>");
    }}

private:
    float m_secondsAlive = 0.0f;
}};

}} // namespace

// Registers the name "{0}" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT({0})
)",
                       scriptName);
}

} // namespace editor
