// ============================================================================
//  ScriptComponent.cpp - scripts. See ScriptComponent.h for why the connection
//  between a component and its behaviour is made by NAME, and why the
//  lifecycle hooks are found by the compiler rather than declared.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/physics/Collider.h>
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
#include <string_view>
#include <map>
#include <vector>

namespace eng {
namespace {

// Every script component currently attached, so the system can find them all
// without walking the whole scene.
std::vector<ScriptComponent*> g_scripts;

// The subsets that actually need walking, one per update rate.
//
// THESE LISTS ARE THE WHOLE POINT OF THE HOOK TABLE. A collision-only script
// sits in g_scripts so it can be counted, inspected and rebound, and is
// absent from both of these so it costs nothing at all sixty times a second.
//
// Two lists rather than one with a test inside the loop, because the common
// case is a script that has exactly one of the two updates: a movement script
// has OnFixedUpdate and should never be touched by the per-frame walk, and a
// HUD script has OnUpdate and should never be touched by the step walk.
std::vector<ScriptComponent*> g_fixedTicking;
std::vector<ScriptComponent*> g_frameTicking;

bool g_collisionsSubscribed = false;

// std::map rather than std::unordered_map so that listing the scripts comes
// out alphabetical without sorting - which is what the editor's "attach a
// script" list wants. There are tens of scripts, not thousands, so the speed
// difference does not matter.
using ScriptTable = std::map<std::string, ScriptRegistry::Entry>;

ScriptTable& Table() {
    // A variable inside a function, not a global.
    //
    // This is the detail that makes ENGINE_REGISTER_SCRIPT work. Those
    // registrar objects run before main(), in an order nobody controls, and a
    // plain global table might not exist yet when the first one tries to use
    // it. A variable inside a function is created the first time the function
    // is called, so whichever registrar runs first builds the table.
    static ScriptTable table;
    return table;
}

void AddTo(std::vector<ScriptComponent*>& list, ScriptComponent* script) {
    if (std::find(list.begin(), list.end(), script) == list.end()) {
        list.push_back(script);
    }
}

void AddToTicking(ScriptComponent* script) {
    if (script->NeedsFixedTick()) { AddTo(g_fixedTicking, script); }
    if (script->NeedsFrameTick()) { AddTo(g_frameTicking, script); }
}

} // namespace

// ---------------------------------------------------------------------------
//  ScriptBehaviour - the accessors a script uses
// ---------------------------------------------------------------------------

Entity* ScriptBehaviour::Owner() const {
    return m_component != nullptr ? m_component->Owner() : nullptr;
}

EntityId ScriptBehaviour::OwnerId() const {
    return m_component != nullptr ? m_component->OwnerId() : EntityId{};
}

Scene* ScriptBehaviour::GetScene() const {
    return m_component != nullptr ? m_component->GetScene() : nullptr;
}

Transform2D* ScriptBehaviour::Transform() const {
    return m_component != nullptr ? m_component->OwnerTransform() : nullptr;
}

// ---------------------------------------------------------------------------
//  Hooks
// ---------------------------------------------------------------------------

std::string DescribeHooks(const ScriptHooks& hooks) {
    std::string out;
    const auto  add = [&out](const char* name) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    };

    if (hooks.start != nullptr)          { add("OnStart"); }
    if (hooks.update != nullptr)         { add("OnUpdate"); }
    if (hooks.fixedUpdate != nullptr)    { add("OnFixedUpdate"); }
    if (hooks.destroy != nullptr)        { add("OnDestroy"); }
    if (hooks.collisionEnter != nullptr) { add("OnCollisionEnter"); }
    if (hooks.collisionStay != nullptr)  { add("OnCollisionStay"); }
    if (hooks.collisionExit != nullptr)  { add("OnCollisionExit"); }
    if (hooks.triggerEnter != nullptr)   { add("OnTriggerEnter"); }
    if (hooks.triggerStay != nullptr)    { add("OnTriggerStay"); }
    if (hooks.triggerExit != nullptr)    { add("OnTriggerExit"); }

