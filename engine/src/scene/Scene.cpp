// =============================================================================
//  Scene.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. Scene.h explains WHAT each
//  function is for and WHY it exists - read it first.
//
//  This is the biggest single piece of the engine, and it is worth doing in
//  the order the editor exercises it: CreateEntity and Get first, so the
//  Hierarchy panel shows something; then Load, so a .json file becomes a
//  world; then Save, which the Play button depends on.
// =============================================================================

#include <engine/scene/Scene.h>

namespace eng {
namespace {

// The one scene everything else talks to. Kept here rather than as a member so
// that Scene::Active() works before any scene has been built.
Scene* g_active = nullptr;

} // namespace

Scene::Scene() = default;

// TODO: unload before the slots go away, so every entity gets taken apart in
// the right order rather than being dropped when the vector is destroyed.
Scene::~Scene() {}

Scene* Scene::Active() { return g_active; }

void Scene::SetActive(Scene* scene) { g_active = scene; }

// ---------------------------------------------------------------------------
//  Entities
//
//  TODO: the slot-and-generation scheme described in Scene.h. Destroying an
//  entity bumps its slot's generation, which is what lets every EntityId still
//  referring to it be RECOGNISED as out of date instead of quietly pointing at
//  whatever moved into the slot afterwards.
// ---------------------------------------------------------------------------
EntityId Scene::CreateEntity(std::string_view /*name*/) { return EntityId{}; }

void Scene::DestroyEntityImmediate(EntityId /*id*/) {}

Entity* Scene::Get(EntityId /*id*/) { return nullptr; }

bool Scene::IsValid(EntityId /*id*/) const { return false; }

EntityId Scene::Find(std::string_view /*name*/) const { return EntityId{}; }

void Scene::ForEach(const std::function<void(Entity&)>& /*fn*/) {}

// ---------------------------------------------------------------------------
//  Loading and saving
//
//  TODO: a scene file is JSON - a name, a camera, and a list of entities, each
//  of which is a name and a list of components. Nothing about any particular
//  game belongs in this file; it only knows how to READ one.
// ---------------------------------------------------------------------------
EntityId Scene::CreateEntityFromJson(const Json& /*node*/,
                                     std::string_view /*nameOverride*/,
                                     std::string& outError) {
    outError = "Scene::CreateEntityFromJson is not implemented yet";
    return EntityId{};
}

void Scene::ResolveParents(const Json& /*entitiesArray*/) {}

bool Scene::BuildFromDocument(std::string& outError) {
    outError = "Scene::BuildFromDocument is not implemented yet";
    return false;
}

bool Scene::Load(std::string_view /*virtualPath*/, std::string& outError) {
    outError = "Scene::Load is not implemented yet - this is a shell of the engine";
    return false;
}

bool Scene::Save(std::string_view /*virtualPath*/, std::string& outError) {
    outError = "Scene::Save is not implemented yet - this is a shell of the engine";
    return false;
}

// SaveToString and LoadFromString are the two halves of the editor's Play
// button: pressing Play saves the scene to text, and Stop loads it back. They
// deliberately share the same code path as Save and Load, so the snapshot is
// exercised constantly instead of only when somebody saves a file.
bool Scene::SaveToString(std::string& /*outText*/, std::string& outError) {
    outError = "Scene::SaveToString is not implemented yet";
    return false;
}

bool Scene::LoadFromString(std::string_view /*text*/, std::string& outError) {
    outError = "Scene::LoadFromString is not implemented yet";
    return false;
}

void Scene::Unload() {}

// ---------------------------------------------------------------------------
//  Editing
// ---------------------------------------------------------------------------
std::string Scene::MakeUniqueName(std::string_view base) const {
    return std::string(base);
}

bool Scene::RenameEntity(EntityId /*id*/, std::string_view /*newName*/) { return false; }

EntityId Scene::DuplicateEntity(EntityId /*id*/, std::string& outError) {
    outError = "Scene::DuplicateEntity is not implemented yet";
    return EntityId{};
}

bool Scene::HasPrefab(std::string_view /*name*/) const { return false; }

EntityId Scene::InstantiatePrefab(std::string_view /*prefab*/, std::string_view /*name*/,
                                  std::string& outError) {
    outError = "Scene::InstantiatePrefab is not implemented yet";
    return EntityId{};
}

} // namespace eng
