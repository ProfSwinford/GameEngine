// SCRIPTS. See ScriptComponent.h for why the binding is by name.

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

// Dense array of the components to tick, same shape as SpinSystem's.
std::vector<ScriptComponent*> g_scripts;

// Registered once at boot, not once per component - see the note on
// SubscribeToCollisions.
bool g_collisionsSubscribed = false;

// std::map rather than unordered_map so ForEachScript comes out alphabetical,
// which is what the editor's "attach a script" list wants. There are tens of
// scripts, not thousands; the lookup difference is not measurable and the
// stable order is worth having.
using ScriptTable = std::map<std::string, ScriptRegistry::CreateFn, std::less<>>;

ScriptTable& Table() {
    // FUNCTION-LOCAL STATIC, and this is the detail that makes
    // ENGINE_REGISTER_SCRIPT safe. Script registrars are file-scope objects
    // whose construction order across translation units is unspecified; a
    // namespace-scope table could be constructed after the first registrar
    // that fills it. A function-local static is constructed on first use, so
    // whichever registrar runs first builds the table.
    //
    // That is the static initialization order fiasco, avoided rather than
    // survived. ComponentFactory sidesteps it a different way - explicit
    // RegisterBuiltins() at boot - because it can; scripts cannot, since the
    // whole point is that the engine does not know their names.
    static ScriptTable table;
    return table;
}

} // namespace

// ---------------------------------------------------------------------------
//  ScriptBehaviour
// ---------------------------------------------------------------------------
Entity* ScriptBehaviour::Owner() const {
    return m_component != nullptr ? m_component->Owner() : nullptr;
}

EntityHandle ScriptBehaviour::OwnerHandle() const {
    return m_component != nullptr ? m_component->OwnerHandle() : EntityHandle{};
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
    // Last registration wins, and a duplicate is worth a warning: two files
    // claiming one name means one of them will never run, and which one is a
    // property of the link order.
    ScriptTable& table = Table();
    const auto   it    = table.find(scriptName);
    if (it != table.end()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "two scripts are registered as '{}' - only one can win, and which "
                        "one is decided by link order",
                        scriptName);
    }
    table[std::string(scriptName)] = create;
}

bool ScriptRegistry::IsRegistered(std::string_view scriptName) {
    return Table().find(scriptName) != Table().end();
}

std::unique_ptr<ScriptBehaviour> ScriptRegistry::Create(std::string_view scriptName) {
    const auto it = Table().find(scriptName);
    return it != Table().end() ? it->second() : nullptr;
}

void ScriptRegistry::ForEachScript(const std::function<void(const char*)>& fn) {
    for (const auto& [name, create] : Table()) {
        fn(name.c_str());
    }
}

usize ScriptRegistry::Count() { return Table().size(); }

// ---------------------------------------------------------------------------
//  ScriptComponent
// ---------------------------------------------------------------------------
StringId ScriptComponent::TypeIdStatic() {
    static const StringId id = Intern(kTypeName);
    return id;
}

ScriptComponent::~ScriptComponent() {
    // Safety net for a component destroyed without ever being attached, which
    // happens when Deserialize fails during a scene load. OnDetach is the
    // mechanism; this is the backstop. Same pattern as SpinComponent.
    ScriptSystem::Unregister(*this);
    Unbind();
}

bool ScriptComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    const ConfigNode script = node.Child("script");
    if (!script.IsValid()) {
        outError = node.Path() + " needs a 'script' naming the behaviour to run";
        return false;
    }

    m_scriptName = script.AsString("");
    if (m_scriptName.empty()) {
        outError = node.Path() + ".script is empty";
        return false;
    }
    return true;
}

bool ScriptComponent::Serialize(ConfigWriter& out) const {
    // THE NAME IS WRITTEN WHETHER OR NOT IT RESOLVED. An unresolved script is
    // a script that has not been compiled yet, and dropping it from the save
    // would silently delete the author's work the first time they saved a
    // scene from a build that did not have their script in it.
    out.SetString("script", m_scriptName);
    return true;
}

void ScriptComponent::OnAttach() {
    Bind();
    ScriptSystem::Register(*this);
}