    // LAST, and after the "no hooks" test below would have been decided -
    // except that it cannot change that test, because a script with only
    // OnExpose does not compile. See the catch-all static_assert in
    // ScriptHooks.h.
    if (hooks.expose != nullptr)         { add("OnExpose"); }

    // Worth naming rather than printing an empty string. A script with no
    // hooks at all compiles, registers, attaches and does nothing - and the
    // overwhelmingly likely reason is a misspelled function name.
    if (out.empty()) {
        out = "NO HOOKS - check the spelling of OnStart / OnUpdate / OnFixedUpdate";
    }
    return out;
}

// ---------------------------------------------------------------------------
//  ScriptRegistry
// ---------------------------------------------------------------------------

void ScriptRegistry::Register(std::string_view scriptName, CreateFn create,
                              const ScriptHooks& hooks, std::string_view sourceFile) {
    if (scriptName.empty() || create == nullptr) {
        return;
    }

    ScriptTable&      table = Table();
    const std::string name(scriptName);

    if (table.contains(name)) {
        const Entry& already = table.at(name);

        // The same script, seen twice. This is normal and harmless: a script
        // written in a .h and included by two .cpp files registers once per
        // file that included it. Same name, same file, same class - the second
        // one has nothing to add.
        if (already.sourceFile == sourceFile) {
            return;
        }

        // Two DIFFERENT files claiming the same name is a real problem: one of
        // them will never run, and which one is decided by something nobody
        // can see. The first is kept, so at least the choice is stable.
        ENGINE_LOG_WARN(Channels::kScene,
                        "two different files both define a script called '{}' ('{}' and "
                        "'{}') - only the first can run, so rename one of them",
                        scriptName, already.sourceFile, sourceFile);
        return;
    }

    Entry entry;
    entry.create     = create;
    entry.hooks      = hooks;
    entry.sourceFile = std::string(sourceFile);
    table[name]      = entry;
}

bool ScriptRegistry::IsRegistered(std::string_view scriptName) {
    return Table().contains(std::string(scriptName));
}

const ScriptRegistry::Entry* ScriptRegistry::Find(std::string_view scriptName) {
    const std::string name(scriptName);
    if (Table().contains(name)) {
        return &Table().at(name);
    }
    return nullptr;
}

std::unique_ptr<ScriptBehaviour> ScriptRegistry::Create(std::string_view scriptName) {
    const Entry* entry = Find(scriptName);
    return (entry != nullptr) ? entry->create() : nullptr;
}

void ScriptRegistry::ForEachScript(const std::function<void(const char*)>& fn) {
    for (const auto& [name, entry] : Table()) {
        fn(name.c_str());
    }
}

void ScriptRegistry::ForEachEntry(
    const std::function<void(const char* name, const Entry& entry)>& fn) {
    for (const auto& [name, entry] : Table()) {
        fn(name.c_str(), entry);
    }
}

std::size_t ScriptRegistry::Count() { return Table().size(); }

void ScriptRegistry::Clear() { Table().clear(); }

// ---------------------------------------------------------------------------
//  ScriptComponent
// ---------------------------------------------------------------------------

ScriptComponent::~ScriptComponent() {
    // A safety net for a component that was built but never attached, which
    // happens when a scene fails to load partway through.
    ScriptSystem::Unregister(*this);
    Unbind();
}

