// =============================================================================
//  ScriptComponent.cpp - A SHELL. The declarations are real; the bodies are
//  yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. ScriptComponent.h explains WHAT
//  each function is for and WHY it exists - read it first.
//
//  NOTE WHAT IS ALREADY DONE FOR YOU, in the header: the hook detection. The
//  concepts, ScriptHooks and MakeScriptHooks are templates, so they live in
//  ScriptComponent.h and already work. What is missing here is the plumbing -
//  the table of names, and the component that holds one script.
// =============================================================================

#include <engine/core/Log.h>
#include <engine/scene/ScriptComponent.h>

namespace eng {

// ---------------------------------------------------------------------------
//  ScriptBehaviour - the accessors a script uses
//
//  TODO: each of these asks the owning component. They are the reason a script
//  can say Transform()->Translate(...) without being handed anything.
// ---------------------------------------------------------------------------
Entity* ScriptBehaviour::Owner() const { return nullptr; }

EntityId ScriptBehaviour::OwnerId() const { return EntityId{}; }

Scene* ScriptBehaviour::GetScene() const { return nullptr; }

Transform2D* ScriptBehaviour::Transform() const { return nullptr; }

// ---------------------------------------------------------------------------
//  DescribeHooks - given, because it is formatting rather than a lesson.
//
//  It is what the Console and the Inspector use to say which hooks a script
//  turned out to have, which is the main defence against a misspelled one.
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

    if (out.empty()) {
        out = "NO HOOKS - check the spelling of OnStart / OnUpdate";
    }
    return out;
}

// ---------------------------------------------------------------------------
//  ScriptRegistry
//
//  TODO: a table of script name -> how to make one, plus which hooks it has
//  and which file it came from. ENGINE_REGISTER_SCRIPT fills this in when the
//  compiled script library is loaded.
//
//  Clear() is the one with a trap in it: every entry points INTO the loaded
//  library, so the table has to be emptied before that library is unloaded.
// ---------------------------------------------------------------------------
void ScriptRegistry::Register(std::string_view /*scriptName*/, CreateFn /*create*/,
                              const ScriptHooks& /*hooks*/,
                              std::string_view /*sourceFile*/) {}

bool ScriptRegistry::IsRegistered(std::string_view /*scriptName*/) { return false; }

const ScriptRegistry::Entry* ScriptRegistry::Find(std::string_view /*scriptName*/) {
    return nullptr;
}

std::unique_ptr<ScriptBehaviour> ScriptRegistry::Create(std::string_view /*scriptName*/) {
    return nullptr;
}

void ScriptRegistry::ForEachScript(const std::function<void(const char*)>& /*fn*/) {}

void ScriptRegistry::ForEachEntry(
    const std::function<void(const char* name, const Entry& entry)>& /*fn*/) {}

std::size_t ScriptRegistry::Count() { return 0; }

void ScriptRegistry::Clear() {}

// ---------------------------------------------------------------------------
//  ScriptComponent - the component that holds one script, BY NAME
// ---------------------------------------------------------------------------
ScriptComponent::~ScriptComponent() {}

bool ScriptComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

// The name is saved whether or not the script was found, so a script that has
// not been written yet is not silently deleted from the scene file.
bool ScriptComponent::Serialize(Json& out) const {
    out["script"] = m_scriptName;
    return true;
}

void ScriptComponent::OnAttach() {}
void ScriptComponent::OnDetach() {}

void ScriptComponent::SetScriptName(std::string_view name) {
    m_scriptName = std::string(name);
}

void ScriptComponent::UnbindForReload()   {}
void ScriptComponent::RebindAfterReload() {}

// TODO: look the name up in the registry, create the behaviour, and copy its
// hook table. Bind is also where an unknown name gets reported, which is what
// shows a script as NOT FOUND in the Inspector.
void ScriptComponent::Bind()   {}
void ScriptComponent::Unbind() {}

// TODO: OnStart on the first tick, then OnUpdate every tick - and only if the
// script actually has those hooks.
void ScriptComponent::Tick(float /*deltaSeconds*/) {}

void ScriptComponent::DispatchCollision(const std::string& /*messageType*/,
                                        EntityId /*other*/) {}

// ---------------------------------------------------------------------------
//  ScriptSystem - stage 200 (Gameplay)
// ---------------------------------------------------------------------------
void ScriptSystem::Register(ScriptComponent& /*script*/)   {}
void ScriptSystem::Unregister(ScriptComponent& /*script*/) {}
void ScriptSystem::Clear()                                 {}

std::size_t ScriptSystem::Count()           { return 0; }
std::size_t ScriptSystem::TickingCount()    { return 0; }
std::size_t ScriptSystem::UnresolvedCount() { return 0; }

std::size_t ScriptSystem::CountUsing(std::string_view /*scriptName*/) { return 0; }

std::size_t ScriptSystem::RebindRenamed(std::string_view /*oldName*/,
                                        std::string_view /*newName*/) {
    return 0;
}

void ScriptSystem::UnbindAll() {}
void ScriptSystem::RebindAll() {}

// TODO: tick every script that needs it. Keeping a SEPARATE list of the ones
// with an OnUpdate is the whole payoff of the hook table - a collision-only
// script should cost nothing here.
void ScriptSystem::Update(float /*deltaSeconds*/) {}

// TODO: one subscription per message type for ALL scripts, not one per
// component - a hundred scripted entities must not mean three hundred
// subscriptions for the bus to walk on every collision.
void ScriptSystem::SubscribeToCollisions() {}

void ScriptSystem::RegisterComponentTypes() {}

} // namespace eng
