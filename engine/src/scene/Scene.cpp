// ============================================================================
//  Scene.cpp - loading, saving and holding a level. See Scene.h.
//
//  Notice what is NOT in this file: no entity name, no position, no texture
//  filename. Every one of those lives in a .json file. That is what makes it
//  possible to build a level without writing any C++.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/render/Gizmos.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Component.h>
#include <engine/scene/Scene.h>

#include <unordered_map>

namespace eng {
namespace {

// The scene the engine is currently running. There is only ever one.
Scene* g_active = nullptr;

// Returns the named part of a Json object, or an empty object when it is not
// there. Used instead of `document["entities"]` because square brackets INSERT
// a missing key, which would modify the document just by reading it.
const Json& Field(const Json& object, const char* name) {
    static const Json kEmpty = Json::object();
    if (!object.is_object()) {
        return kEmpty;
    }
    const auto it = object.find(name);
    return (it != object.end()) ? *it : kEmpty;
}

} // namespace

Scene::Scene() = default;

Scene::~Scene() {
    Unload();
    if (g_active == this) {
        g_active = nullptr;
    }
}

Scene* Scene::Active()                { return g_active; }
void   Scene::SetActive(Scene* scene) { g_active = scene; }

// ---------------------------------------------------------------------------
//  Creating and destroying entities
// ---------------------------------------------------------------------------

EntityId Scene::CreateEntity(std::string_view name) {
    int index = 0;

    if (!m_freeIndices.empty()) {
        // Reuse a slot from something that was destroyed earlier.
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        index = static_cast<int>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot& slot    = m_slots[static_cast<std::size_t>(index)];
    slot.entity   = std::make_unique<Entity>();
    slot.occupied = true;

    slot.entity->m_id    = EntityId{index, slot.generation};
    slot.entity->m_scene = this;
    slot.entity->m_alive = true;
    slot.entity->SetName(name);

    m_byName[std::string(name)] = slot.entity->m_id;
    ++m_liveCount;

    return slot.entity->m_id;
}

void Scene::DestroyEntityImmediate(EntityId id) {
    if (!IsValid(id)) {
        // Destroying something twice is harmless and silent. Game code does it
        // constantly - two bullets hitting the same enemy on the same tick -
        // and treating it as an error would fill the Console during perfectly
        // ordinary play.
        return;
    }

    Slot& slot = m_slots[static_cast<std::size_t>(id.index)];

    if (slot.entity != nullptr) {
        m_byName.erase(slot.entity->Name());
        slot.entity->DestroyInternal();
    }
    slot.entity.reset();
    slot.occupied = false;

    // The generation goes up LAST. From this moment every EntityId still
    // referring to this slot can be recognised as out of date, which is what
    // lets the editor keep a selection across a delete without crashing.
    ++slot.generation;

    m_freeIndices.push_back(id.index);
    if (m_liveCount > 0) {
        --m_liveCount;
    }
}

Entity* Scene::Get(EntityId id) {
    if (!IsValid(id)) {
        return nullptr;
    }
    return m_slots[static_cast<std::size_t>(id.index)].entity.get();
}

bool Scene::IsValid(EntityId id) const {
    if (id.IsNull() || id.index >= static_cast<int>(m_slots.size())) {
        return false;
    }
    const Slot& slot = m_slots[static_cast<std::size_t>(id.index)];

    // Both halves are checked: the slot has to be occupied AND still hold the
    // same occupant it did when the id was made.
    return slot.occupied && slot.generation == id.generation;
}

EntityId Scene::Find(std::string_view name) const {
    const auto it = m_byName.find(std::string(name));
    return (it != m_byName.end()) ? it->second : EntityId{};
}

void Scene::ForEach(const std::function<void(Entity&)>& fn) {
    // Looped by index, re-reading the size each time: the callback is allowed
    // to create an entity, and adding to m_slots can move the whole list
    // elsewhere in memory.
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        Slot& slot = m_slots[i];
        if (slot.occupied && slot.entity != nullptr) {
            fn(*slot.entity);
        }
    }
}

// ---------------------------------------------------------------------------
//  Building entities from JSON
// ---------------------------------------------------------------------------

EntityId Scene::CreateEntityFromJson(const Json& node, std::string_view nameOverride,
                                     std::string& outError) {
    std::string name(nameOverride);
    if (name.empty()) {
        name = ReadString(node, "name", "");
    }
    if (name.empty()) {
        outError = "an entity in this scene has no \"name\"";
        return EntityId{};
    }

    const EntityId id     = CreateEntity(name);
    Entity*        entity = Get(id);
    if (entity == nullptr) {
        outError = "could not create an entity called '" + name + "'";
        return EntityId{};
    }

    const Json& components = Field(node, "components");
    if (components.is_null() || !components.is_array()) {
        return id;   // an entity with only a transform is perfectly legal
    }

    for (const Json& componentNode : components) {
        const std::string typeName = ReadString(componentNode, "type", "");
        if (typeName.empty()) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "'{}': a component entry has no \"type\"", name);
            continue;
        }