bool ScriptComponent::Deserialize(const Json& node, std::string& outError) {
    m_scriptName = ReadString(node, "script", "", kTypeName);
    if (m_scriptName.empty()) {
        outError = "ScriptComponent needs a \"script\" naming the behaviour to run";
        return false;
    }

    // ----------------------------------------------------------------------
    //  "vars" is taken as it stands and NOT checked against the script.
    //
    //  It cannot be: this runs before the component is attached, so nothing
    //  is bound yet and there is no variable list to check against. See
    //  Scene.cpp, where a component is deserialized and only then handed to
    //  AddComponent - which is what calls OnAttach and therefore Bind.
    //
    //  That order is what makes this work rather than merely survive. The
    //  values are already here by the time the script is created, so they are
    //  applied during Bind and are correct before OnStart has run - which is
    //  where a script reads its settings.
    // ----------------------------------------------------------------------
    m_savedValues = Json::object();
    if (HasKey(node, "vars")) {
        if (const Json& vars = node["vars"]; vars.is_object()) {
            m_savedValues = vars;
        } else {
            // NOT fatal. The script itself is fine, and refusing the whole
            // component over a malformed settings block would cost the entity
            // its behaviour as well as its values.
            ENGINE_LOG_ERROR(Channels::kScene,
                             "the \"vars\" of the script '{}' should be a set of named "
                             "values like {{ \"speed\": 120 }}, but it is {} - it has "
                             "been ignored, so the script runs with its own defaults",
                             m_scriptName, vars.type_name());
        }
    }

    // Applied here as well as in Bind, so that the order of the two stops
    // mattering. Deserialize comes first today; a component built in the
    // editor and then given values is the other way round, and neither should
    // be the one that works.
    ApplySavedValues();
    return true;
}

bool ScriptComponent::Serialize(Json& out) const {
    // The name is saved WHETHER OR NOT it was found in this build. An
    // unresolved script is simply one that has not been compiled yet, and
    // leaving it out of the save would silently delete somebody's work the
    // first time they saved a scene from a build without their script in it.
    out["script"] = m_scriptName;

    // ----------------------------------------------------------------------
    //  THE SAVED VALUES, NOT THE LIVE ONES.
    //
    //  This is the one place where the obvious implementation is wrong. Asking
    //  the running object for its variables would write down whatever the
    //  GAME has done to them - a health counting down, a timer part-way
    //  through - and a scene saved after a minute of play would come back with
    //  the player on three health.
    //
    //  So what is written is what somebody set in the Inspector, which is the
    //  only thing they asked to be saved. A variable the script changes by
    //  itself is run-time state and has no business in a level file.
    //
    //  The Inspector still SHOWS the live value, because watching a number
    //  move while the game runs is worth having. Showing one thing and saving
    //  another is a real asymmetry, and it is the right way round.
    // ----------------------------------------------------------------------
    if (m_savedValues.is_object() && !m_savedValues.empty()) {
        out["vars"] = m_savedValues;
    }
    return true;
}

void ScriptComponent::OnAttach() {
    Bind();
    ScriptSystem::Register(*this);
}

void ScriptComponent::OnDetach() {
    // OnDestroy runs BEFORE the entity is taken apart, so a behaviour can
    // still reach its transform and its neighbours. That is the whole reason
    // the hook exists rather than leaving clean-up to the destructor.
    if (m_behaviour != nullptr && m_started && m_hooks.destroy != nullptr) {
        m_hooks.destroy(m_behaviour.get());
    }
    ScriptSystem::Unregister(*this);
    Unbind();
}

void ScriptComponent::SetScriptName(std::string_view name) {
    if (m_scriptName == name) {
        return;
    }
    if (m_behaviour != nullptr && m_started && m_hooks.destroy != nullptr) {
        m_hooks.destroy(m_behaviour.get());
    }
    Unbind();
    m_scriptName = std::string(name);
    m_started    = false;

    // m_savedValues is NOT cleared, and that is a choice rather than an
    // oversight. Switching script to look at something and switching back
    // should not cost the settings, and two scripts that share a variable
    // name - "speed" on any of a dozen movement scripts - carry it over,
    // which is usually what was wanted.
    //
    // The price is that values belonging to the old script linger. They are
    // not invisible: UnusedSavedValues finds them and the Inspector offers to
    // drop them, which is a decision somebody can make and a silent delete is
    // not.
    Bind();

    // Re-registering is how the component gets back into the ticking list if
    // its new script has an OnUpdate and its old one did not. Register()
    // ignores a component it already knows about, so this is safe to call
    // whether or not the component is attached.
    ScriptSystem::Register(*this);
}

