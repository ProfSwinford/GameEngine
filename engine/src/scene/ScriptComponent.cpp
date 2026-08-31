// ============================================================================
//  ScriptComponent.cpp - scripts. See ScriptComponent.h for why the connection
//  between a component and its behaviour is made by NAME.
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

// Every script component currently attached, so the system can tick them all
// without walking the whole scene.
std::vector<ScriptComponent*> g_scripts;

bool g_collisionsSubscribed = false;

// std::map rather than std::unordered_map so that listing the scripts comes
// out alphabetical without sorting - which is what the editor's "attach a
// script" list wants. There are tens of scripts, not thousands, so the speed
// difference does not matter.
//
// The std::less<> at the end is what allows looking a script up with a
// std::string_view without first copying it into a std::string.
using ScriptTable = std::map<std::string, ScriptRegistry::CreateFn, std::less<>>;

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
//  ScriptRegistry
// ---------------------------------------------------------------------------

void ScriptRegistry::Register(std::string_view scriptName, CreateFn create) {
    if (scriptName.empty() || create == nullptr) {
        return;
    }
    ScriptTable& table = Table();
    if (table.find(scriptName) != table.end()) {
        // Two files claiming the same name means one of them will never run,
        // and which one is decided by something nobody can see. Worth saying.
        ENGINE_LOG_WARN(Channels::kScene,
                        "two scripts are both called '{}' - only one of them can run",
                        scriptName);
    }
    table[std::string(scriptName)] = create;
}

bool ScriptRegistry::IsRegistered(std::string_view scriptName) {
    return Table().find(scriptName) != Table().end();
}

std::unique_ptr<ScriptBehaviour> ScriptRegistry::Create(std::string_view scriptName) {
    const auto it = Table().find(scriptName);
    return (it != Table().end()) ? it->second() : nullptr;
}

void ScriptRegistry::ForEachScript(const std::function<void(const char*)>& fn) {
    for (const auto& [name, create] : Table()) {
        fn(name.c_str());
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
    if (m_behaviour != nullptr && m_started) {
        m_behaviour->OnDestroy();
    }
    ScriptSystem::Unregister(*this);
    Unbind();
}

void ScriptComponent::SetScriptName(std::string_view name) {
    if (m_scriptName == name) {
        return;
    }
    if (m_behaviour != nullptr && m_started) {
        m_behaviour->OnDestroy();
    }
    Unbind();
    m_scriptName = std::string(name);
    m_started    = false;
    Bind();
}

void ScriptComponent::UnbindForReload() {
    // The behaviour object is about to stop existing along with the library
    // that defined it, so it gets its OnDestroy exactly as it would if the
    // entity were being deleted.
    if (m_behaviour != nullptr && m_started) {
        m_behaviour->OnDestroy();
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
    m_behaviour = ScriptRegistry::Create(m_scriptName);
    if (m_behaviour != nullptr) {
        // Set before any hook can run, so Owner() and Transform() already work
        // inside OnStart.
        m_behaviour->m_component = this;
        return;
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
}

void ScriptComponent::Tick(float deltaSeconds) {
    if (m_behaviour == nullptr) {
        return;   // the script is not compiled into this build
    }
    // OnStart happens on the first TICK, not at attach. See ScriptComponent.h.
    if (!m_started) {
        m_started = true;
        m_behaviour->OnStart();
    }
    m_behaviour->OnUpdate(deltaSeconds);
}

void ScriptComponent::DispatchCollision(const std::string& messageType, EntityId other) {
    // A collision arriving before the first tick would mean OnStart has not
    // run yet, and delivering OnCollisionEnter to a behaviour that has not
    // started is exactly the kind of surprise that makes scripting feel
    // unreliable. It is dropped instead; a CollisionStay will arrive next step
    // anyway, because "stay" repeats for as long as the two things overlap.
    if (m_behaviour == nullptr || !m_started) {
        return;
    }
    if (messageType == MessageTypes::kCollisionEnter) {
        m_behaviour->OnCollisionEnter(other);
    } else if (messageType == MessageTypes::kCollisionStay) {
        m_behaviour->OnCollisionStay(other);
    } else if (messageType == MessageTypes::kCollisionExit) {
        m_behaviour->OnCollisionExit(other);
    }
}

// ---------------------------------------------------------------------------
//  ScriptSystem
// ---------------------------------------------------------------------------

void ScriptSystem::Register(ScriptComponent& script) {
    if (std::find(g_scripts.begin(), g_scripts.end(), &script) == g_scripts.end()) {
        g_scripts.push_back(&script);
    }
}

void ScriptSystem::Unregister(ScriptComponent& script) {
    std::erase(g_scripts, &script);
}

void        ScriptSystem::Clear() { g_scripts.clear(); }
std::size_t ScriptSystem::Count() { return g_scripts.size(); }

std::size_t ScriptSystem::UnresolvedCount() {
    std::size_t count = 0;
    for (const ScriptComponent* script : g_scripts) {
        if (!script->IsResolved()) {
            ++count;
        }
    }
    return count;
}

void ScriptSystem::UnbindAll() {
    // A copy of the list is walked, because unbinding does not remove anything
    // from g_scripts - but being careful here costs nothing and the rule
    // "never modify a list you are walking" is worth applying consistently.
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        script->UnbindForReload();
    }
}

void ScriptSystem::RebindAll() {
    for (ScriptComponent* script : std::vector<ScriptComponent*>(g_scripts)) {
        script->RebindAfterReload();
    }
}

void ScriptSystem::Update(float deltaSeconds) {
    // Walked by index with the size re-read each time. A script's OnUpdate is
    // allowed to attach another script, or to destroy its own entity - which
    // removes an entry from this very list. A range-for would be reading the
    // list while it changed underneath.
    for (std::size_t i = 0; i < g_scripts.size(); ++i) {
        g_scripts[i]->Tick(deltaSeconds);
    }
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
