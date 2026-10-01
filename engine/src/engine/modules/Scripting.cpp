// ============================================================================
//  Scripting.cpp - scripts attached to entities, and loading the library they live in.
//  See Scripting.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Files.h>
#include <engine/modules/Math.h>
#include <engine/modules/Messaging.h>
#include <engine/modules/Scene.h>
#include <engine/modules/Scripting.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <vector>

// ============================================================================
//  ScriptComponent.cpp - scripts. See ScriptComponent.h for why the connection
//  between a component and its behaviour is made by NAME, and why the
//  lifecycle hooks are found by the compiler rather than declared.
// ============================================================================



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


// ============================================================================
//  ScriptLibrary.cpp - loading and unloading a project's compiled scripts.
//  See ScriptLibrary.h for the order a reload has to happen in.
//
//  SDL is used to do the loading. SDL_LoadObject and SDL_UnloadObject are a
//  thin cross-platform wrapper over LoadLibrary on Windows and dlopen
//  everywhere else, so this file needs no #ifdef per operating system. SDL is
//  already a dependency, which makes it a much better choice here than writing
//  the platform code by hand.
// ============================================================================



namespace eng {
namespace {

// The handle SDL gives back for a loaded library. Null when nothing is loaded.
SDL_SharedObject* g_handle = nullptr;

std::string g_loadedPath;

} // namespace

std::string ScriptLibrary::DefaultVirtualPath() {
    // Every script in the project is compiled into this one library.
    //
    // It sits in .build/ beside assets/ rather than inside it. Your scripts
    // live in assets/, next to the scenes and images they belong with - but
    // the compiled result is not something you wrote and not something to
    // browse, and putting it in the tree the Assets panel shows would just be
    // clutter you have to learn to ignore.
    //
    // This is the ONE definition of the name, used by the engine that loads it
    // and by the editor that writes it, so there is never a question of which
    // file is the current one.
#if defined(_WIN32)
    return ".build/userContent.dll";
#else
    return ".build/userContent.so";
#endif
}

bool ScriptLibrary::Init(const BootConfig&) {
    // A project with no scripts is fine and returns true. Only a library that
    // exists and will not load is a failure.
    std::string error;
    return Load(DefaultVirtualPath(), error);
}

void ScriptLibrary::Shutdown() {
    Unload();
}

bool ScriptLibrary::Load(std::string_view virtualPath, std::string& outError) {
    // Step 1 to 3 of the order in the header: get rid of the old one first.
    Unload();

    if (!FileSystem::Exists(virtualPath)) {
        // Not an error. A project with no scripts written yet, or one that has
        // never been built, is a perfectly ordinary state to be in.
        outError.clear();
        ENGINE_LOG_INFO(Channels::kScene,
                        "no compiled scripts found at '{}' - write one in the Assets "
                        "panel and the editor will build it", virtualPath);
        return true;
    }

    const std::string realPath = FileSystem::Resolve(virtualPath);

    // Step 4: loading the library runs the constructors of its file-scope
    // objects, and those are what ENGINE_REGISTER_SCRIPT creates - so by the
    // time this call returns, every script inside has already added itself to
    // ScriptRegistry.
    g_handle = SDL_LoadObject(realPath.c_str());
    if (g_handle == nullptr) {
        outError = std::string("could not load '") + realPath + "': " + SDL_GetError();
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    g_loadedPath.assign(virtualPath);

    ENGINE_LOG_INFO(Channels::kScene, "loaded {} script(s) from '{}'",
                    ScriptRegistry::Count(), virtualPath);
    // Each script is listed WITH THE HOOKS IT TURNED OUT TO HAVE.
    //
    // Because hooks are found by name rather than declared, a misspelled
    // OnUpdate is not a compile error - it is a function nobody calls. This
    // line is what makes that visible: if your script is listed with hooks you
    // did not expect, the spelling is where to look. A script with none at all
    // is reported as a warning, because it is almost certainly a mistake.
    ScriptRegistry::ForEachEntry([](const char* name, const ScriptRegistry::Entry& e) {
        const std::string hooks = DescribeHooks(e.hooks);
        if (e.hooks.start == nullptr && e.hooks.update == nullptr &&
            e.hooks.destroy == nullptr && !e.hooks.AnyCollision()) {
            ENGINE_LOG_WARN(Channels::kScene, "    script '{}' has {}", name, hooks);
        } else {
            ENGINE_LOG_INFO(Channels::kScene, "    script '{}' - {}", name, hooks);
        }
    });

    // Step 5: anything in the scene that was waiting for a script by name can
    // now find it. A ScriptComponent that showed as NOT FOUND a moment ago
    // starts working here, with nothing reattached by hand.
    ScriptSystem::RebindAll();

    outError.clear();
    return true;
}

void ScriptLibrary::Unload() {
    if (g_handle == nullptr) {
        // Still worth clearing the registry: the scripts may have been
        // registered by a build that was linked in rather than loaded.
        ScriptSystem::UnbindAll();
        ScriptRegistry::Clear();
        return;
    }

    // Step 1: destroy every live script object. They were created by code
    // inside the library, so they must not outlive it.
    ScriptSystem::UnbindAll();

    // Step 2: empty the registry. Every entry in it is a pointer to a function
    // inside the library that is about to disappear.
    ScriptRegistry::Clear();

    // Step 3: and only now let go of the library itself.
    SDL_UnloadObject(g_handle);
    g_handle = nullptr;

    ENGINE_LOG_INFO(Channels::kScene, "unloaded the scripts from '{}'", g_loadedPath);
    g_loadedPath.clear();
}

bool ScriptLibrary::IsLoaded() { return g_handle != nullptr; }

const std::string& ScriptLibrary::LoadedPath() { return g_loadedPath; }

std::size_t ScriptLibrary::ScriptCount() { return ScriptRegistry::Count(); }

} // namespace eng
