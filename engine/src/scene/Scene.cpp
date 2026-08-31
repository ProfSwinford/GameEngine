// WEEK 9 - the scene loader. See Scene.h.
//
// Note what is NOT in this file: no entity name, no position, no texture
// filename. That is the Milestone 3 bar, and grepping for any of them is how
// it is checked.

#include <engine/core/Log.h>
#include <engine/debug/DebugDraw.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Component.h>
#include <engine/fs/FileSystem.h>
#include <engine/scene/Scene.h>

#include <nlohmann/json.hpp>

namespace eng {

using Json = nlohmann::json;

namespace {

Scene* g_active = nullptr;

} // namespace

Scene::Scene() = default;

Scene::~Scene() {
    Unload();
    if (g_active == this) {
        g_active = nullptr;
    }
}

Scene* Scene::Active() { return g_active; }
void   Scene::SetActive(Scene* scene) { g_active = scene; }

EntityHandle Scene::CreateEntity(std::string_view name) {
    u32 index = 0;

    if (!m_freeIndices.empty()) {
        // FIFO reuse - front, not back. See the note in Scene.h about keeping
        // the generation counter away from its wrap point.
        index = m_freeIndices.front();
        m_freeIndices.pop_front();
    } else {
        index = static_cast<u32>(m_slots.size());
        if (index > EntityHandle::kMaxIndex) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "entity index space exhausted ({} slots) - widen the index "
                             "bits in Handle.h", EntityHandle::kMaxIndex);
            return EntityHandle{};
        }
        m_slots.emplace_back();
    }

    Slot& slot = m_slots[index];
    slot.entity   = std::make_unique<Entity>();
    slot.occupied = true;

    slot.entity->m_handle = MakeHandle<EntityTag>(index, slot.generation);
    slot.entity->m_scene  = this;
    slot.entity->m_alive  = true;
    slot.entity->SetName(name);

    m_byName[slot.entity->NameId().Value()] = slot.entity->m_handle;
    ++m_liveCount;

    return slot.entity->m_handle;
}

void Scene::DestroyEntityImmediate(EntityHandle handle) {
    if (!IsValid(handle)) {
        // Double destroy is HARMLESS and silent at this level - gameplay code
        // does it constantly (two bullets hit the same enemy in one tick) and
        // DeferredOps filters most of it, but the last line of defence is
        // here.
        return;
    }

    Slot& slot = m_slots[handle.Index()];

    if (slot.entity != nullptr) {
        m_byName.erase(slot.entity->NameId().Value());
        slot.entity->DestroyInternal();
    }
    slot.entity.reset();
    slot.occupied = false;

    // Bump the generation LAST. From this moment every outstanding handle to
    // this slot is detectably stale rather than dangling - which is the whole
    // mechanism, and the reason the editor can hold a selection across a
    // destroy without crashing.
    //
    // Wrapping back to 1, not 0: generation 0 combined with index 0 is the
    // null handle, so a live slot must never be at generation 0.
    slot.generation = (slot.generation + 1) & EntityHandle::kMaxGeneration;
    if (slot.generation == 0) {
        slot.generation = 1;
    }

    m_freeIndices.push_back(handle.Index());
    if (m_liveCount > 0) {
        --m_liveCount;
    }
}

Entity* Scene::Get(EntityHandle handle) {
    if (!IsValid(handle)) {
        return nullptr;
    }
    return m_slots[handle.Index()].entity.get();
}

bool Scene::IsValid(EntityHandle handle) const {
    if (handle.IsNull()) {
        return false;
    }
    const u32 index = handle.Index();
    if (index >= m_slots.size()) {
        return false;
    }
    const Slot& slot = m_slots[index];
    return slot.occupied && slot.generation == handle.Generation();
}

EntityHandle Scene::Find(StringId name) const {
    const auto it = m_byName.find(name.Value());
    return (it != m_byName.end()) ? it->second : EntityHandle{};
}

EntityHandle Scene::Find(std::string_view name) const {
    return Find(StringId(name));
}

