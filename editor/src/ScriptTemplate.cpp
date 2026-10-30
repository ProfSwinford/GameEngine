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
//  THIS IS COMPILED C++, NOT AN INTERPRETED SCRIPT - but you never rebuild the
//  editor. Save this file, switch back to the editor window, and it compiles
//  and reloads by itself. There is a Build Scripts button in the Assets panel
//  for when you would rather not alt-tab.
//
//  If it does not compile, the errors appear in the Console with the file and
//  line, and the menu bar says so.
//
//  -----------------------------------------------------------------------------
//  DELETE THE HOOKS YOU DO NOT NEED. All of them are optional, and that is
//  literal: there is no `override` and nothing to declare. The engine works out
//  which of these functions you actually wrote, while your script is being
//  compiled, and only calls those. A script with no update is not asked to
//  update - it costs nothing at all per frame.
//
//  The catch is the same one every engine that works this way has: a MISSPELLED
//  hook is not an error, it is just a function nobody calls. If your script
//  seems to do nothing, check the Console - it lists every script it loaded
//  together with the hooks it found in it.
//
//  -----------------------------------------------------------------------------
//  THE LIFECYCLE.
//
//    OnStart()            Once, on the first simulation step after this script
//                         is attached and its entity is fully built. NOT at
//                         attach time: while a scene loads, components are
//                         attached one at a time, so another component you look
//                         for at attach time may not exist yet.
//
//    OnFixedUpdate()      Every FIXED simulation step. NOT once per drawn
//                         frame - this engine simulates at a steady rate and
//                         draws separately, so this runs a whole number of
//                         times per frame, sometimes twice and sometimes not
//                         at all. That is what makes the game behave the same
//                         on every machine, and it is where movement, physics
//                         and anything timed belongs.
//
//                         It takes no argument: ask Time::FixedDeltaTime().
//
//    OnUpdate()           Once per DRAWN frame, so however often that happens
//                         to be on this machine. For things tied to the
//                         screen rather than to the world - a fade, a menu, a
//                         camera that should move as smoothly as the display
//                         allows. NOT for movement or physics: it runs at a
//                         different rate on every machine, which is the whole
//                         thing the fixed step exists to avoid.
//
//                         Ask Time::DeltaTime() here, or
//                         Time::UnscaledDeltaTime() for something that must
//                         keep moving while the game is paused.
//
//    OnDestroy()          The entity is going away. It is still safe to touch
//                         here and not afterwards.
//
//    OnExpose(fields)     NOT AN EVENT, and USUALLY NOT NEEDED. Variables
//                         reach the Inspector on their own - see below. Write
//                         this only when the list is not a fixed set of
//                         declarations, and it then wins over the automatic
//                         one. See engine/scene/ScriptFields.h.
//
//  -----------------------------------------------------------------------------
//  VARIABLES IN THE INSPECTOR, which take no code at all.
//
//  A PUBLIC variable of a supported type is on screen, editable while the game
//  runs, and saved on each entity separately - so one script can drive two
//  entities that behave differently:
//
//      float speed = 200.0f;        // how fast it goes
//      float blend = 0.5f;          // [0..1] how much to mix
//      eng::EntityRef target;       // [Rigidbody2DComponent] what to push
//
//  The trailing comment is the tooltip. A [low..high] at its START makes a
//  slider; a [TypeName] narrows a reference to entities carrying that
//  component or script.
//
//  float, int, bool, std::string, Vec2, Color and eng::EntityRef. A `static
//  constexpr` constant cannot be changed, so it is left out - the Console says
//  which and why. To hide one, make it private or name it m_something.
//
//  HOW: the editor reads this file when it builds it, because C++ cannot ask a
//  class what members it has. See engine/tools/ScriptVars.h.
//
//    OnCollisionEnter     A SOLID contact: neither side is a trigger. ENTER
//    OnCollisionStay      fires once when the overlap begins, STAY every step
//    OnCollisionExit      it continues, and EXIT once when it ends - including
//                         when the other entity is destroyed while still
//                         overlapping.
//
//                         Takes EITHER the other entity or the whole contact:
//
//                             void OnCollisionEnter(eng::EntityId other)
//                             void OnCollisionEnter(const eng::Collision2D& hit)
//
//                         The second form carries hit.normal, hit.point and
//                         hit.relativeVelocity - which is how "I landed on
//                         something" is told from "I walked into a wall".
//
//    OnTriggerEnter       The same three events for a TRIGGER contact, where
//    OnTriggerStay        either side has "trigger" ticked. A trigger reports
//    OnTriggerExit        overlaps and nothing is pushed apart.
//
//                         A PAIR SENDS ONE SET OR THE OTHER, NEVER BOTH. A
//                         script with OnCollisionEnter on a trigger collider
//                         is never called - so the engine reports that in the
//                         Console rather than leaving it silent.
//
//                         `other` is an EntityId, not a pointer, and the thing
//                         it refers to may already be gone. Look it up through
//                         the scene every time; never keep a pointer to it.
//
//                         These need a collider on BOTH entities, and each
//                         one's "collides with" list has to include the
//                         other's layer.
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

