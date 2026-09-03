// =============================================================================
//  Scene.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Scene.h is the specification; read it before filling one in.
//
//  This is the largest file in the engine. Written in the order the editor
//  exercises it - CreateEntity and Get first, then Load, then Save - each step
//  makes another part of the editor start working.
// =============================================================================

#include <engine/scene/Scene.h>

namespace eng {

// Builds an empty scene with no entities in it.
Scene::Scene() = default;

// Unloads everything before the slots go away, so each entity is taken apart in
// the right order rather than dropped when the list is destroyed.
Scene::~Scene() {
}

// The scene everything else talks to. Kept outside any particular scene so that
// asking works even before one has been built.
Scene* Scene::Active() {
    return nullptr;
}

// Sets which scene is the active one.
void Scene::SetActive(Scene* /*scene*/) {
}

// Creates an empty entity with a name and hands back its id. Destroying an
// entity bumps its slot's generation, which is what lets an old id be
// recognised as out of date rather than quietly pointing at whatever moved in.
EntityId Scene::CreateEntity(std::string_view /*name*/) {
    return EntityId{};
}

// Destroys an entity right now. Game code should use DeferredOps::QueueDestroy
// instead; this is what the queue eventually calls.
void Scene::DestroyEntityImmediate(EntityId /*id*/) {
}

// Looks an entity up by id, or nullptr when that id is out of date.
Entity* Scene::Get(EntityId /*id*/) {
    return nullptr;
}

// Does this id still refer to a living entity?
bool Scene::IsValid(EntityId /*id*/) const {
    return false;
}

// Finds an entity by name. Names are unique, which is what makes this possible.
EntityId Scene::Find(std::string_view /*name*/) const {
    return EntityId{};
}

// Visits every living entity in turn - what the Hierarchy panel is built on.
void Scene::ForEach(const std::function<void(Entity&)>& /*fn*/) {
}

// Builds one entity from a chunk of scene-file JSON: its name, then each
// component built by the factory and handed its own settings.
EntityId Scene::CreateEntityFromJson(const Json& /*node*/,
                                     std::string_view /*nameOverride*/,
                                     std::string& /*outError*/) {
    return EntityId{};
}

// Connects up the parent relationships, in a second pass. It has to be second,
// because an entity can name a parent that appears later in the file.
void Scene::ResolveParents(const Json& /*entitiesArray*/) {
}

// Turns an already-parsed scene document into entities. Shared by loading from
// a file and restoring a Play-mode snapshot, so the two cannot drift apart.
bool Scene::BuildFromDocument(std::string& /*outError*/) {
    return false;
}

// Reads a scene file and replaces everything in this scene with what is in it.
bool Scene::Load(std::string_view /*virtualPath*/, std::string& /*outError*/) {
    return false;
}

// Writes this scene out as a file, entity by entity and component by component.
bool Scene::Save(std::string_view /*virtualPath*/, std::string& /*outError*/) {
    return false;
}

// Writes the scene into a string instead of a file. This is what Play mode
// snapshots, which is why saving has to work before Play can be safe.
bool Scene::SaveToString(std::string& /*outText*/, std::string& /*outError*/) {
    return false;
}

// Rebuilds the scene from a string - what Stop uses to put the snapshot back.
bool Scene::LoadFromString(std::string_view /*text*/, std::string& /*outError*/) {
    return false;
}

// Destroys every entity and empties the scene.
void Scene::Unload() {
}

// Turns a wanted name into one nothing else is using, by adding a number.
std::string Scene::MakeUniqueName(std::string_view /*base*/) const {
    return {};
}

// Renames an entity, keeping the name table in step and refusing a name that is
// already taken.
bool Scene::RenameEntity(EntityId /*id*/, std::string_view /*newName*/) {
    return false;
}

// Copies an entity and everything on it, giving the copy a new unique name.
EntityId Scene::DuplicateEntity(EntityId /*id*/, std::string& /*outError*/) {
    return EntityId{};
}

} // namespace eng
