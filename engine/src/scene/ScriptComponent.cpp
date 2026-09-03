// ============================================================================
//  ScriptComponent.cpp - scripts. See ScriptComponent.h for why the connection
//  between a component and its behaviour is made by NAME, and why the
//  lifecycle hooks are found by the compiler rather than declared.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
#include <map>
#include <vector>

namespace eng {
namespace {

// Every script component currently attached, so the system can find them all
// without walking the whole scene.
std::vector<ScriptComponent*> g_scripts;

// The subset that actually needs Tick() - scripts with an OnUpdate, plus any
// that have not had their OnStart yet.
//
// THIS LIST IS THE WHOLE POINT OF THE HOOK TABLE. A collision-only script sits
// in g_scripts so it can be counted, inspected and rebound, and is absent from
// here so it costs nothing at all sixty times a second.
std::vector<ScriptComponent*> g_ticking;

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

void AddToTicking(ScriptComponent* script) {
    if (script->NeedsTick() &&
        std::find(g_ticking.begin(), g_ticking.end(), script) == g_ticking.end()) {
        g_ticking.push_back(script);
    }
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
    if (hooks.destroy != nullptr)        { add("OnDestroy"); }
    if (hooks.collisionEnter != nullptr) { add("OnCollisionEnter"); }
    if (hooks.collisionStay != nullptr)  { add("OnCollisionStay"); }
    if (hooks.collisionExit != nullptr)  { add("OnCollisionExit"); }

    // Worth naming rather than printing an empty string. A script with no
    // hooks at all compiles, registers, attaches and does nothing - and the
    // overwhelmingly likely reason is a misspelled function name.
    if (out.empty()) {
        out = "NO HOOKS - check the spelling of OnStart / OnUpdate";
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
    return true;
}

bool ScriptComponent::Serialize(Json& out) const {
    // The name is saved WHETHER OR NOT it was found in this build. An
    // unresolved script is simply one that has not been compiled yet, and
    // leaving it out of the save would silently delete somebody's work the
    // first time they saved a scene from a build without their script in it.
    out["script"] = m_scriptName;
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
}

void ScriptComponent::Tick(float deltaSeconds) {
    if (m_behaviour == nullptr) {
        return;   // the script is not compiled into this build
    }

    // OnStart happens on the first TICK, not at attach. See ScriptComponent.h.
    //
    // m_started is set even when there is no OnStart to call, because it also
    // means "this script is live now" - which is what gates collisions, and
    // what tells the system it can stop ticking a script with no OnUpdate.
    if (!m_started) {
        m_started = true;
        if (m_hooks.start != nullptr) {
            m_hooks.start(m_behaviour.get());
        }
    }

    if (m_hooks.update != nullptr) {
        m_hooks.update(m_behaviour.get(), deltaSeconds);
    }
}

void ScriptComponent::DispatchCollision(const std::string& messageType, EntityId other) {
    // A collision arriving before the first tick would mean OnStart has not
    // run yet, and delivering OnCollisionEnter to a behaviour that has not
    // started is exactly the kind of surprise that makes scripting feel
    // unreliable. It is dropped instead; a CollisionStay will arrive next step
    // anyway, because "stay" repeats for as long as the two things overlap.
    if (m_behaviour == nullptr || !m_started || !m_hooks.AnyCollision()) {
        return;
    }

    if (messageType == MessageTypes::kCollisionEnter) {
        if (m_hooks.collisionEnter != nullptr) {
            m_hooks.collisionEnter(m_behaviour.get(), other);
        }
    } else if (messageType == MessageTypes::kCollisionStay) {
        if (m_hooks.collisionStay != nullptr) {
            m_hooks.collisionStay(m_behaviour.get(), other);
        }
    } else if (messageType == MessageTypes::kCollisionExit) {
        if (m_hooks.collisionExit != nullptr) {
            m_hooks.collisionExit(m_behaviour.get(), other);
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
    std::erase(g_ticking, &script);
}

void        ScriptSystem::Clear() { g_scripts.clear(); g_ticking.clear(); }
std::size_t ScriptSystem::Count() { return g_scripts.size(); }
std::size_t ScriptSystem::TickingCount() { return g_ticking.size(); }

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
    g_ticking.clear();
}

void ScriptSystem::RebindAll() {
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        script->RebindAfterReload();
        AddToTicking(script);
    }
}

void ScriptSystem::Update(float deltaSeconds) {
    // Walked by index with the size re-read each time. A script's OnUpdate is
    // allowed to attach another script, which appends to this very list. A
    // range-for would be reading the list while it changed underneath.
    for (std::size_t i = 0; i < g_ticking.size(); ++i) {
        g_ticking[i]->Tick(deltaSeconds);
    }

    // Drop anything that no longer needs ticking. This is where a script whose
    // only hook was OnStart leaves the list: it needed one tick to start, and
    // from the next step onwards it costs nothing.
    //
    // Done AFTER the walk rather than inside it, because removing entries from
    // a list while stepping through it by index skips whatever moves into the
    // gap.
    std::erase_if(g_ticking,
                  [](const ScriptComponent* script) { return !script->NeedsTick(); });
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
        if (auto* script = entity->Find<ScriptComponent>(); script != nullptr) {
            script->DispatchCollision(message.type, message.other);
        }
    };

    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionEnter, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionStay, forward);
    MessageBus::SubscribeBroadcast(MessageTypes::kCollisionExit, forward);
}

void ScriptSystem::RegisterComponentTypes() {
    ComponentFactory::Register(ScriptComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<ScriptComponent>();
                               });
}

} // namespace eng