void ScriptComponent::UnbindForReload() {
    // The behaviour object is about to stop existing along with the library
    // that defined it, so it gets its OnDestroy exactly as it would if the
    // entity were being deleted.
    if (m_behaviour != nullptr && m_started && m_hooks.destroy != nullptr) {
        m_hooks.destroy(m_behaviour.get());
    }
    Unbind();

    // m_scriptName is deliberately KEPT. It is the only thing that survives a
    // reload, and it is what RebindAfterReload uses to find the new code.
    m_started = false;
}

void ScriptComponent::RebindAfterReload() {
    // Bind() reports an unknown name itself, which is what shows a script as
    // NOT FOUND in the Inspector after a build that failed to include it.
    Bind();
}

void ScriptComponent::Bind() {
    m_hooks = ScriptHooks{};

    if (const ScriptRegistry::Entry* entry = ScriptRegistry::Find(m_scriptName);
        entry != nullptr) {
        m_behaviour = entry->create();
        if (m_behaviour != nullptr) {
            // Set before any hook can run, so Owner() and Transform() already
            // work inside OnStart.
            m_behaviour->m_component = this;
            m_hooks                  = entry->hooks;

            // AFTER m_component is set, because OnExpose is a hook like any
            // other and a script is entitled to have its accessors working
            // inside it.
            BuildFields();
            return;
        }
    }

    if (!m_scriptName.empty()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "the script '{}' is not compiled into this build, so it is "
                        "attached but will not run ({} script(s) available)",
                        m_scriptName, ScriptRegistry::Count());
    }
}

void ScriptComponent::Unbind() {
    if (m_behaviour != nullptr) {
        m_behaviour->m_component = nullptr;
        m_behaviour.reset();
    }
    m_hooks = ScriptHooks{};

    // EVERY FIELD POINTED INTO THE OBJECT THAT HAS JUST GONE, so the list is
    // emptied here for exactly the reason m_hooks is: both are made of
    // addresses inside the compiled script library, and after a reload those
    // addresses are not merely wrong but unmapped.
    m_fields.Clear();
    m_defaults = Json::object();

    // m_savedValues is deliberately KEPT, alongside m_scriptName. It is the
    // data; the rest of this is machinery for running it.
}

// ---------------------------------------------------------------------------
//  Variables
// ---------------------------------------------------------------------------

void ScriptComponent::BuildFields() {
    m_fields.Clear();
    m_defaults = Json::object();

    if (m_behaviour == nullptr || m_hooks.expose == nullptr) {
        return;   // most scripts expose nothing, and that is free
    }

    m_hooks.expose(m_behaviour.get(), m_fields);

    // WHAT THE CODE SAID, read off a freshly built object before a single
    // saved value has touched it. This is the only moment those numbers can be
    // captured: nothing else in the program remembers that `radius` started
    // at 90, and after the first override is applied the object does not
    // either.
    for (const ScriptField& field : m_fields.All()) {
        m_defaults[field.name] = FieldToJson(field);
    }

    ApplySavedValues();
}