        // ==================================================================
        //  DESERIALIZE FIRST, THEN ATTACH. The order matters.
        //
        //  OnAttach is where a component hooks itself up and loads what it
        //  needs - SpriteComponent::OnAttach loads the image named in
        //  m_texturePath. Attaching first and filling in the fields afterwards
        //  would have it load an EMPTY path, and the level would render
        //  nothing at all with no obvious explanation.
        // ==================================================================
        std::unique_ptr<Component> component = ComponentFactory::Create(typeName);
        if (component == nullptr) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "'{}': there is no component type called '{}'", name,
                             typeName);
            continue;
        }

        std::string componentError;
        if (!component->Deserialize(componentNode, componentError)) {
            // Reported naming the entity and the component, then attached
            // anyway. A sprite with a bad tint is still a sprite, and throwing
            // the whole component away would turn one typo into an invisible
            // entity.
            ENGINE_LOG_ERROR(Channels::kScene, "'{}' / {}: {}", name, typeName,
                             componentError);
        }

        entity->AddComponent(std::move(component));
    }

    return id;
}

void Scene::ResolveParents(const Json& entitiesArray) {
    // A SECOND pass over the file, because a child may name a parent that
    // appears further down. Doing it in one pass would make the order of the
    // file matter, and "my entity disappeared because I moved it three lines
    // up" is a bad afternoon.
    for (const Json& node : entitiesArray) {
        const std::string childName  = ReadString(node, "name", "");
        const std::string parentName = ReadString(node, "parent", "");
        if (childName.empty() || parentName.empty()) {
            continue;
        }

        Entity* child  = Get(Find(childName));
        Entity* parent = Get(Find(parentName));
        if (child == nullptr) {
            continue;
        }
        if (parent == nullptr) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "'{}' says its parent is '{}', but there is no entity with "
                             "that name in this scene", childName, parentName);
            continue;
        }
        child->Transform().SetParent(&parent->Transform());
    }
}

