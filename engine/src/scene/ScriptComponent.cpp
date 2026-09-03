// =============================================================================
//  ScriptComponent.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. ScriptComponent.h is the specification; read it
//  first.
//
//  The clever part is already done for you, in ScriptHooks.h: working out which
//  lifecycle functions a script has is settled by the compiler. What is missing
//  here is the plumbing - a table of names, and the component that holds one.
// =============================================================================

#include <engine/scene/ScriptComponent.h>

namespace eng {

// The entity this script is attached to.
Entity* ScriptBehaviour::Owner() const {
    return nullptr;
}

// That entity's id, for addressing messages to it.
EntityId ScriptBehaviour::OwnerId() const {
    return EntityId{};
}

// The scene the entity lives in, for finding or creating others.
Scene* ScriptBehaviour::GetScene() const {
    return nullptr;
}

// The entity's transform. This is the accessor almost every script uses.
Transform2D* ScriptBehaviour::Transform() const {
    return nullptr;
}

// Lists a script's hooks as readable text - "OnStart, OnUpdate". The Console
// and the Inspector both show it, and it is the main defence against a
// misspelled hook, which is a function nobody calls rather than an error.
std::string DescribeHooks(const ScriptHooks& /*hooks*/) {
    return {};
}

// Records a script class under its name, with its hooks and the file it came
// from. Called automatically by ENGINE_REGISTER_SCRIPT when the compiled script
// library is loaded.
void ScriptRegistry::Register(std::string_view /*scriptName*/, CreateFn /*create*/,
                              const ScriptHooks& /*hooks*/,
                              std::string_view /*sourceFile*/) {
}

// Is there a script with this name in this build?
bool ScriptRegistry::IsRegistered(std::string_view /*scriptName*/) {
    return false;
}

// Everything known about one script class, or nullptr when the name is unknown.
const ScriptRegistry::Entry* ScriptRegistry::Find(std::string_view /*scriptName*/) {
    return nullptr;
}

// Builds one instance of a script by name.
std::unique_ptr<ScriptBehaviour> ScriptRegistry::Create(std::string_view /*scriptName*/) {
    return nullptr;
}

// Lists every known script name, which is what fills the Inspector's script
// picker.
void ScriptRegistry::ForEachScript(const std::function<void(const char*)>& /*fn*/) {
}

// The same list with the hooks and source file attached, for the Console.
void ScriptRegistry::ForEachEntry(
    const std::function<void(const char* name, const Entry& entry)>& /*fn*/) {
}

// How many script classes are registered.
std::size_t ScriptRegistry::Count() {
    return 0;
}

// Forgets every script. Called just before the compiled library is unloaded,
// because every entry points at a function inside it.
void ScriptRegistry::Clear() {
}

// Destroys the running behaviour, if there is one.
ScriptComponent::~ScriptComponent() {
}

// Reads which script this component should run, by name.
bool ScriptComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes the name back out, whether or not the script exists in this build - so
// a script that has not been written yet is not deleted from the scene file.
bool ScriptComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Binds the named script and joins the script system's list.
void ScriptComponent::OnAttach() {
}

// Gives the behaviour its OnDestroy while the entity is still whole, then
// unbinds and leaves the list.
void ScriptComponent::OnDetach() {
}

// Switches to a different script, tidying up the old one first.
void ScriptComponent::SetScriptName(std::string_view /*name*/) {
}

// Destroys the running behaviour but KEEPS the name, so the same script can be
// found again after the library is rebuilt.
void ScriptComponent::UnbindForReload() {
}

// Finds the newly compiled script under the same name. This is what lets you
// edit a script and carry on with the scene you were editing.
void ScriptComponent::RebindAfterReload() {
}

// Looks the name up in the registry, creates the behaviour, and copies its hook
// table. An unknown name is reported here, which is what shows a script as NOT
// FOUND in the Inspector.
void ScriptComponent::Bind() {
}

// Destroys the behaviour and forgets its hooks.
void ScriptComponent::Unbind() {
}

// Runs OnStart on the first step, then OnUpdate every step - and only if the
// script actually wrote those functions.
void ScriptComponent::Tick(float /*deltaSeconds*/) {
}

// Passes a collision message to the matching hook, if the script has one.
void ScriptComponent::DispatchCollision(const std::string& /*messageType*/,
                                        EntityId /*other*/) {
}

// Adds a script component to the system's list.
void ScriptSystem::Register(ScriptComponent& /*script*/) {
}

// Takes one back out.
void ScriptSystem::Unregister(ScriptComponent& /*script*/) {
}

// Empties the lists, used when a scene is unloaded.
void ScriptSystem::Clear() {
}

// How many script components are attached.
std::size_t ScriptSystem::Count() {
    return 0;
}

// How many of those actually need updating each step. Smaller than Count()
// whenever some scripts only handle collisions.
std::size_t ScriptSystem::TickingCount() {
    return 0;
}

// How many attached scripts name a class that is not in this build - the number
// the editor shows so "nothing happens" has an explanation.
std::size_t ScriptSystem::UnresolvedCount() {
    return 0;
}

// How many components are using one script name.
std::size_t ScriptSystem::CountUsing(std::string_view /*scriptName*/) {
    return 0;
}

// Points every component using the old name at the new one, and says how many
// moved. Used when a rebuild shows a class has been renamed.
std::size_t ScriptSystem::RebindRenamed(std::string_view /*oldName*/,
                                        std::string_view /*newName*/) {
    return 0;
}

// Destroys every running behaviour, before the library they live in is unloaded.
void ScriptSystem::UnbindAll() {
}

// Rebinds every component by name, after a new library is loaded.
void ScriptSystem::RebindAll() {
}

// Runs the scripts, once per fixed step, at the gameplay stage.
void ScriptSystem::Update(float /*deltaSeconds*/) {
}

// Subscribes once for ALL scripts, rather than once per component - a hundred
// scripted entities must not mean three hundred subscriptions for the message
// bus to walk on every collision.
void ScriptSystem::SubscribeToCollisions() {
}

// Tells the component factory that "ScriptComponent" means this class.
void ScriptSystem::RegisterComponentTypes() {
}

} // namespace eng