void ScriptComponent::ApplySavedValues() {
    if (m_behaviour == nullptr || !m_savedValues.is_object()) {
        return;
    }

    // WALKED BY FIELD, not by saved key. A key with no field is left where it
    // is rather than reported here - it may belong to a script this component
    // is about to be pointed back at, and UnusedSavedValues is where that is
    // surfaced, once, in the Inspector.
    for (const ScriptField& field : m_fields.All()) {
        if (!m_savedValues.contains(field.name)) {
            continue;   // never touched, so the script's own value stands
        }

        std::string error;
        if (ReadFieldFromJson(field, m_savedValues.at(field.name), error)) {
            continue;
        }

        // AT ERROR, and the level is the point. A saved value that will not
        // apply means the game runs with a different number than the scene
        // file says, which is a difference nothing else would ever mention -
        // and the project's log threshold discards warnings.
        const Entity* owner = Owner();
        ENGINE_LOG_ERROR(Channels::kScene,
                         "the saved value for '{}' on the script '{}' ({}) could not "
                         "be used: {}. The script's own default is being used instead.",
                         field.name, m_scriptName,
                         owner != nullptr ? owner->Name() : "<unattached>", error);
    }
}

bool ScriptComponent::SaveFieldValue(std::string_view name) {
    const ScriptField* field = m_fields.Find(name);
    if (field == nullptr) {
        return false;
    }
    if (!m_savedValues.is_object()) {
        m_savedValues = Json::object();
    }
    m_savedValues[field->name] = FieldToJson(*field);
    return true;
}

bool ScriptComponent::RevertFieldValue(std::string_view name) {
    const ScriptField* field = m_fields.Find(name);
    if (field == nullptr) {
        return false;
    }

    if (m_savedValues.is_object()) {
        m_savedValues.erase(field->name);
    }

    // Both halves matter. Forgetting the saved value alone would leave the
    // live object holding it until the next scene load, so the Inspector would
    // say "default" next to a number that is not the default.
    if (m_defaults.is_object() && m_defaults.contains(field->name)) {
        std::string error;
        if (!ReadFieldFromJson(*field, m_defaults.at(field->name), error)) {
            // Cannot happen from the engine's own snapshot, which was written
            // by FieldToJson from this very field. Reported rather than
            // asserted, because an unreachable branch that is wrong is worth
            // finding out about.
            ENGINE_LOG_ERROR(Channels::kScene,
                             "could not put the default back for '{}' on '{}': {}",
                             field->name, m_scriptName, error);
            return false;
        }
    }
    return true;
}

bool ScriptComponent::IsFieldOverridden(std::string_view name) const {
    // std::string, not the view. nlohmann's object is a std::map keyed on
    // std::string, and whether it will look one up from a view depends on the
    // comparator its version happens to use - so the conversion is written
    // here instead of relying on that.
    return m_savedValues.is_object() && m_savedValues.contains(std::string(name));
}

std::vector<std::string> ScriptComponent::UnusedSavedValues() const {
    std::vector<std::string> unused;
    if (!m_savedValues.is_object()) {
        return unused;
    }
    for (const auto& [key, value] : m_savedValues.items()) {
        if (m_fields.Find(key) == nullptr) {
            unused.push_back(key);
        }
    }
    return unused;
}

std::size_t ScriptComponent::ForgetUnusedSavedValues() {
    // ======================================================================
    //  REFUSED WHILE THE SCRIPT IS UNRESOLVED.
    //
    //  With nothing bound there are no fields, so every saved value looks
    //  unused and this would empty the whole block - which is the exact data
    //  loss the component exists to prevent. The Inspector does not offer the
    //  button in that state either; this is the half that cannot be bypassed
    //  by calling the function directly.
    // ======================================================================
    if (!IsResolved()) {
        return 0;
    }

    std::size_t removed = 0;
    for (const std::string& key : UnusedSavedValues()) {
        removed += m_savedValues.erase(key);
    }
    return removed;
}

// OnStart happens on the first update of EITHER kind, not at attach. See
// ScriptComponent.h.
//
// It lives in its own function because there are now two callers and OnStart
// must happen exactly once between them. Leaving the test inside each one
// would be two copies of the same rule, and the failure - OnStart running
// twice for a script that has both updates - is the kind that looks like a
// gameplay bug rather than a lifecycle one.
//
// m_started is set even when there is no OnStart to call, because it also
// means "this script is live now" - which is what gates collisions, and what
// tells the system it can stop walking a script that has no update.
void ScriptComponent::EnsureStarted() {
    if (m_started) {
        return;
    }
    m_started = true;
    if (m_hooks.start != nullptr) {
        m_hooks.start(m_behaviour.get());
    }
}

