// ============================================================================
//  ScriptComponent.cpp - scripts. See ScriptComponent.h for why the connection
//  between a component and its behaviour is made by NAME, and why the
//  lifecycle hooks are found by the compiler rather than declared.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/core/Time.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/physics/Collider.h>
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
#include <filesystem>
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

// ---------------------------------------------------------------------------
//  WHICH RESUME PASS IS RUNNING, counted so that no coroutine frame is resumed
//  twice within one of them. Bumped by ScriptSystem at the top of each pass and
//  stamped onto an entry when it is resumed or created; see
//  ScriptComponent::RunningCoroutine::resumedInPass for the case that needs it.
//
//  NEVER RESET, and it does not need to be: at one pass per frame per phase it
//  takes some five thousand million years at 60fps to wrap a 64-bit counter.
//  Zero is the value a fresh entry carries, and the first pass is 1, so an
//  entry made before any pass ran is always eligible for the next one.
// ---------------------------------------------------------------------------
std::uint64_t g_resumePass = 0;

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
//  The verbs. See the long block at the declaration in ScriptComponent.h for
//  what each one replaces; every one of them is written so that a script whose
//  entity has already been destroyed gets a harmless answer rather than a
//  crash, which is what makes the null checks they remove unnecessary rather
//  than merely hidden.
// ---------------------------------------------------------------------------

const std::string& ScriptBehaviour::Name() const {
    // A function-local static rather than a member, so that the reference
    // handed back for a dead entity is valid for the life of the program and
    // every caller gets the same one. Returning a reference to a temporary
    // here would compile and then read freed memory on the next line.
    static const std::string kNone;
    Entity*                  self = Owner();
    return self != nullptr ? self->Name() : kNone;
}

bool ScriptBehaviour::Rename(std::string_view newName) const {
    Scene*  world = GetScene();
    Entity* self  = Owner();
    if (world == nullptr || self == nullptr) {
        return false;
    }
    // ======================================================================
    //  AN EMPTY NAME IS RECOGNISED HERE, because it is the one refusal
    //  Scene::RenameEntity does not report itself.
    //
    //  It answers false two ways: an empty name, silently, and a name
    //  something else holds, with its own warning. Reporting both from below
    //  as a collision told the script that something already held "", which
    //  nothing can - the message named a cause that cannot exist.
    // ======================================================================
    const std::string who =
        m_component != nullptr ? m_component->ScriptName() : std::string();

    if (newName.empty()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "the script '{}' could not rename '{}': a name cannot be "
                        "empty - it is what parents and entity references are "
                        "recorded by. Nothing has changed",
                        who, self->Name());
        return false;
    }

    if (world->RenameEntity(self->Id(), newName)) {
        return true;
    }

    // ======================================================================
    //  WHAT IS LEFT IS A COLLISION, with the empty name handled above and the
    //  entity known to exist. Scene::RenameEntity has warned about it too;
    //  this line adds WHICH SCRIPT asked, which is the part it cannot know.
    //
    //  Making the name unique instead and answering true would leave the
    //  script believing an entity is called what it asked for when it is
    //  called something else - and a name is an identity here, so every
    //  reference recorded against the name it chose would be wrong.
    // ======================================================================
    ENGINE_LOG_WARN(Channels::kScene,
                    "the script '{}' could not rename '{}' to '{}': something else in "
                    "the scene already has that name, and a name is an identity here. "
                    "Nothing has changed",
                    who, self->Name(), newName);
    return false;
}

const std::string& ScriptBehaviour::Tag() const {
    // A function-local static for the same reason Name() uses one: the
    // reference handed back for a dead entity has to outlive the call.
    static const std::string kNone;
    Entity*                  self = Owner();
    return self != nullptr ? self->Tag() : kNone;
}

void ScriptBehaviour::SetTag(std::string_view newTag) const {
    if (Entity* self = Owner(); self != nullptr) {
        self->SetTag(newTag);
    }
}

bool ScriptBehaviour::HasTag(std::string_view wantedTag) const {
    Entity* self = Owner();
    return self != nullptr && self->HasTag(wantedTag);
}

bool ScriptBehaviour::IsActive() const {
    Entity* self = Owner();
    return self != nullptr && self->IsActiveInHierarchy();
}

void ScriptBehaviour::SetActive(bool active) const {
    if (Entity* self = Owner(); self != nullptr) {
        self->SetActive(active);
    }
}

// ---------------------------------------------------------------------------
//  `world` rather than `scene` for the local, from here to the end of this
//  block, because `scene` is one of the bare-name handles on this class now
//  and a local of that name would hide it. Silently, as it happens - C4458 is
//  a /W4 warning and a project's scripts compile at /W3 - so inside the engine
//  the rename is the honest fix rather than a suppression.
// ---------------------------------------------------------------------------

