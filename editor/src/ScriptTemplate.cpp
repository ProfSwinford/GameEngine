// The default script text. See ScriptTemplate.h.

#include "ScriptTemplate.h"

#include <cctype>
#include <format>
#include <unordered_set>

namespace editor {
namespace {

// Not exhaustive - just the keywords someone might plausibly type as a script
// name. A name that slips through produces a compile error in a file the user
// can see and rename, which is a recoverable outcome; the point of the list is
// to catch the likely ones at the moment of typing.
const std::unordered_set<std::string>& ReservedNames() {
    static const std::unordered_set<std::string> reserved = {
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
        outError = "a script name cannot start with a digit - it becomes a C++ class name";
        return false;
    }
    for (const char c : name) {
        const bool ok = (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
        if (!ok) {
            outError = std::format("'{}' is not allowed in a script name - letters, digits "
                                   "and underscore only, because the name becomes a C++ "
                                   "class name",
                                   c);
            return false;
        }
    }
    if (ReservedNames().contains(std::string(name))) {
        outError = std::format("'{}' is a C++ keyword", name);
        return false;
    }
    outError.clear();
    return true;
}

std::string DefaultScriptText(std::string_view scriptName) {
    // std::format with {0} positional arguments, so the name can appear as
    // many times as the template needs from one argument. The literal braces
    // of the C++ being generated are escaped as {{ and }}, which is the one
    // genuinely awkward part of generating code with format - and the reason
    // the body below is a single raw string rather than assembled in pieces.
    return std::format(R"(// =============================================================================
//  {0} - a script.
//
//  Attach it by dragging this file from the Asset Browser onto an entity in
//  the Hierarchy or the Inspector. The entity gets a ScriptComponent bound to
//  the name "{0}".
//
//  ---------------------------------------------------------------------------
//  *** THIS IS COMPILED C++, NOT AN INTERPRETED SCRIPT. ***
//
//  Saving this file changes nothing in a running editor. Build the project and
//  relaunch, and the binding resolves by itself - the scene already refers to
//  "{0}" by name, so nothing needs reattaching.
//
//  Until then the Inspector shows this script as UNRESOLVED, which is the
//  editor telling you the truth rather than pretending.
//
//  ---------------------------------------------------------------------------
//  THE LIFECYCLE. Every hook is optional; delete the ones you do not need.
//
//    OnStart()            Once, on the first simulation step after this script
//                         is attached and its entity is fully built. NOT at
//                         attach time - during a scene load, components are
//                         attached one at a time, so a sibling component you
//                         look for at attach time may not exist yet. This is
//                         the first moment the entity is whole. Cache things
//                         here.
//
//    OnUpdate(dt)         Every FIXED simulation step, with the fixed delta.
//                         NOT once per rendered frame: this engine simulates on
//                         a fixed timestep and renders separately, so this runs
//                         a whole number of times per frame - sometimes twice,
//                         sometimes zero. That is what makes the simulation
//                         reproducible, and it is why you multiply by dt rather
//                         than assuming a frame rate.
//
//                         Runs at stage 200 (gameplay), BEFORE movement at 300
//                         and collision at 400 - so a position you set here is
//                         the position collision tests this step, not next.
//
//    OnDestroy()          The entity is going away - destroyed, or unloaded
//                         with the scene. The entity is still valid here and is
//                         not afterwards, so this is the place to release
//                         anything you took.
//
//    OnCollisionEnter/    Forwarded from the message bus. ENTER fires once when
//    OnCollisionStay/     an overlap begins, STAY every step it continues, EXIT
//    OnCollisionExit      once when it ends - including when the other entity is
//                         destroyed while overlapping.
//
//                         `other` is an EntityHandle, NOT a pointer, and it may
//                         already be dead by the time you look at it. Resolve
//                         it through the scene every time; never cache it as a
//                         pointer. That rule is not specific to scripts - it is
//                         how the whole engine works.
//
//                         These need a collider on BOTH entities, and each
//                         one's mask must include the other's layer.
//
//  ---------------------------------------------------------------------------
//  WHAT YOU CAN REACH from inside any hook:
//
//    Owner()        Entity*       this script's entity
//    Transform()    Transform2D*  its transform - the usual one
//    GetScene()     Scene*        to find or spawn other entities
//    OwnerHandle()  EntityHandle  this entity's handle, for messages
//
//  All four are valid from OnStart onwards. Any of them can return null if the
//  entity has been destroyed, so check before dereferencing in a hook that can
//  run late.
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

    void OnUpdate(eng::f32 deltaSeconds) override {{
        eng::Transform2D* transform = Transform();
        if (transform == nullptr) {{
            return;
        }}

        // Delete this. It is here so that a brand new script does something
        // visible the first time you press Play - a template that compiles and
        // then appears to do nothing is indistinguishable from one that failed
        // to attach.
        m_secondsAlive += deltaSeconds;
        (void)transform;
    }}

    void OnDestroy() override {{
        ENGINE_LOG_INFO(eng::Channels::kGame, "{0} lived {{:.2f}}s", m_secondsAlive);
    }}

    void OnCollisionEnter(eng::EntityHandle other) override {{
        // The partner is resolved through the scene rather than cached,
        // because it may already have been destroyed this step.
        eng::Scene* scene = GetScene();
        if (scene == nullptr) {{
            return;
        }}
        const eng::Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(eng::Channels::kGame, "{0} touched '{{}}'",
                        partner != nullptr ? partner->Name() : "<destroyed>");
    }}

private:
    eng::f32 m_secondsAlive = 0.0f;
}};

}} // namespace

// Registers the name "{0}" so a scene file and the editor can bind to it.
// WITHOUT THIS LINE the file compiles and the script can never be found.
ENGINE_REGISTER_SCRIPT({0})
)",
                       scriptName);
}

} // namespace editor