void ScriptComponent::FixedTick() {
    if (m_behaviour == nullptr) {
        return;   // the script is not compiled into this build
    }
    EnsureStarted();

    if (m_hooks.fixedUpdate != nullptr) {
        m_hooks.fixedUpdate(m_behaviour.get());
    }
}

void ScriptComponent::FrameTick() {
    if (m_behaviour == nullptr) {
        return;
    }
    EnsureStarted();

    if (m_hooks.update != nullptr) {
        m_hooks.update(m_behaviour.get());
    }
}

void ScriptComponent::DispatchCollision(const std::string& messageType, EntityId other) {
    // A contact arriving before the first tick would mean OnStart has not run
    // yet, and delivering OnCollisionEnter to a behaviour that has not started
    // is exactly the kind of surprise that makes scripting feel unreliable. It
    // is dropped instead; a Stay will arrive next step anyway, because "stay"
    // repeats for as long as the two things overlap.
    if (m_behaviour == nullptr || !m_started || !m_hooks.AnyContact()) {
        return;
    }

    const bool isTrigger = messageType == MessageTypes::kTriggerEnter ||
                           messageType == MessageTypes::kTriggerStay ||
                           messageType == MessageTypes::kTriggerExit;

    // ======================================================================
    //  THE MISMATCH THAT WOULD OTHERWISE BE SILENT.
    //
    //  A script with OnCollisionEnter, attached to an entity whose collider is
    //  a TRIGGER, is never called: the pair sends trigger events and the
    //  script is listening for collision ones. Both hooks are real, both
    //  spelled correctly, and nothing goes wrong - it simply never fires.
    //
    //  That is the worst kind of bug to leave unreported, so it is reported.
    //  Once per script instance, naming the hook to use instead.
    //
    //  Not a compile error, because it CANNOT be one: whether a collider is a
    //  trigger is data in a scene file, and the same script may be correct on
    //  one entity and wrong on another.
    // ======================================================================
    if (isTrigger && !m_hooks.AnyTrigger() && m_hooks.AnyCollision() &&
        !m_warnedAboutTriggerMismatch) {
        m_warnedAboutTriggerMismatch = true;

        const Entity* owner = Owner();
        ENGINE_LOG_ERROR(Channels::kScene,
                         "script '{}' on '{}' has OnCollision hooks but the collider "
                         "it touched is a TRIGGER, which sends OnTriggerEnter / Stay / "
                         "Exit instead - so none of them will ever run. Rename the hook "
                         "to OnTrigger..., or clear \"trigger\" on the collider. See "
                         "physics/Collider.h.",
                         m_scriptName,
                         owner != nullptr ? owner->Name() : "<unknown>");
    }

    if (isTrigger) {
        if (messageType == MessageTypes::kTriggerEnter) {
            if (m_hooks.triggerEnter != nullptr) {
                m_hooks.triggerEnter(m_behaviour.get(), other);
            }
        } else if (messageType == MessageTypes::kTriggerStay) {
            if (m_hooks.triggerStay != nullptr) {
                m_hooks.triggerStay(m_behaviour.get(), other);
            }
        } else if (m_hooks.triggerExit != nullptr) {
            m_hooks.triggerExit(m_behaviour.get(), other);
        }
        return;
    }

    // ---- a solid collision, which carries the contact data ---------------
    //
    // The contact is looked up rather than carried in the message. A Message
    // has room for two floats and an entity by design - see Messaging.h - and
    // a contact needs five numbers. Widening the message for this would make
    // every message in the engine bigger, so the collision system keeps the
    // contact for the current step and it is fetched here.
    Collision2D info;
    info.other = other;

    if (const Collision2D* found = CollisionSystem::FindContact(OwnerId(), other);
        found != nullptr) {
        info = *found;

        // The stored copy is oriented for whichever entity sorted first in the
        // pair. When that is not the one receiving this event, everything with
        // a direction has to be turned round.
        if (info.other != other) {
            std::swap(info.thisCollider, info.otherCollider);
            info.other            = other;
            info.normal           = -info.normal;
            info.relativeVelocity = -info.relativeVelocity;
        }
    }

    if (messageType == MessageTypes::kCollisionEnter) {
        if (m_hooks.collisionEnter != nullptr) {
            m_hooks.collisionEnter(m_behaviour.get(), info);
        }
    } else if (messageType == MessageTypes::kCollisionStay) {
        if (m_hooks.collisionStay != nullptr) {
            m_hooks.collisionStay(m_behaviour.get(), info);
        }
    } else if (messageType == MessageTypes::kCollisionExit) {
        // An exit has no contact - they are no longer touching - so `info`
        // here carries the partner and nothing else. A script wanting the
        // normal of a contact should read it on Enter or Stay.
        if (m_hooks.collisionExit != nullptr) {
            m_hooks.collisionExit(m_behaviour.get(), info);
        }
    }
}