Entity* ScriptBehaviour::FindEntity(EntityId id) const {
    Scene* world = GetScene();
    return world != nullptr ? world->Get(id) : nullptr;
}

Entity* ScriptBehaviour::FindEntity(std::string_view entityName) const {
    Scene* world = GetScene();
    return world != nullptr ? world->FindEntity(entityName) : nullptr;
}

Entity* ScriptBehaviour::FindEntityWithTag(std::string_view wantedTag) const {
    Scene* world = GetScene();
    return world != nullptr ? world->FindByTag(wantedTag) : nullptr;
}

EntityId ScriptBehaviour::Instantiate(std::string_view prefabPath) const {
    return Instantiate(prefabPath, SpawnOptions{});
}

EntityId ScriptBehaviour::Instantiate(std::string_view     prefabPath,
                                      const SpawnOptions& placement) const {
    Scene* world = GetScene();
    if (world == nullptr) {
        return EntityId{};
    }

    // ======================================================================
    //  THE ERROR IS REPORTED HERE, not handed back.
    //
    //  Scene::InstantiatePrefab takes an outError because the editor needs the
    //  text to put in a dialog. A script has nowhere to put it: the honest
    //  options are to log it or to drop it, and a spawn that silently does
    //  nothing is the single most confusing failure a script can have - the
    //  gun fires, nothing appears, and there is no message anywhere.
    //
    //  The script's NAME is in the message because "could not load
    //  prefabs/bullet.oprefab" does not say which of forty scripts asked.
    // ======================================================================
    std::string    error;
    const EntityId spawned = world->InstantiatePrefab(prefabPath, placement, error);
    if (spawned.IsNull()) {
        const std::string& who =
            m_component != nullptr ? m_component->ScriptName() : std::string();
        ENGINE_LOG_ERROR(Channels::kGame,
                         "the script '{}' on '{}' could not spawn '{}': {}", who,
                         Name().empty() ? "<a destroyed entity>" : Name().c_str(),
                         prefabPath, error);
    }
    return spawned;
}

EntityId ScriptBehaviour::Instantiate(const PrefabRef& prefab) const {
    return Instantiate(prefab, SpawnOptions{});
}

EntityId ScriptBehaviour::Instantiate(const PrefabRef&    prefab,
                                      const SpawnOptions& placement) const {
    if (!prefab.IsSet()) {
        // ==================================================================
        //  ONCE PER COMPONENT, NOT ONCE PER CALL.
        //
        //  A spawner that asks every step would otherwise write this line
        //  sixty times a second for as long as the reference is unset, and a
        //  Console with four thousand copies of one message in it is a Console
        //  nobody reads. The flag lives on the ScriptComponent rather than on
        //  the behaviour, so it survives neither a reload nor a reattach -
        //  which is right: after a rebuild the message is worth saying again.
        // ==================================================================
        if (m_component != nullptr && !m_component->m_warnedAboutUnsetPrefab) {
            m_component->m_warnedAboutUnsetPrefab = true;
            ENGINE_LOG_ERROR(Channels::kGame,
                             "the script '{}' on '{}' was asked to spawn a prefab, but "
                             "its prefab variable has not been set - drag a prefab onto "
                             "it in the Inspector. Nothing has been spawned, and this "
                             "is reported once rather than on every attempt",
                             m_component->ScriptName(),
                             Name().empty() ? "<a destroyed entity>" : Name());
        }
        return EntityId{};
    }
    return Instantiate(prefab.Path(), placement);
}

EntityId ScriptBehaviour::CreateEntity(std::string_view baseName) const {
    Scene* world = GetScene();
    return world != nullptr ? world->CreateEntityUniquelyNamed(baseName) : EntityId{};
}

EntityId ScriptBehaviour::CreateEntity(std::string_view     baseName,
                                       const SpawnOptions& placement) const {
    Scene* world = GetScene();
    return world != nullptr ? world->CreateEntityUniquelyNamed(baseName, placement)
                            : EntityId{};
}

void ScriptBehaviour::DestroySelf() const {
    DeferredOps::QueueDestroy(OwnerId());
}

void ScriptBehaviour::Destroy(EntityId id) const {
    DeferredOps::QueueDestroy(id);
}

void ScriptBehaviour::Destroy(Entity& entity) const {
    DeferredOps::QueueDestroy(entity.Id());
}

void ScriptBehaviour::DestroySelfAfter(float delaySeconds) const {
    DeferredOps::QueueDestroyAfter(OwnerId(), delaySeconds);
}