void Scene::ForEach(const std::function<void(Entity&)>& fn) {
    // By index, and the size is re-read: a callback may create an entity, and
    // m_slots would then reallocate under an iterator. Newly created entities
    // are visited this pass, which is deliberate for load-time use and is
    // exactly why gameplay spawns go through DeferredOps instead.
    for (usize i = 0; i < m_slots.size(); ++i) {
        Slot& slot = m_slots[i];
        if (slot.occupied && slot.entity != nullptr) {
            fn(*slot.entity);
        }
    }
}

EntityHandle Scene::CreateEntityFromNode(const ConfigNode& node,
                                         std::string_view nameOverride,
                                         std::string& outError) {
    std::string name(nameOverride);
    if (name.empty()) {
        name = node.Child("name").AsString("");
    }
    if (name.empty()) {
        outError = node.Path() + " has no 'name'";
        return EntityHandle{};
    }

    const EntityHandle handle = CreateEntity(name);
    Entity*            entity = Get(handle);
    if (entity == nullptr) {
        outError = "could not allocate an entity slot for '" + name + "'";
        return EntityHandle{};
    }

    const ConfigNode components = node.Child("components");
    if (!components.IsValid()) {
        return handle;   // an entity with only a transform is legal
    }
    if (!components.IsArray()) {
        outError = components.Path() + " must be an array";
        return handle;
    }

    for (usize i = 0; i < components.Size(); ++i) {
        const ConfigNode componentNode = components.At(i);
        const std::string typeName     = componentNode.Child("type").AsString("");
        if (typeName.empty()) {
            // Reported with the ENTITY and the FIELD, not "parse error".
            ENGINE_LOG_ERROR(Channels::kScene, "{}: component {} has no 'type'", name, i);
            continue;
        }

        // *** DESERIALIZE BEFORE ATTACH, AND THE ORDER IS LOAD-BEARING. ***
        //
        // OnAttach is where a component registers with its system and acquires
        // its resources - SpriteComponent::OnAttach calls AcquireTexture on the
        // path it was given. Attaching first and deserialising second means it
        // acquires an EMPTY path, and the scene renders nothing while the
        // resource panel shows one texture resident instead of four.
        //
        // That is exactly the "scene loads but nothing appears" symptom the
        // Week 9 troubleshooting list describes, and it is why Component.h
        // insists registration belongs in OnAttach rather than the constructor
        // and why the data has to be in place before OnAttach runs.
        std::unique_ptr<Component> component = ComponentFactory::Create(typeName);
        if (component == nullptr) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "{}: unknown component type '{}' - is it registered with "
                             "ComponentFactory?", name, typeName);
            continue;
        }

        std::string componentError;
        if (!component->Deserialize(componentNode, componentError)) {
            // Reported with the ENTITY and the FIELD, then attached anyway: a
            // sprite with a bad tint is still a sprite, and dropping the whole
            // component would turn one typo into an invisible entity.
            ENGINE_LOG_ERROR(Channels::kScene, "{}.{}: {}", name, typeName, componentError);
        }

        entity->AddComponent(std::move(component));
    }

    return handle;
}

void Scene::ResolveParents(const ConfigNode& entitiesNode) {
    // A second pass, because a child may name a parent that appears later in
    // the file. Doing it in one pass would make the scene file order-sensitive
    // for no reason, and "your entity vanished because you moved it up three
    // lines" is a bad afternoon.
    for (usize i = 0; i < entitiesNode.Size(); ++i) {
        const ConfigNode node       = entitiesNode.At(i);
        const std::string childName = node.Child("name").AsString("");
        const std::string parentName = node.Child("parent").AsString("");
        if (childName.empty() || parentName.empty()) {
            continue;
        }

        Entity* child  = Get(Find(childName));
        Entity* parent = Get(Find(parentName));
        if (child == nullptr) {
            continue;
        }
        if (parent == nullptr) {
            ENGINE_LOG_ERROR(Channels::kScene, "{}: parent '{}' does not exist in this scene",
                             childName, parentName);
            continue;
        }
        child->Transform().SetParent(&parent->Transform());
    }
}