// ---------------------------------------------------------------------------
//  ScriptSystem
// ---------------------------------------------------------------------------

void ScriptSystem::Register(ScriptComponent& script) {
    if (std::find(g_scripts.begin(), g_scripts.end(), &script) == g_scripts.end()) {
        g_scripts.push_back(&script);
    }
    AddToTicking(&script);
}

void ScriptSystem::Unregister(ScriptComponent& script) {
    std::erase(g_scripts, &script);
    std::erase(g_fixedTicking, &script);
    std::erase(g_frameTicking, &script);
}

void ScriptSystem::Clear() {
    g_scripts.clear();
    g_fixedTicking.clear();
    g_frameTicking.clear();
}
std::size_t ScriptSystem::Count() { return g_scripts.size(); }
std::size_t ScriptSystem::TickingCount()      { return g_fixedTicking.size(); }
std::size_t ScriptSystem::FrameTickingCount() { return g_frameTicking.size(); }

std::size_t ScriptSystem::UnresolvedCount() {
    std::size_t count = 0;
    for (const ScriptComponent* script : g_scripts) {
        if (!script->IsResolved()) {
            ++count;
        }
    }
    return count;
}

std::size_t ScriptSystem::CountUsing(std::string_view scriptName) {
    std::size_t count = 0;
    for (const ScriptComponent* script : g_scripts) {
        if (script->ScriptName() == scriptName) {
            ++count;
        }
    }
    return count;
}

std::size_t ScriptSystem::RebindRenamed(std::string_view oldName,
                                        std::string_view newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) {
        return 0;
    }

    std::size_t moved = 0;
    // A copy, because SetScriptName re-registers and therefore touches the
    // very lists being walked.
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        if (script->ScriptName() == oldName) {
            script->SetScriptName(newName);
            ++moved;
        }
    }
    return moved;
}

void ScriptSystem::UnbindAll() {
    // A copy of the list is walked, because unbinding does not remove anything
    // from g_scripts - but being careful here costs nothing and the rule
    // "never modify a list you are walking" is worth applying consistently.
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        script->UnbindForReload();
    }
    // Nothing can need ticking while nothing is bound.
    g_fixedTicking.clear();
    g_frameTicking.clear();
}

void ScriptSystem::RebindAll() {
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        script->RebindAfterReload();
        AddToTicking(script);
    }
}

// ---------------------------------------------------------------------------
//  The two walks.
//
//  They are the same four lines twice rather than one shared helper taking a
//  list and a member-function pointer. The shared version was written first
//  and read considerably worse than this does, for no saving: the whole body
//  is a loop and an erase_if.
// ---------------------------------------------------------------------------