bool Scene::BuildFromDocument(std::string& outError) {
    m_name = ReadString(m_document, "name", "<unnamed>");

    const Json& camera = Field(m_document, "camera");
    if (camera.is_object()) {
        m_cameraPosition = ReadVec2(camera, "position", Vec2{0.0f, 0.0f}, "camera");
        m_cameraZoom     = ReadFloat(camera, "zoom", 1.0f, "camera");
    }

    const Json& entities = Field(m_document, "entities");
    if (!entities.is_array()) {
        outError = "this scene file has no \"entities\" list";
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    for (const Json& node : entities) {
        std::string entityError;
        if (CreateEntityFromJson(node, {}, entityError).IsNull()) {
            ENGINE_LOG_ERROR(Channels::kScene, "{}", entityError);
        }
    }

    ResolveParents(entities);

    SetActive(this);
    outError.clear();
    return true;
}

bool Scene::Load(std::string_view virtualPath, std::string& outError) {
    Unload();

    std::string text;
    if (!FileSystem::ReadTextFile(virtualPath, text, outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    m_document = ParseJson(text, outError);
    if (!outError.empty()) {
        ENGINE_LOG_ERROR(Channels::kScene, "{}: {}", virtualPath, outError);
        return false;
    }

    m_sourcePath.assign(virtualPath);

    if (!BuildFromDocument(outError)) {
        return false;
    }

    ENGINE_LOG_INFO(Channels::kScene, "scene '{}' loaded from {}: {} entities, {} image(s)",
                    m_name, virtualPath, m_liveCount, ResourceManager::LoadedCount());
    return true;
}

// ---------------------------------------------------------------------------
//  Saving
// ---------------------------------------------------------------------------

namespace {

// Builds the JSON for the whole scene. Kept as a free function in this file
// rather than a member, because it is only ever used here.
//
// `existing` is the document the scene was loaded from, so that any key this
// build does not understand survives being saved. Only "name", "camera" and
// "entities" are regenerated. A tool that silently drops the parts of a file
// it did not understand is a tool people stop trusting with their files.
Json BuildSceneDocument(Scene& scene, const std::string& sceneName, Vec2 cameraPosition,
                        float cameraZoom, std::size_t& outSkipped, const Json& existing) {
    Json root = existing.is_object() ? existing : Json::object();

    root["name"] = sceneName.empty() ? std::string("Untitled") : sceneName;
    root["camera"]["position"] = Json::array({cameraPosition.x, cameraPosition.y});
    root["camera"]["zoom"]     = cameraZoom;

    // Parent/child relationships are read from the LIVE transform tree, not
    // from whatever the original file said, so an entity reparented in the
    // editor saves correctly. This first pass builds a table from each
    // transform to its entity's name so the second pass can look parents up.
    std::unordered_map<const Transform2D*, std::string> transformNames;
    scene.ForEach([&](Entity& entity) {
        transformNames[&entity.Transform()] = entity.Name();
    });

    outSkipped = 0;
    Json entities = Json::array();

    scene.ForEach([&](Entity& entity) {
        Json entityJson    = Json::object();
        entityJson["name"] = entity.Name();

        if (const Transform2D* parent = entity.Transform().Parent(); parent != nullptr) {
            if (const auto it = transformNames.find(parent); it != transformNames.end()) {
                entityJson["parent"] = it->second;
            }
        }

        Json components = Json::array();
        entity.ForEachComponent([&](Component& component) {
            Json componentJson = Json::object();
            if (!component.Serialize(componentJson)) {
                // Reported, not silently dropped. A save that loses a
                // component without saying so is worse than one that refuses.
                ENGINE_LOG_WARN(Channels::kScene,
                                "saving '{}': the component '{}' cannot be saved and was "
                                "left out of the file",
                                entity.Name(), component.TypeName());
                ++outSkipped;
                return;
            }
            // The "type" key is written HERE rather than by each component, so
            // no component can get its own name wrong.
            componentJson["type"] = component.TypeName();
            components.push_back(std::move(componentJson));
        });

        entityJson["components"] = std::move(components);
        entities.push_back(std::move(entityJson));
    });

    root["entities"] = std::move(entities);
    return root;
}

} // namespace

bool Scene::Save(std::string_view virtualPath, std::string& outError) {
    if (virtualPath.empty()) {
        outError = "no file to save to";
        return false;
    }

    std::size_t skipped = 0;
    const Json  root = BuildSceneDocument(*this, m_name, m_cameraPosition, m_cameraZoom,
                                          skipped, m_document);

    // dump(2) turns the document back into text, indented by two spaces so a
    // person can read it and a version-control diff makes sense.
    const std::string text = root.dump(2);
    if (!FileSystem::WriteTextFile(virtualPath, text, outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "could not save '{}': {}", virtualPath,
                         outError);
        return false;
    }

    // "Save As" retargets the scene, so a later Ctrl+S goes to the new file
    // rather than back to the one it was opened from.
    m_sourcePath.assign(virtualPath);

    if (skipped > 0) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "saved '{}', but {} component(s) were left out - the file is not "
                        "a complete record of the scene", virtualPath, skipped);
    } else {
        ENGINE_LOG_INFO(Channels::kScene, "saved '{}': {} entities", virtualPath,
                        m_liveCount);
    }
    outError.clear();
    return true;
}

bool Scene::SaveToString(std::string& outText, std::string& outError) {
    std::size_t skipped = 0;
    const Json  root = BuildSceneDocument(*this, m_name, m_cameraPosition, m_cameraZoom,
                                          skipped, m_document);

    // A skipped component makes this FAIL, where Save() only warns, and the
    // difference is deliberate. A saved file with a component missing is
    // something you can open and fix. A Play-mode snapshot with a component
    // missing would silently DELETE that component when Stop restores the
    // scene - so the editor refuses to enter Play mode rather than promise a
    // restore it cannot deliver.
    if (skipped > 0) {
        outError = "this scene contains " + std::to_string(skipped) +
                   " component(s) that cannot be saved, so pressing Stop would not put "
                   "the scene back the way it was";
        return false;
    }

    outText = root.dump();
    outError.clear();
    return true;
}

bool Scene::LoadFromString(std::string_view text, std::string& outError) {
    Unload();

    // m_sourcePath is deliberately left alone. Restoring a Play-mode snapshot
    // must not make the scene forget which file it came from, or Ctrl+S after
    // pressing Stop would have nowhere to go.
    m_document = ParseJson(text, outError);
    if (!outError.empty()) {
        ENGINE_LOG_ERROR(Channels::kScene, "could not restore the scene: {}", outError);
        return false;
    }

    return BuildFromDocument(outError);
}

void Scene::Unload() {
    if (m_slots.empty()) {
        return;
    }

    // Destroying every entity detaches every component, which lets go of every
    // texture. Anything no longer used unloads itself at that point.
    for (Slot& slot : m_slots) {
        if (slot.occupied && slot.entity != nullptr) {
            slot.entity->DestroyInternal();
            slot.entity.reset();
            slot.occupied = false;
        }
    }

    m_slots.clear();
    m_freeIndices.clear();
    m_byName.clear();
    m_liveCount = 0;

    // Otherwise a three-second gizmo would outlive the entity it was marking
    // and hang in the air pointing at nothing.
    Gizmos::Clear();

    ResourceManager::PruneCache();
    ENGINE_LOG_INFO(Channels::kScene, "scene unloaded; {} image(s) still loaded",
                    ResourceManager::LoadedCount());

    m_name.clear();

    // m_sourcePath is NOT cleared. It means "the file this scene came from",
    // and that is still true after an unload - it is the only way back.
}

// ---------------------------------------------------------------------------
//  Editing
// ---------------------------------------------------------------------------

std::string Scene::MakeUniqueName(std::string_view base) const {
    std::string candidate(base);
    if (candidate.empty()) {
        candidate = "Entity";
    }
    if (Find(candidate).IsNull()) {
        return candidate;
    }
    for (int suffix = 1; suffix < 100000; ++suffix) {
        std::string attempt = std::string(base) + "_" + std::to_string(suffix);
        if (Find(attempt).IsNull()) {
            return attempt;
        }
    }
    return candidate;
}

bool Scene::RenameEntity(EntityId id, std::string_view newName) {
    Entity* entity = Get(id);
    if (entity == nullptr || newName.empty()) {
        return false;
    }
    if (entity->Name() == newName) {
        return true;   // renaming something to what it is already called
    }
    if (!Find(newName).IsNull()) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "cannot rename '{}': something is already called '{}'",
                        entity->Name(), newName);
        return false;
    }

    // The lookup table is keyed on the name, so both the old and the new key
    // have to be fixed. Calling Entity::SetName on its own would leave Find()
    // answering for a name that no longer exists.
    m_byName.erase(entity->Name());
    entity->SetName(newName);
    m_byName[std::string(newName)] = id;
    return true;
}