// Builds the live scene from whatever is already in m_document. Shared by
// Load (from a file) and LoadFromString (from a Play-mode snapshot), so the two
// cannot drift apart - and a snapshot restore therefore goes through exactly
// the same code path as a normal load.
bool Scene::BuildFromDocument(std::string& outError) {
    const ConfigNode root = m_document.Root();
    m_name = root.Child("name").AsString("<unnamed>");

    if (const ConfigNode camera = root.Child("camera"); camera.IsValid()) {
        f32 position[2] = {0.0f, 0.0f};
        camera.Child("position").AsFloatArray(position, 2);
        m_cameraPosition = Vec2{position[0], position[1]};
        m_cameraZoom     = static_cast<f32>(camera.Child("zoom").AsFloat(1.0));
    }

    const ConfigNode entities = root.Child("entities");
    if (!entities.IsValid() || !entities.IsArray()) {
        outError = "scene has no 'entities' array";
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    for (usize i = 0; i < entities.Size(); ++i) {
        std::string entityError;
        if (CreateEntityFromNode(entities.At(i), {}, entityError).IsNull()) {
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

    if (!m_document.LoadFromVirtualPath(virtualPath, outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "scene {}: {}", virtualPath, outError);
        return false;
    }
    m_sourcePath.assign(virtualPath);

    if (!BuildFromDocument(outError)) {
        return false;
    }

    ENGINE_LOG_INFO(Channels::kScene,
                    "scene '{}' loaded from {}: {} entities, {} textures resident, "
                    "total refcount {}",
                    m_name, virtualPath, m_liveCount, ResourceManager::LoadedCount(),
                    ResourceManager::TotalRefCount());
    return true;
}

namespace {

// Free rather than a member, so its signature never has to appear in a public
// header - see the note in Scene.h. Handed the few private fields it needs.
Json BuildSceneDocument(Scene& scene, const std::string& sceneName, Vec2 cameraPosition,
                        f32 cameraZoom, usize& outSkipped, const Json* existing) {
    // PRESERVE WHAT WE DID NOT WRITE - `_comment`, `prefabs` and any key a
    // newer build understands. Only name / camera / entities are regenerated.
    Json root = (existing != nullptr && existing->is_object()) ? *existing : Json::object();

    root["name"] = sceneName.empty() ? std::string("Untitled") : sceneName;

    // Engine::SaveScene pushes the LIVE camera in via SetCameraState before
    // calling this, so framing a shot in the editor and saving keeps the
    // framing rather than silently rewriting whatever the file said at load.
    root["camera"]["position"] = Json::array({cameraPosition.x, cameraPosition.y});
    root["camera"]["zoom"]     = cameraZoom;

    // Parenting comes from the TRANSFORM TREE, not from the source file, so an
    // entity reparented in the editor saves correctly. Names are resolved
    // through a transform -> name map built in the same pass.
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
            ConfigWriter writer;
            if (!component.Serialize(writer)) {
                // REPORTED, not silently dropped. A save that loses a component
                // without saying so is worse than one that refuses.
                ENGINE_LOG_WARN(Channels::kScene,
                                "saving '{}': component '{}' does not support saving and "
                                "was not written",
                                entity.Name(), component.TypeName());
                ++outSkipped;
                return;
            }
            Json componentJson = *static_cast<const Json*>(writer.NativeHandle());
            // The type key is written HERE rather than by each component, so no
            // component can get its own name wrong.
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
        outError = "no path to save to";
        return false;
    }

    // The document as LOADED, so keys this build does not understand survive a
    // round trip. Null when the scene was built in memory rather than loaded.
    const Json* existing = nullptr;
    if (m_document.IsLoaded()) {
        existing = static_cast<const Json*>(m_document.Root().NativeHandle());
    }

    usize      skipped = 0;
    const Json root = BuildSceneDocument(*this, m_name, m_cameraPosition, m_cameraZoom,
                                         skipped, existing);

    const std::string text = root.dump(2);
    if (!FileSystem::WriteFile(virtualPath, text.data(), text.size(), outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "could not save '{}': {}", virtualPath, outError);
        return false;
    }

    // Save As RETARGETS the scene, so a subsequent Ctrl+S goes to the new file
    // rather than back to the one it was opened from.
    m_sourcePath.assign(virtualPath);

    if (skipped > 0) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "saved '{}' with {} component(s) SKIPPED - the file is not a "
                        "complete record of the scene", virtualPath, skipped);
    } else {
        ENGINE_LOG_INFO(Channels::kScene, "saved '{}': {} entities", virtualPath,
                        m_liveCount);
    }
    outError.clear();
    return true;
}

bool Scene::SaveToString(std::string& outText, std::string& outError) {
    const Json* existing = nullptr;
    if (m_document.IsLoaded()) {
        existing = static_cast<const Json*>(m_document.Root().NativeHandle());
    }

    usize      skipped = 0;
    const Json root = BuildSceneDocument(*this, m_name, m_cameraPosition, m_cameraZoom,
                                         skipped, existing);

    // A SKIPPED COMPONENT MAKES THIS FAIL, where Save only warns - and the
    // difference is deliberate. A file with a component missing is a file the
    // user can look at and fix; a Play-mode snapshot with a component missing
    // silently DELETES that component when Stop restores it. So Engine
    // refuses to enter Play mode rather than promising a restore it cannot
    // deliver.
    if (skipped > 0) {
        outError = "the scene contains " + std::to_string(skipped) +
                   " component(s) that cannot be saved, so a snapshot would not "
                   "restore the scene faithfully";
        return false;
    }

    outText = root.dump();
    outError.clear();
    return true;
}

bool Scene::LoadFromString(std::string_view text, std::string& outError) {
    Unload();

    // NOTE that m_sourcePath is NOT touched. Restoring a Play-mode snapshot
    // must not make the scene forget which file it came from - Ctrl+S after
    // Stop still saves to the right place.
    if (!m_document.LoadFromText(text, outError)) {
        ENGINE_LOG_ERROR(Channels::kScene, "restoring scene: {}", outError);
        return false;
    }

    return BuildFromDocument(outError);
}

void Scene::Unload() {
    if (m_slots.empty()) {
        return;
    }

    // Destroy every entity, which detaches every component, which RELEASES
    // EVERY RESOURCE. After this TotalRefCount() must read zero - the M3
    // check, watched live on the resource panel.
    for (usize i = 0; i < m_slots.size(); ++i) {
        Slot& slot = m_slots[i];
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

    // Otherwise a three-second debug marker outlives the entity it was
    // marking, and points at a position nothing occupies any more.
    DebugDraw::Clear();

    ENGINE_LOG_INFO(Channels::kScene, "scene unloaded; {} texture(s) resident, total "
                                      "refcount {}",
                    ResourceManager::LoadedCount(), ResourceManager::TotalRefCount());

    m_name.clear();

    // m_sourcePath is DELIBERATELY NOT CLEARED. It is "the path this scene was
    // last loaded from", and after an unload that is still true and still the
    // only way back.
    //
    // Clearing it was a real bug: the editor's File > Unload Scene is the
    // Milestone 3 demonstration, and File > Reload Scene immediately afterwards
    // reloaded the empty string and failed. Unloading the scene made the scene
    // unloadable-from, which is a small and very annoying dead end.
}

std::string Scene::MakeUniqueName(std::string_view base) const {
    std::string candidate(base);
    if (candidate.empty()) {
        candidate = "Entity";
    }
    if (Find(candidate).IsNull()) {
        return candidate;
    }
    for (u32 suffix = 1; suffix < 100000u; ++suffix) {
        std::string attempt = std::string(base) + "_" + std::to_string(suffix);
        if (Find(attempt).IsNull()) {
            return attempt;
        }
    }
    return candidate;
}

bool Scene::RenameEntity(EntityHandle handle, std::string_view newName) {
    Entity* entity = Get(handle);
    if (entity == nullptr || newName.empty()) {
        return false;
    }
    if (entity->Name() == newName) {
        return true;   // renaming to itself is a no-op, not a failure
    }
    if (!Find(newName).IsNull()) {
        ENGINE_LOG_WARN(Channels::kScene, "cannot rename '{}': '{}' is already taken",
                        entity->Name(), newName);
        return false;
    }

    // The map key changes with the name. Doing this through Entity::SetName
    // alone would leave m_byName holding the OLD name pointing at this handle
    // and no entry for the new one - so Find() would answer questions about a
    // name that no longer exists and fail on the one that does.
    m_byName.erase(entity->NameId().Value());
    entity->SetName(newName);
    m_byName[entity->NameId().Value()] = handle;
    return true;
}

EntityHandle Scene::DuplicateEntity(EntityHandle handle, std::string& outError) {
    Entity* source = Get(handle);
    if (source == nullptr) {
        outError = "nothing to duplicate";
        return EntityHandle{};
    }

    // Serialize, then rebuild from that. A hand-written member-by-member copy
    // would need extending every time a component type is added, and would be
    // forgotten exactly once.
    ConfigWriter entityWriter;
    std::vector<ConfigWriter> componentWriters;
    std::vector<std::string>  componentTypes;

    source->ForEachComponent([&](Component& component) {
        ConfigWriter writer;
        if (!component.Serialize(writer)) {
            ENGINE_LOG_WARN(Channels::kScene,
                            "duplicating '{}': component '{}' cannot be serialised and "
                            "will be created with its defaults",
                            source->Name(), component.TypeName());
        }
        componentWriters.push_back(std::move(writer));
        componentTypes.emplace_back(component.TypeName());
    });

    const std::string name = MakeUniqueName(source->Name());
    const EntityHandle copyHandle = CreateEntity(name);
    Entity* copy = Get(copyHandle);
    if (copy == nullptr) {
        outError = "could not allocate an entity slot";
        return EntityHandle{};
    }

    for (usize i = 0; i < componentTypes.size(); ++i) {
        std::unique_ptr<Component> component = ComponentFactory::Create(componentTypes[i]);
        if (component == nullptr) {
            continue;
        }
        // Deserialize BEFORE attach, for the same reason Scene::Load does -
        // OnAttach acquires resources from fields that must already be set.
        const ConfigNode node = componentWriters[i].AsNode(name + "." + componentTypes[i]);
        std::string componentError;
        if (!component->Deserialize(node, componentError)) {
            ENGINE_LOG_WARN(Channels::kScene, "duplicating '{}': {}", name, componentError);
        }
        copy->AddComponent(std::move(component));
    }

    // Same parent as the original, keeping its LOCAL transform - so a
    // duplicate lands exactly on top of what it was copied from, which is what
    // makes "duplicate then drag it aside" the obvious next action.
    if (Transform2D* sourceParent = source->Transform().Parent(); sourceParent != nullptr) {
        copy->Transform().SetParent(sourceParent);
    }

    outError.clear();
    return copyHandle;
}

bool Scene::HasPrefab(StringId name) const {
    if (!m_document.IsLoaded()) {
        return false;
    }
    return m_document.Root().Child("prefabs").Child(name.ToString()).IsValid();
}

ConfigNode Scene::Prefab(StringId name) const {
    if (!m_document.IsLoaded()) {
        return ConfigNode{};
    }
    return m_document.Root().Child("prefabs").Child(name.ToString());
}

EntityHandle Scene::InstantiatePrefab(StringId prefab, std::string_view name,
                                      std::string& outError) {
    const ConfigNode node = Prefab(prefab);
    if (!node.IsValid()) {
        outError = std::string("no prefab named '") + prefab.ToString() +
                   "' in this scene";
        return EntityHandle{};
    }
    return CreateEntityFromNode(node, name, outError);
}

} // namespace eng