void ScriptSystem::Update(float /*fixedStepSeconds*/) {
    // The delta is no longer passed down. A script asks Time::DeltaTime() for
    // it, which returns the fixed step because Engine has marked this as a
    // fixed step - see Time.h. The parameter stays because this is the System
    // interface, which the render-stage systems also use.
    //
    // Walked by index with the size re-read each time. A script's
    // OnFixedUpdate is allowed to attach another script, which appends to
    // this very list. A range-for would be reading the list while it changed
    // underneath.
    for (std::size_t i = 0; i < g_fixedTicking.size(); ++i) {
        g_fixedTicking[i]->FixedTick();
    }

    // Drop anything that no longer needs walking. This is where a script
    // whose only hook was OnStart leaves the list: it needed one tick to
    // start, and from the next step onwards it costs nothing.
    //
    // Done AFTER the walk rather than inside it, because removing entries
    // from a list while stepping through it by index skips whatever moves
    // into the gap.
    std::erase_if(g_fixedTicking, [](const ScriptComponent* script) {
        return !script->NeedsFixedTick();
    });
}

void ScriptSystem::FrameUpdate() {
    for (std::size_t i = 0; i < g_frameTicking.size(); ++i) {
        g_frameTicking[i]->FrameTick();
    }

    std::erase_if(g_frameTicking, [](const ScriptComponent* script) {
        return !script->NeedsFrameTick();
    });
}

void ScriptSystem::SubscribeToCollisions() {
    if (g_collisionsSubscribed) {
        return;
    }
    g_collisionsSubscribed = true;

    // ONE subscription per message type for ALL scripts, rather than one per
    // component. A hundred scripted entities would otherwise mean three
    // hundred subscriptions for the bus to walk on every single collision.
    const auto forward = [](const Message& message) {
        Scene* scene = Scene::Active();
        if (scene == nullptr) {
            return;
        }
        Entity* entity = scene->Get(message.target);
        if (entity == nullptr) {
            return;   // destroyed between the collision and the delivery
        }
        // EVERY SCRIPT ON THE ENTITY, not just the first one.
        //
        // This used to be Find<ScriptComponent>(), which returns the FIRST
        // match - so an entity carrying two scripts delivered collisions to
        // one of them and silently dropped them for the other. Which one
        // depended on the order they happened to be listed in the scene file,
        // and nothing reported the difference: OnStart and OnUpdate ran for
        // both, so the script looked attached and working right up until it
        // needed to know it had been touched.
        //
        // WALKED BY INDEX with the size re-read each time, exactly as
        // ScriptSystem::Update does, and for the same reason: a script
        // handling a collision is allowed to attach another component.
        for (std::size_t i = 0; i < entity->ComponentCount(); ++i) {
            Component* component = entity->ComponentAt(i);
            if (component == nullptr) {
                continue;
            }
            // Compared as TEXT, not as pointers. Both sides are string
            // literals, but the two literals live in different binaries -
            // engine.dll and userContent.dll - and nothing promises the
            // linker folded them into one address.
            if (std::string_view(component->TypeName()) != ScriptComponent::kTypeName) {
                continue;
            }
            static_cast<ScriptComponent*>(component)->DispatchCollision(message.type,
                                                                        message.other);
        }
    };

    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionEnter, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionStay, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionExit, forward);

    // The trigger half goes to the same place. One dispatcher, which is what
    // keeps the two kinds of contact from drifting apart in how they are
    // delivered - see ScriptComponent::DispatchCollision.
    MessageBus::SubscribeBroadcast(MessageTypes::kTriggerEnter, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kTriggerStay, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kTriggerExit, forward);
}

void ScriptSystem::RegisterComponentTypes() {
    ComponentFactory::Register(ScriptComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<ScriptComponent>();
                               });
}

} // namespace eng