void ScriptComponent::OnDetach() {
    // OnDestroy runs BEFORE the entity is torn down, so a behaviour can still
    // reach its transform and its siblings - which is the whole reason the
    // hook exists rather than leaving it to the destructor.
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

void ScriptComponent::Bind() {
    m_behaviour = ScriptRegistry::Create(m_scriptName);
    if (m_behaviour != nullptr) {
        // Set before any hook can run, so Owner() and Transform() are valid
        // inside OnStart.
        m_behaviour->m_component = this;
        return;
    }

    if (!m_scriptName.empty()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "script '{}' is not compiled into this build, so it is attached "
                        "but will not run ({} script(s) available)",
                        m_scriptName, ScriptRegistry::Count());
    }
}

void ScriptComponent::Unbind() {
    if (m_behaviour != nullptr) {
        m_behaviour->m_component = nullptr;
        m_behaviour.reset();
    }
}

void ScriptComponent::Tick(f32 deltaSeconds) {
    if (m_behaviour == nullptr) {
        return;
    }
    // OnStart on the first TICK rather than at attach. At attach time a scene
    // load may not have built the rest of the entity yet - components are
    // attached one at a time - so a script looking for its sibling collider in
    // OnAttach would find it only if the file listed the collider first.
    if (!m_started) {
        m_started = true;
        m_behaviour->OnStart();
    }
    m_behaviour->OnUpdate(deltaSeconds);
}

void ScriptComponent::DispatchCollision(StringId messageType, EntityHandle other) {
    // A collision arriving before the first tick means OnStart has not run.
    // Delivering OnCollisionEnter to a behaviour that has not started yet is
    // exactly the kind of surprise that makes scripting feel unreliable, so
    // the event is dropped rather than delivered out of order - it will fire
    // again next step, because collision STAY repeats while overlapping.
    if (m_behaviour == nullptr || !m_started) {
        return;
    }
    if (messageType == MessageTypes::CollisionEnter()) {
        m_behaviour->OnCollisionEnter(other);
    } else if (messageType == MessageTypes::CollisionStay()) {
        m_behaviour->OnCollisionStay(other);
    } else if (messageType == MessageTypes::CollisionExit()) {
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
    const auto it = std::find(g_scripts.begin(), g_scripts.end(), &script);
    if (it != g_scripts.end()) {
        g_scripts.erase(it);
    }
}

void ScriptSystem::Clear() { g_scripts.clear(); }

usize ScriptSystem::Count() { return g_scripts.size(); }

usize ScriptSystem::UnresolvedCount() {
    usize count = 0;
    for (const ScriptComponent* script : g_scripts) {
        if (!script->IsResolved()) {
            ++count;
        }
    }
    return count;
}

void ScriptSystem::Update(f32 deltaSeconds) {
    // ITERATED BY INDEX OVER A SNAPSHOT OF THE SIZE, because a script's
    // OnUpdate can attach another script - or destroy its own entity, which
    // detaches and erases from this very vector. Range-for over g_scripts
    // would invalidate its iterator on the first such call.
    //
    // Entries removed mid-loop shift the tail down, so an index can skip one
    // script for one step. That is the trade against a copy of the vector
    // every step, and a script missing one tick on the frame something was
    // destroyed is not observable; the allocation would be.
    for (usize i = 0; i < g_scripts.size(); ++i) {
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
    // hundred subscriptions the bus has to walk for every collision.
    const auto forward = [](const Message& message) {
        Scene* scene = Scene::Active();
        if (scene == nullptr) {
            return;
        }
        Entity* entity = scene->Get(message.target);
        if (entity == nullptr) {
            return;   // destroyed between the collision and the dispatch
        }
        if (auto* script = entity->Find<ScriptComponent>(); script != nullptr) {
            script->DispatchCollision(message.type, message.other);
        }
    };

    MessageBus::SubscribeBroadcast(MessageTypes::CollisionEnter(), forward);
    MessageBus::SubscribeBroadcast(MessageTypes::CollisionStay(), forward);
    MessageBus::SubscribeBroadcast(MessageTypes::CollisionExit(), forward);
}

void ScriptSystem::RegisterComponentTypes() {
    ComponentFactory::Register(ScriptComponent::kTypeName, []() -> std::unique_ptr<Component> {
        return std::make_unique<ScriptComponent>();
    });
}

} // namespace eng