#include <engine/Engine.h>
using namespace eng;

namespace {{

class {0} final : public MyScriptBehaviour {{
public:
    // ---- a number you can tune from the Inspector -----------------------
    //
    // IT IS ALREADY THERE. Public, and a type the Inspector can draw, is all
    // it takes - nothing registers it and nothing has to be kept in step.
    //
    // The trailing comment becomes its tooltip. Put [0..500] at the start of
    // that comment to make it a slider instead of a drag box.
    //
    // It is saved ON EACH ENTITY SEPARATELY, and only if somebody changed it:
    // leave it alone and it takes whatever this line says, so editing the
    // default still reaches every entity that never disagreed. The name here
    // is also the key in the scene file.
    //
    // Delete it if this script has nothing worth tuning. Make it private, or
    // rename it m_speed, to keep it out of the Inspector while keeping it.
    float speed = 100.0f;   // say here what this number is for

    void OnStart() {{
        ENGINE_LOG_INFO(Channels::kGame, "{0} started on '{{}}'",
                        Owner() != nullptr ? Owner()->Name() : "<none>");
    }}

    void OnFixedUpdate() {{
        Transform2D* transform = Transform();
        if (transform == nullptr) {{
            return;
        }}

        // Replace this with whatever your script should do. It is here so that
        // a brand new script does something visible the first time you press
        // Play - a template that compiles and then appears to do nothing is
        // indistinguishable from one that failed to attach.
        m_secondsAlive += Time::FixedDeltaTime();
    }}

    // ONCE PER DRAWN FRAME, which is a different rate on every machine.
    //
    // Most scripts do not need this one - delete it. It is for things tied to
    // the screen rather than to the world: a fade, a menu, a transition.
    // Movement and physics belong in OnFixedUpdate above, because anything
    // that moves from here would travel further on a fast machine.
    void OnUpdate() {{
        // Time::DeltaTime() is the frame time in here and the fixed step in
        // OnFixedUpdate, so the same line is correct in both. When the
        // difference matters, say which you mean: Time::FixedDeltaTime(), or
        // Time::UnscaledDeltaTime() for something that must keep moving while
        // the game is paused or in slow motion.
    }}

    void OnDestroy() {{
        ENGINE_LOG_INFO(Channels::kGame, "{0} lived {{:.2f}} seconds",
                        m_secondsAlive);
    }}

    void OnCollisionEnter(EntityId other) {{
        // The other entity is looked up fresh rather than remembered, because
        // it may already have been destroyed this step.
        Scene* scene = GetScene();
        if (scene == nullptr) {{
            return;
        }}
        const Entity* partner = scene->Get(other);
        ENGINE_LOG_INFO(Channels::kGame, "{0} touched '{{}}'",
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