void ScriptBehaviour::DestroyAfter(EntityId id, float delaySeconds) const {
    DeferredOps::QueueDestroyAfter(id, delaySeconds);
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

namespace {

// ---------------------------------------------------------------------------
//  ONE SPELLING FOR ONE FILE, which __FILE__ does not give you.
//
//  Every file that includes a header gets its own __FILE__ for it, and the
//  compiler does not spell them alike. Measured, for one header included three
//  ways:
//
//      C:\dev\proj\assets\scripts\Bullet.h              from Bullet.cpp
//      C:/dev/proj/assets/scripts/Bullet.h              from the headers unit
//      C:\dev\proj\assets\scripts\Enemies\../Bullet.h   from a sub-folder
//
//  Compared raw, that one file is three, and the duplicate check below then
//  reports a clash between a file and itself - telling somebody to "rename one
//  of them" when only one of them exists. Which is exactly the happy path of a
//  shared script class, so it has to be right rather than nearly right.
// ---------------------------------------------------------------------------
std::string NormalizedSourcePath(std::string_view spelled) {
    if (spelled.empty()) {
        return {};
    }
    return std::filesystem::path(spelled).lexically_normal().generic_string();
}

// How many registrations have been recognised as the SAME script arriving
// again. Read either side of a library load; see ScriptRegistry::RepeatCount.
std::size_t g_repeatRegistrations = 0;

} // namespace

std::size_t ScriptRegistry::RepeatCount() { return g_repeatRegistrations; }

void ScriptRegistry::Register(std::string_view scriptName, CreateFn create,
                              const ScriptHooks& hooks, std::string_view sourceFile) {
    if (scriptName.empty() || create == nullptr) {
        return;
    }

    ScriptTable&      table  = Table();
    const std::string name(scriptName);
    const std::string source = NormalizedSourcePath(sourceFile);

    if (table.contains(name)) {
        const Entry& already = table.at(name);

        // The same script, seen twice. This is normal and harmless: a script
        // written in a .h and included by two .cpp files registers once per
        // file that included it. Same name, same file, same class - the second
        // one has nothing to add.
        //
        // COUNTED, because a library whose scripts ALL arrive this way has not
        // failed - it has been beaten to them by whoever included the header
        // first - and the loader would otherwise warn that nothing in it can
        // run. See ScriptLibrary::Load.
        if (already.sourceFile == source) {
            ++g_repeatRegistrations;
            return;
        }

        // Two DIFFERENT files claiming the same name is a real problem: one of
        // them will never run, and which one is decided by something nobody
        // can see. The first is kept, so at least the choice is stable.
        ENGINE_LOG_WARN(Channels::kScene,
                        "two different files both define a script called '{}' ('{}' and "
                        "'{}') - only the first can run, so rename one of them",
                        scriptName, already.sourceFile, source);
        return;
    }

    Entry entry;
    entry.create     = create;
    entry.hooks      = hooks;
    entry.sourceFile = source;
    table[name]      = entry;
}

bool ScriptRegistry::SetExposer(std::string_view scriptName,
                                void (*expose)(ScriptBehaviour*, ScriptFields&)) {
    if (expose == nullptr) {
        return false;
    }

    const auto it = Table().find(std::string(scriptName));
    if (it == Table().end()) {
        // Cannot happen from generated code, which is compiled into the same
        // translation unit as the registration and runs after it. Reported
        // rather than ignored, because if it ever does happen the symptom
        // would be an Inspector that is mysteriously empty.
        ENGINE_LOG_WARN(Channels::kScene,
                        "an exposer arrived for '{}', which is not a registered script",
                        scriptName);
        return false;
    }

    if (it->second.hooks.expose != nullptr) {
        return false;   // the script wrote its own OnExpose, and it wins
    }

    it->second.hooks.expose = expose;
    return true;
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

bool ScriptRegistry::Remove(std::string_view scriptName) {
    return Table().erase(std::string(scriptName)) > 0;
}

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
    // Nothing reaches Bind with a live behaviour today, but it would replace
    // one silently - m_behaviour is a unique_ptr and the assignment below has
    // no hook. A frame outliving the object it points into is not a leak, it
    // is a dangling `this` resumed on a later frame, so the guard is written
    // rather than reasoned about. RebindAll is public.
    DestroyAllCoroutines();

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
    // FIRST, while the behaviour is still whole. A suspended frame holds the
    // script's `this` and its own locals, and those locals are destroyed by
    // handle.destroy() - which must therefore happen before the object they
    // point into. It is also what keeps destroy() from being called after the
    // script's DLL has been unloaded: ScriptLibrary::Unload runs UnbindAll
    // before SDL_UnloadObject, so this line is step one of a reload.
    //
    // ONE LINE FOR EVERY TEARDOWN PATH. Scene unload (open scene, Stop, every
    // undo step, a script-driven scene change), a script rebuild, a prefab
    // apply or revert, the stage-600 destroy drain, Inspector "Remove
    // Component", SetScriptName and ~ScriptComponent all reach OnDetach or the
    // destructor, and both call this.
    DestroyAllCoroutines();

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

    // ======================================================================
    //  A SWITCHED-OFF ENTITY'S SCRIPTS DO NOT RUN - AND DO NOT START.
    //
    //  The order matters: this is checked BEFORE EnsureStarted, so a script
    //  attached to something that is switched off gets its OnStart when it is
    //  switched on, not when it was built. A script whose OnStart ran while
    //  its entity was off would have looked around a world it is not part of.
    //
    //  It stays in the ticking list rather than being removed, so switching
    //  the entity back on needs nothing more than the flag. See
    //  Entity::IsActiveInHierarchy.
    // ======================================================================
    if (!OwnerIsActive()) {
        return;
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
    if (!OwnerIsActive()) {
        // STILL SWEPT, THOUGH. A coroutine stopped while this entity was
        // switched off would otherwise keep its frame - and the script's
        // `this` inside it - until the entity came back, and would keep this
        // component in the frame-ticking list to be walked twice a frame
        // doing nothing. Freeing a stopped frame is bookkeeping, not running
        // the game, so it is safe here and nothing else about the entity
        // resumes. See SweepFinishedCoroutines.
        SweepFinishedCoroutines();

        // See FixedTick above - and note what this means for coroutines: a
        // switched-off entity PAUSES them rather than losing them, which is
        // this engine's rule for a disabled entity everywhere else. Unity
        // destroys a GameObject's coroutines when it is deactivated; the local
        // rule wins here because every other thing on a disabled entity
        // resumes where it left off, and a coroutine that silently evaporated
        // would be the one exception.
        return;
    }
    EnsureStarted();

    if (m_hooks.update != nullptr) {
        m_hooks.update(m_behaviour.get());
    }

    // AFTER the update hook, which is where Unity resumes too: `yield return
    // null` continues once every MonoBehaviour has had its Update. There is no
    // LateUpdate in this engine to come before.
    ResumeCoroutines(ResumePhase::Update);
}

void ScriptComponent::EndOfFrameTick() {
    if (m_behaviour == nullptr) {
        return;
    }
    if (!OwnerIsActive()) {
        // Paused, not lost - the rule FrameTick follows above, for the same
        // reason. A coroutine waiting for the end of the frame on an entity
        // that was switched off mid-frame waits for the end of a later one.
        return;
    }

    // NO EnsureStarted AND NO UPDATE HOOK; see the declaration. OnStart belongs
    // to whichever ordinary tick gets there first, and firing it here would run
    // a script's first line after its first frame had already been drawn.
    ResumeCoroutines(ResumePhase::EndOfFrame);
}

// ---------------------------------------------------------------------------
//  Coroutines
// ---------------------------------------------------------------------------

CoroutineId ScriptComponent::AddCoroutine(Coroutine&& routine) {
    Coroutine::Handle handle = routine.Release();
    if (!handle) {
        return CoroutineId::None;
    }

    const CoroutineId id = static_cast<CoroutineId>(m_nextCoroutineId++);

    // STAMPED WITH THE PASS THAT IS RUNNING, which is what stops the walk from
    // resuming this a second time in the pass that created it - including when
    // the creator is a coroutine on a DIFFERENT component that has not been
    // walked yet. See RunningCoroutine::resumedInPass. Outside a pass this is
    // the last completed one, which no later pass matches, so the coroutine is
    // eligible on the next one.
    m_coroutines.push_back(RunningCoroutine{id, handle, false, g_resumePass});

    // STARTED NOW, not next frame, because that is what Unity does: the body
    // runs up to its first yield inside StartCoroutine. initial_suspend is
    // suspend_always precisely so that this call is the thing that begins it.
    handle.resume();

    // AND BACK ON THE FRAME LIST. A coroutine started from a collision hook or
    // from OnStart belongs to a component that may have been dropped from
    // g_frameTicking already - it is only kept there while it has an update
    // hook or has not started. Register is idempotent.
    ScriptSystem::Register(*this);
    return id;
}

void ScriptComponent::ResumeCoroutines(ResumePhase phase) {
    if (m_coroutines.empty()) {
        return;
    }

    using Wait = Coroutine::promise_type::Wait;

    const bool endOfFramePass = phase == ResumePhase::EndOfFrame;

    // Only the update pass spends time, so only it asks what the deltas were.
    const float scaled   = endOfFramePass ? 0.0f : Time::DeltaTime();
    const float unscaled = endOfFramePass ? 0.0f : Time::UnscaledDeltaTime();

    // THE SIZE IS TAKEN ONCE, BEFORE THE WALK. A coroutine may start another
    // one, which appends here; that one has already been resumed by
    // AddCoroutine and must not be resumed twice on the same frame.
    //
    // AND NOTHING IS ERASED INSIDE THE WALK. A resume can call back into this
    // component - StopCoroutine on itself, StopAllCoroutines, or starting
    // another coroutine, which may reallocate the vector. So the entry is
    // re-read by index each time and nothing is held across resume().
    const std::size_t count = m_coroutines.size();
    for (std::size_t i = 0; i < count && i < m_coroutines.size(); ++i) {
        if (m_coroutines[i].stopRequested) {
            continue;
        }
        // ALREADY RESUMED IN THIS PASS. The snapshot above cannot see a
        // coroutine that another component's resume started on THIS component
        // before the walk got here; the stamp can. See
        // RunningCoroutine::resumedInPass.
        if (m_coroutines[i].resumedInPass == g_resumePass) {
            continue;
        }
        Coroutine::Handle handle = m_coroutines[i].handle;
        if (!handle || handle.done()) {
            continue;
        }

        Coroutine::promise_type& promise = handle.promise();

        // ---- THE PHASE FILTER ------------------------------------------
        //
        // A filter and not an assertion: both passes walk this same list and
        // each steps over the entries the other one owns. So a WaitForEndOfFrame
        // coroutine is invisible to the update pass and everything else is
        // invisible to the end-of-frame pass, and neither can resume a coroutine
        // the other already did.
        if ((promise.wait == Wait::EndOfFrame) != endOfFramePass) {
            continue;
        }

        if (promise.wait == Wait::Scaled) {
            promise.remaining -= scaled;
            if (promise.remaining > 0.0f) {
                continue;
            }
        } else if (promise.wait == Wait::Unscaled) {
            promise.remaining -= unscaled;
            if (promise.remaining > 0.0f) {
                continue;
            }
        }

        // STAMPED BEFORE THE RESUME, and by index. The resume may reallocate
        // m_coroutines, so there is no entry left to stamp afterwards.
        m_coroutines[i].resumedInPass = g_resumePass;

        handle.resume();
        // m_coroutines may have moved. Nothing of it is touched here.
    }

    // ---- the sweep, after the walk --------------------------------------
    //
    // The same discipline the two tick loops use, and here it is what makes a
    // coroutine able to stop itself: the frame is freed now, not during its
    // own resume.
    SweepFinishedCoroutines();
}

void ScriptComponent::SweepFinishedCoroutines() {
    // PHASE-BLIND, and that is deliberate rather than an oversight. A coroutine
    // suspended on WaitForEndOfFrame has a live handle and is not done, so
    // neither pass's sweep can mistake it for finished - while a STOPPED one
    // must be freed by whichever pass sweeps next, whatever it was waiting for.
    // Narrowing this to "entries this pass touched" would leave a frame, and
    // the locals in it, alive until the other pass came round.
    std::erase_if(m_coroutines, [](const RunningCoroutine& running) {
        const bool finished = !running.handle || running.handle.done();
        if (finished || running.stopRequested) {
            if (running.handle) {
                running.handle.destroy();
            }
            return true;
        }
        return false;
    });
}

void ScriptComponent::DestroyAllCoroutines() {
    // Walked and cleared rather than erase_if, because every one of them goes
    // and the vector is about to be empty either way.
    for (RunningCoroutine& running : m_coroutines) {
        if (running.handle) {
            running.handle.destroy();
        }
    }
    m_coroutines.clear();
}

CoroutineId ScriptBehaviour::StartCoroutine(Coroutine routine) const {
    if (m_component == nullptr) {
        // The gap between construction and bind. The frame is destroyed by
        // ~Coroutine on the way out of this function, so this is a no-op and
        // not a leak - which is what the class contract above promises.
        return CoroutineId::None;
    }
    return m_component->AddCoroutine(std::move(routine));
}

bool ScriptBehaviour::StopCoroutine(CoroutineId id) const {
    if (m_component == nullptr || id == CoroutineId::None) {
        return false;
    }
    for (ScriptComponent::RunningCoroutine& running : m_component->m_coroutines) {
        if (running.id == id && !running.stopRequested) {
            // MARKED, NOT DESTROYED. This may be reached from inside the very
            // coroutine being stopped; the sweep at the end of the resume pass
            // frees it.
            running.stopRequested = true;
            return true;
        }
    }
    return false;
}

void ScriptBehaviour::StopAllCoroutines() const {
    if (m_component == nullptr) {
        return;
    }
    for (ScriptComponent::RunningCoroutine& running : m_component->m_coroutines) {
        running.stopRequested = true;
    }
}

std::size_t ScriptBehaviour::RunningCoroutineCount() const {
    if (m_component == nullptr) {
        return 0;
    }
    std::size_t live = 0;
    for (const ScriptComponent::RunningCoroutine& running : m_component->m_coroutines) {
        if (!running.stopRequested && running.handle && !running.handle.done()) {
            ++live;
        }
    }
    return live;
}

void ScriptComponent::DispatchCollision(const std::string& messageType, EntityId other,
                                        int pairToken) {
    // A contact arriving before the first tick would mean OnStart has not run
    // yet, and delivering OnCollisionEnter to a behaviour that has not started
    // is exactly the kind of surprise that makes scripting feel unreliable. It
    // is dropped instead; a Stay will arrive next step anyway, because "stay"
    // repeats for as long as the two things overlap.
    if (m_behaviour == nullptr || !m_started || !m_hooks.AnyContact()) {
        return;
    }

    // A switched-off script receives no contact events either. It cannot
    // normally be sent one - a switched-off entity's collider does not
    // collide - but a message is queued at stage 400 and delivered at 500, so
    // something switched off in between would otherwise be told about a
    // contact it was no longer part of.
    if (!OwnerIsActive()) {
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
    //  AND NOT WHILE THE COLLISION HOOKS ARE ALSO FIRING, which became
    //  possible when contacts started being keyed per COLLIDER pair. One
    //  entity can carry a trigger collider and a solid one, so a script with
    //  only OnCollision hooks can legitimately be sent a trigger event by one
    //  of them while its own hooks run perfectly well from the other. Saying
    //  "none of them will ever run" would then be flatly false, and a false
    //  error in the Console is worse than no error at all.
    //
    //  FindContact is SOLID-ONLY, so a non-null answer means exactly "there
    //  is a solid contact with this entity this step" - which is to say the
    //  collision hooks are about to run.
    //  AND NOT WHEN THIS ENTITY HAS A SOLID COLLIDER AT ALL.
    //
    //  "No solid contact THIS STEP" is not proof that the collision hooks can
    //  never run; it is only proof that nothing solid is touching right now.
    //  A gate carrying a wide trigger sensor AND a small solid body is the
    //  ordinary case: on the steps where the player is inside the sensor and
    //  not yet against the gate, the guard saw a trigger event, no solid
    //  contact and collision-only hooks, and printed an error saying none of
    //  them would ever run - which was flatly false, and the comment above
    //  says a false error in the Console is worse than no error at all.
    //
    //  Asking whether a SOLID COLLIDER EXISTS is the real question, and it is
    //  the one the message is about: a script with OnCollision hooks on an
    //  entity that has one is correctly written, whatever is overlapping it
    //  at this instant.
    const auto hasSolidCollider = [this]() {
        const Entity* owner = Owner();
        if (owner == nullptr) {
            return false;
        }
        for (std::size_t i = 0; i < owner->ComponentCount(); ++i) {
            const auto* collider =
                dynamic_cast<const ColliderComponent*>(owner->ComponentAt(i));
            if (collider != nullptr && !collider->IsTrigger()) {
                return true;
            }
        }
        return false;
    };

    if (isTrigger && !m_hooks.AnyTrigger() && m_hooks.AnyCollision() &&
        CollisionSystem::FindContact(OwnerId(), other) == nullptr &&
        !hasSolidCollider() && !m_warnedAboutTriggerMismatch) {
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

    // ---- the contact, for a trigger exactly as for a solid collision -----
    //
    // The contact is looked up rather than carried in the message. A Message
    // has room for two floats and an entity by design - see Messaging.h - and
    // a contact needs five numbers and two colliders. Widening the message for
    // this would make every message in the engine bigger, so the collision
    // system keeps the current step's contacts and the event fetches one here.
    //
    // COLLISION RUNS AT STAGE 400 AND THIS AT 500, with nothing able to run in
    // between: Engine::Simulate calls MessageBus::Dispatch() directly rather
    // than through the scheduler. So the table still holds this step's contacts
    // and both colliders are still alive.
    //
    // A TRIGGER PAIR HAS A FULL CONTACT. The narrow phase never looks at
    // IsTrigger - it fills in the normal, the point and the separation for
    // every touching pair, and only the SOLVER skips the trigger ones. So a
    // trigger hook is handed a real contact normal, which is more than Unity's
    // OnTriggerEnter2D gets.
    // ======================================================================
    //  AN EXIT IS NOT LOOKED UP AT ALL, and that is correctness rather than a
    //  saved map lookup.
    //
    //  An exit pair is BY DEFINITION one that was touching last step and is
    //  not now. While contacts were keyed by ENTITY pair there was therefore
    //  never an entry to find, and `info` stayed empty on its own - which is
    //  what physics/Contact.h promises at the separation and relativeVelocity
    //  fields, and what the note further down this function already states.
    //
    //  KEYED PER COLLIDER PAIR THAT IS NO LONGER AUTOMATIC, and getting it
    //  wrong would be invisible. A compound body can have ONE collider stop
    //  touching while a SIBLING goes on touching the same entity - and
    //  FindContact answers per ENTITY pair, so the lookup would come back
    //  holding the sibling's LIVE contact: a real normal, a real separation,
    //  handed to a handler for a pair that has just ended. Every number in it
    //  would look plausible.
    //
    //  So the question is not asked. Both kinds of exit, because the trigger
    //  family reaches this point too once its hooks can take a contact.
    // ======================================================================
    const bool isExit = messageType == MessageTypes::kCollisionExit ||
                        messageType == MessageTypes::kTriggerExit;

    Collision2D info;
    info.other = other;

    if (!isExit) {
        // THE INDEX FOR THE KIND BEING DELIVERED. Asking the wrong one finds
        // nothing at all for a compound body carrying both kinds, which is
        // how this was caught: the trigger hooks were handed an empty contact
        // because FindContact answers only for solid pairs.
        // ==================================================================
        //  BY THE TOKEN THE EVENT CARRIES, which names the COLLIDER PAIR.
        //
        //  It used to be looked up by ENTITY pair, and that index holds one
        //  contact per entity pair - the deepest. So a compound body's two or
        //  three events against one wall were all handed the SAME contact:
        //  `hit.thisCollider` named the same collider every time, and the
        //  shallower pairs' contacts could not be reached from any API, while
        //  Collider.h promises the opposite in as many words.
        //
        //  The entity lookups are still the right answer for a caller that
        //  only has two entity ids - a script asking "am I touching that?" -
        //  and they are still what FindContact is for. They are the fallback
        //  here for a message that carries no token, which is anything not
        //  sent by the collision system.
        // ==================================================================
        const Collision2D* found = CollisionSystem::ContactForEvent(pairToken);
        if (found == nullptr) {
            found = isTrigger ? CollisionSystem::FindTriggerContact(OwnerId(), other)
                              : CollisionSystem::FindContact(OwnerId(), other);
        }
        if (found != nullptr) {
            info = *found;

            // The stored copy is oriented for whichever collider sorted first
            // in the pair. When that is not the one receiving this event,
            // everything with a direction has to be turned round. THIS IS WHAT
            // MAKES thisCollider MEAN MY COLLIDER IN MY HANDLER.
            if (info.other != other) {
                std::swap(info.thisCollider, info.otherCollider);
                info.other            = other;
                info.normal           = -info.normal;
                info.relativeVelocity = -info.relativeVelocity;
            }
        }
    }

    if (isTrigger) {
        if (messageType == MessageTypes::kTriggerEnter) {
            if (m_hooks.triggerEnter != nullptr) {
                m_hooks.triggerEnter(m_behaviour.get(), info);
            }
        } else if (messageType == MessageTypes::kTriggerStay) {
            if (m_hooks.triggerStay != nullptr) {
                m_hooks.triggerStay(m_behaviour.get(), info);
            }
        } else if (m_hooks.triggerExit != nullptr) {
            m_hooks.triggerExit(m_behaviour.get(), info);
        }
        return;
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

std::size_t ScriptComponent::RetargetReferences(std::string_view oldName,
                                                std::string_view newName) {
    if (oldName.empty() || oldName == newName) {
        return 0;
    }

    std::size_t moved = 0;
    for (const ScriptField& field : m_fields.All()) {
        EntityRef* ref = field.AsEntityRef();
        if (ref == nullptr || ref->TargetName() != oldName) {
            continue;
        }

        ref->Set(newName);
        ++moved;

        // ==================================================================
        //  BOTH HALVES, ALWAYS - AND THE "ALWAYS" IS THE CORRECTION.
        //
        //  The live reference is what the game reads; the saved value is what
        //  the scene file gets. Moving the first and not the second works
        //  perfectly until the level is loaded again.
        //
        //  This was once guarded by "only if it was already overridden", on
        //  the reasoning that a rename should not invent an edit to a field
        //  nobody touched. That reasoning is wrong, and a mutation test is
        //  what showed it:
        //
        //      eng::EntityRef scoreLabel{"HudScore"};
        //
        //  is a reference whose default is a NAME, written in the code - see
        //  GameRules.cpp. Rename HudScore and the live reference follows, the
        //  default still says "HudScore", and with nothing saved the next
        //  load puts the old name back and the HUD is silently broken.
        //
        //  There is no spurious case to protect against. Getting here means
        //  the reference WAS pointing at the renamed entity, so it now
        //  differs from whatever the code says - which is exactly what a
        //  saved value is for. A reference that was unset, or pointed
        //  somewhere else, never reaches this line.
        // ==================================================================
        SaveFieldValue(field.name);
    }

    // ======================================================================
    //  AN UNRESOLVED SCRIPT'S REFERENCES CANNOT BE MOVED, and guessing would
    //  be worse than leaving them.
    //
    //  With no script bound there are no fields, so there is nothing to say
    //  which of the saved values are entity references. Patching every saved
    //  STRING that happens to equal the old name would also rewrite a
    //  `std::string label` that legitimately read "Player" - silently, in a
    //  file, as a side effect of renaming something else.
    //
    //  So a rename while a script is not compiled in can orphan that script's
    //  reference. The Inspector shows it in red against the name it still
    //  holds, which is enough to put right by hand, and nothing is lost.
    // ======================================================================
    return moved;
}

// ---------------------------------------------------------------------------
//  Finding a script on another entity. See the note in ScriptComponent.h for
//  why Entity::Find<T> cannot answer this.
// ---------------------------------------------------------------------------

ScriptComponent* FindScriptNamedOn(Entity& entity, std::string_view scriptName) {
    if (scriptName.empty()) {
        return nullptr;
    }
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        Component* component = entity.ComponentAt(i);
        if (component == nullptr) {
            continue;
        }
        if (std::string_view(component->TypeName()) != ScriptComponent::kTypeName) {
            continue;
        }
        auto* script = static_cast<ScriptComponent*>(component);
        if (script->ScriptName() == scriptName) {
            return script;
        }
    }
    return nullptr;
}

ScriptComponent* FindScriptNamedIn(Scene& scene, std::string_view scriptName) {
    if (scriptName.empty()) {
        return nullptr;
    }

    ScriptComponent* found = nullptr;
    // ForEachUntil rather than ForEach: the one game-rules object in a scene is
    // usually near the front, and this is a call a script makes in OnStart -
    // walking the remaining four hundred entities after finding it is waste.
    scene.ForEachUntil([&](Entity& entity) {
        if (DeferredOps::IsPendingDestroy(entity.Id())) {
            return true;
        }
        if (ScriptComponent* script = FindScriptNamedOn(entity, scriptName);
            script != nullptr) {
            found = script;
            return false;
        }
        return true;
    });
    return found;
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

std::size_t ScriptSystem::RetargetRenamed(std::string_view oldName,
                                          std::string_view newName) {
    std::size_t moved = 0;
    // Walked directly rather than through a copy, unlike RebindRenamed: this
    // changes a string inside each component and cannot add or remove one, so
    // the list is not touched while it is being read.
    for (ScriptComponent* script : g_scripts) {
        moved += script->RetargetReferences(oldName, newName);
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
    ++g_resumePass;
    for (std::size_t i = 0; i < g_frameTicking.size(); ++i) {
        g_frameTicking[i]->FrameTick();
    }

    std::erase_if(g_frameTicking, [](const ScriptComponent* script) {
        return !script->NeedsFrameTick();
    });
}

void ScriptSystem::EndOfFrameUpdate() {
    // A PASS OF ITS OWN, counted separately from the update pass above so that
    // a coroutine resumed in one is not a candidate in the other. See
    // g_resumePass.
    ++g_resumePass;

    // THE SAME LIST, and that is the design rather than an economy. A component
    // stays in g_frameTicking for as long as it holds ANY coroutine, because
    // NeedsFrameTick asks - so a coroutine waiting for the end of the frame
    // keeps its own component walkable here, and there is no second registry to
    // fall out of step with this one.
    //
    // Walked by index with the size re-read, for the reason the loops above
    // are: a resumed coroutine may attach a script, which appends to this very
    // list, and a range-for would be reading it while it moved.
    for (std::size_t i = 0; i < g_frameTicking.size(); ++i) {
        g_frameTicking[i]->EndOfFrameTick();
    }

    // The same sweep, because this pass can be the one that finishes a
    // coroutine - and a component whose last one has just ended stops needing
    // to be walked from here on.
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
            static_cast<ScriptComponent*>(component)->DispatchCollision(
                message.type, message.other, message.i0);
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