EntityId Scene::DuplicateEntity(EntityId id, std::string& outError) {
    Entity* source = Get(id);
    if (source == nullptr) {
        outError = "there is nothing selected to duplicate";
        return EntityId{};
    }

    // Write the entity out to JSON and read it straight back in. Doing it this
    // way means a duplicate is automatically a complete copy of every
    // component, including component types added later by somebody else.
    Json                     componentBlobs = Json::array();
    std::vector<std::string> componentTypes;

    source->ForEachComponent([&](Component& component) {
        Json blob = Json::object();
        if (!component.Serialize(blob)) {
            ENGINE_LOG_WARN(Channels::kScene,
                            "duplicating '{}': '{}' cannot be copied and will be created "
                            "with its default settings",
                            source->Name(), component.TypeName());
        }
        componentBlobs.push_back(std::move(blob));
        componentTypes.emplace_back(component.TypeName());
    });

    const std::string name       = MakeUniqueName(source->Name());
    const EntityId    copyId     = CreateEntity(name);
    Entity*           copy       = Get(copyId);
    if (copy == nullptr) {
        outError = "could not create the copy";
        return EntityId{};
    }

    for (std::size_t i = 0; i < componentTypes.size(); ++i) {
        std::unique_ptr<Component> component = ComponentFactory::Create(componentTypes[i]);
        if (component == nullptr) {
            continue;
        }
        // Fill it in BEFORE attaching, for the same reason loading does. See
        // the note in CreateEntityFromJson.
        std::string componentError;
        if (!component->Deserialize(componentBlobs[i], componentError)) {
            ENGINE_LOG_WARN(Channels::kScene, "duplicating '{}': {}", name, componentError);
        }
        copy->AddComponent(std::move(component));
    }

    // The copy gets the same parent as the original and keeps the same local
    // position, so it lands exactly on top of what it was copied from - which
    // makes "duplicate, then drag it aside" the obvious next action.
    if (Transform2D* sourceParent = source->Transform().Parent(); sourceParent != nullptr) {
        copy->Transform().SetParent(sourceParent);
    }

    outError.clear();
    return copyId;
}

// ---------------------------------------------------------------------------
//  Prefabs
// ---------------------------------------------------------------------------

bool Scene::HasPrefab(std::string_view name) const {
    const Json& prefabs = Field(m_document, "prefabs");
    return prefabs.is_object() && prefabs.contains(std::string(name));
}

EntityId Scene::InstantiatePrefab(std::string_view prefab, std::string_view name,
                                  std::string& outError) {
    const Json& prefabs = Field(m_document, "prefabs");
    if (!prefabs.is_object() || !prefabs.contains(std::string(prefab))) {
        outError = "this scene has no prefab called '" + std::string(prefab) + "'";
        return EntityId{};
    }
    return CreateEntityFromJson(prefabs[std::string(prefab)], name, outError);
}

} // namespace eng
