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
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
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

    // ======================================================================
    //  A NAME THAT IS ALREADY TAKEN IS REPORTED, LOUDLY.
    //
    //  Names are an identity in this engine - parents resolve by name on
    //  every load and every EntityRef stores one - so two live entities with
    //  the same name is not untidiness, it is two things claiming to be one.
    //  What the overwrite below actually costs is written out at the
    //  declaration of this function in Scene.h.
    //
    //  AT ERROR LEVEL, NOT WARN, AND THAT IS NOT SHOUTING. config/engine.json
    //  ships with the log threshold at Error, so a warning here would be
    //  discarded by the default configuration and this message would only
    //  ever appear for somebody who had already gone looking.
    //
    //  Reported rather than refused: a malformed scene file with two
    //  identical names should still open, minus the ambiguity, rather than
    //  losing an entity. The fix is named in the message because "duplicate
    //  name" on its own does not tell anybody what to do.
    // ======================================================================
    if (const auto existing = m_byName.find(std::string(name));
        existing != m_byName.end() && IsValid(existing->second) &&
        existing->second != slot.entity->m_id) {
        ENGINE_LOG_ERROR(Channels::kScene,
                         "two entities are now called '{}', and a name is an identity "
                         "here - parents and entity references are both recorded by "
                         "name, so one of them will lose its children and its "
                         "references the next time this scene is loaded. Spawning code "
                         "should call Scene::CreateEntityUniquelyNamed instead of "
                         "CreateEntity",
                         name);
    }

    m_byName[std::string(name)] = slot.entity->m_id;
    ++m_liveCount;

    return slot.entity->m_id;
}

EntityId Scene::CreateEntityUniquelyNamed(std::string_view baseName) {
    return CreateEntity(MakeUniqueName(baseName));
}

EntityId Scene::CreateEntityUniquelyNamed(std::string_view    baseName,
                                          const SpawnOptions& placement) {
    const std::string_view wanted =
        placement.nameOverride.empty() ? baseName : std::string_view(placement.nameOverride);
    const EntityId id = CreateEntity(MakeUniqueName(wanted));
    ApplyPlacement(id, placement);
    return id;
}

// ---------------------------------------------------------------------------
//  Placement, shared by every spawn route so there is one answer to "where
//  does a spawned thing go".
// ---------------------------------------------------------------------------

void Scene::ApplyPlacement(EntityId id, const SpawnOptions& placement) {
    Entity* spawned = Get(id);
    if (spawned == nullptr) {
        return;
    }

    Transform2D& transform = spawned->Transform();

    // ROTATION AND SCALE FIRST, THEN THE PARENT, THEN THE POSITION.
    //
    // The order is load-bearing. SetWorldPosition works out what local
    // position produces a given world one UNDER THE CURRENT PARENT, so it has
    // to run after the parent is set or it solves the wrong equation. And
    // SetParent with keepWorldTransform recomputes the local numbers from
    // where the thing currently is, so it has to run after the rotation and
    // scale that decide where that is.
    if (placement.overrideRotation) {
        transform.SetLocalRotation(placement.rotationRadians);
    }
    if (placement.overrideScale) {
        transform.SetLocalScale(placement.scale);
    }

    if (!placement.parent.IsNull()) {
        Entity* under = Get(placement.parent);
        if (under == nullptr) {
            // Reported rather than ignored. "My bullets have no parent" is a
            // silent symptom of a parent id that went stale, and the thing
            // still spawns - at the top level - so the game carries on.
            ENGINE_LOG_ERROR(Channels::kScene,
                             "'{}' was spawned to go under an entity that no longer "
                             "exists, so it has been left at the top level instead",
                             spawned->Name());
        } else {
            // keepWorldTransform=false here even when the caller asked for a
            // world position, because the position has not been applied yet -
            // there is nothing to keep. The world position is honoured below,
            // after the parent is in place, which is the only order in which
            // both can be right.
            transform.SetParent(&under->Transform(), /*keepWorldTransform=*/false);

            // THE PREFAB RECORD HAS TO FOLLOW THE TREE. A prefab instance's
            // parent lives in its record, not in the transform, and the
            // record is what gets written out - so reparenting the live root
            // without this loses the parent on the next save. See Prefab.h.
            if (PrefabInstance* record = PrefabInstanceOf(id); record != nullptr) {
                record->parentName = under->Name();
            }
        }
    }

    if (placement.overridePosition) {
        if (placement.positionIsWorldSpace) {
            transform.SetWorldPosition(placement.position);
        } else {
            transform.SetLocalPosition(placement.position);
        }
    }
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

    // ======================================================================
    //  DESTROYING A PREFAB INSTANCE'S ROOT DESTROYS THE INSTANCE.
    //
    //  Anything else would be wrong rather than merely untidy. The lines
    //  below hand an entity's children back to the WORLD, which for a prefab
    //  root would leave its members scattered at the top level of the scene
    //  still claiming to belong to an instance whose root has gone - and the
    //  next save would write an instance record describing entities that are
    //  no longer arranged the way the asset says.
    //
    //  THIS IS NOT RECURSIVE. DestroyPrefabMembers clears every member's link
    //  before it destroys anything, so the root arrives back here as an
    //  ordinary entity.
    // ======================================================================
    if (slot.prefab != nullptr && IsPrefabRoot(id)) {
        DestroyPrefabInstanceImmediate(id);
        return;
    }

    if (slot.entity != nullptr) {
        m_byName.erase(slot.entity->Name());
        slot.entity->DestroyInternal();
    }
    slot.entity.reset();
    slot.occupied = false;

    // The prefab link goes with the occupant. A slot is reused, and one that
    // kept the last entity's instance pointer would hand a brand new entity
    // somebody else's prefab - see the note on Slot::prefab in Scene.h.
    slot.prefab        = nullptr;
    slot.prefabLocalId = 0;

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

const Entity* Scene::Get(EntityId id) const {
    if (!IsValid(id)) {
        return nullptr;
    }
    return m_slots[static_cast<std::size_t>(id.index)].entity.get();
}

EntityId Scene::Find(std::string_view name) const {
    const std::string key(name);
    if (m_byName.contains(key)) {
        return m_byName.at(key);
    }
    // A null id, which IsNull() reports as empty - see EntityId.h.
    return EntityId{};
}

Entity* Scene::FindEntity(std::string_view name) {
    return Get(Find(name));
}

const Entity* Scene::FindEntity(std::string_view name) const {
    return Get(Find(name));
}

// ---------------------------------------------------------------------------
//  Transform -> entity. See the long note at the declaration in Scene.h for
//  why this table is kept by the scene rather than by Transform2D.
// ---------------------------------------------------------------------------

void Scene::RegisterTransform(const Transform2D& transform, EntityId owner) {
    m_transformOwners[&transform] = owner;
}

void Scene::ForgetTransform(const Transform2D& transform) {
    m_transformOwners.erase(&transform);
}

EntityId Scene::IdOf(const Transform2D& transform) const {
    const auto found = m_transformOwners.find(&transform);
    if (found == m_transformOwners.end()) {
        return EntityId{};
    }

    // CHECKED AGAINST THE GENERATION ON THE WAY OUT, not trusted. The table is
    // kept up to date by TransformComponent's OnDetach, so a stale entry
    // should be impossible - and "should be impossible" is exactly the
    // reasoning that turns a missed teardown path into a use-after-free
    // instead of a null. IsValid costs two comparisons.
    return IsValid(found->second) ? found->second : EntityId{};
}

Entity* Scene::EntityOf(const Transform2D& transform) {
    return Get(IdOf(transform));
}

const Entity* Scene::EntityOf(const Transform2D& transform) const {
    return Get(IdOf(transform));
}

// ---------------------------------------------------------------------------
//  Tags
// ---------------------------------------------------------------------------

Entity* Scene::FindByTag(std::string_view tag, bool skipPendingDestroy) {
    if (tag.empty()) {
        // Empty means untagged - see Entity::Tag. Answering with the first
        // untagged entity in the scene would be a surprising reading of
        // FindByTag(""), and it is almost always an uninitialised variable.
        return nullptr;
    }

    Entity* found = nullptr;
    ForEachUntil([&](Entity& entity) {
        if (skipPendingDestroy && DeferredOps::IsPendingDestroy(entity.Id())) {
            return true;
        }
        if (entity.HasTag(tag)) {
            found = &entity;
            return false;
        }
        return true;
    });
    return found;
}

std::size_t Scene::ForEachWithTag(std::string_view                    tag,
                                  const std::function<void(Entity&)>& fn,
                                  bool skipPendingDestroy) {
    if (tag.empty()) {
        return 0;
    }

    std::size_t seen = 0;
    ForEach([&](Entity& entity) {
        if (skipPendingDestroy && DeferredOps::IsPendingDestroy(entity.Id())) {
            return;
        }
        if (entity.HasTag(tag)) {
            ++seen;
            fn(entity);
        }
    });
    return seen;
}

std::size_t Scene::CountWithTag(std::string_view tag, bool skipPendingDestroy) {
    return ForEachWithTag(tag, [](Entity&) {}, skipPendingDestroy);
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

bool Scene::ForEachUntil(const std::function<bool(Entity&)>& fn) {
    // By index with the size re-read, exactly as ForEach above, and for the
    // same reason: the callback is allowed to create an entity.
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        Slot& slot = m_slots[i];
        if (slot.occupied && slot.entity != nullptr) {
            if (!fn(*slot.entity)) {
                return false;
            }
        }
    }
    return true;
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

    // The tag, if the file names one. Read with "" as the default, and the
    // save side writes the key out ONLY when it is not empty - see
    // BuildSceneDocument. That keeps a tag out of every scene file written
    // before tags existed, which is what makes this change invisible to a
    // scene nobody has tagged.
    entity->SetTag(ReadString(node, "tag", ""));

    // Active unless the file says otherwise, read the same way and for the same
    // reason: true is overwhelmingly the common case, so the key is absent from
    // every entity nobody has switched off.
    entity->SetActive(ReadBool(node, "active", true));

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

    const Json& entities  = Field(m_document, "entities");
    const Json& instances = Field(m_document, "prefabInstances");

    // EITHER will do. A scene file assembled entirely out of prefabs has no
    // plain entities at all, and refusing to open it would be the file format
    // telling somebody they have used the engine too well.
    if (!entities.is_array() && !instances.is_array()) {
        outError = "this scene file has no \"entities\" list and no "
                   "\"prefabInstances\" list, so there is nothing in it";
        ENGINE_LOG_ERROR(Channels::kScene, "{}", outError);
        return false;
    }

    if (entities.is_array()) {
        for (const Json& node : entities) {
            std::string entityError;
            if (CreateEntityFromJson(node, {}, entityError).IsNull()) {
                ENGINE_LOG_ERROR(Channels::kScene, "{}", entityError);
            }
        }
    }

    // ======================================================================
    //  THE PREFABS GO IN BETWEEN, and that position is the whole reason this
    //  is three steps rather than two.
    //
    //  AFTER the plain entities, because a prefab instance can name one of
    //  them as its parent - and ResolveParents looks parents up by name, so
    //  they have to exist.
    //
    //  BEFORE ResolveParents, because the reverse is also true: a plain
    //  entity can be parented under a prefab's child, and the name that child
    //  ends up with in this scene is not settled until the instance is built.
    // ======================================================================
    if (instances.is_array()) {
        BuildPrefabInstances(instances);
    }

    if (entities.is_array()) {
        ResolveParents(entities);
    }

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

    ENGINE_LOG_INFO(Channels::kScene, "scene '{}' loaded from {}: {} entities, {} texture(s)",
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
        // ==================================================================
        //  A PREFAB INSTANCE'S MEMBERS ARE NOT WRITTEN HERE. They are written
        //  once, as the instance record further down, and BUILT from the
        //  asset when the scene is read back.
        //
        //  Writing them out as ordinary entities as well would not merely
        //  duplicate them - it would BREAK THE LINK, which is the entire
        //  feature. The scene file would hold a frozen copy of every
        //  instance, editing the asset would reach none of them, and loading
        //  would produce every member twice over.
        // ==================================================================
        if (scene.PrefabInstanceOf(entity.Id()) != nullptr) {
            return;
        }

        Json entityJson    = Json::object();
        entityJson["name"] = entity.Name();

        // ONLY WHEN THERE IS ONE. An untagged entity writes no "tag" key at
        // all, so adding tags to the engine did not change one byte of any
        // existing scene file - which is what lets this be checked by
        // round-tripping a scene saved by the previous build.
        if (!entity.Tag().empty()) {
            entityJson["tag"] = entity.Tag();
        }

        // Only when switched OFF, for the same reason. An "active": true on
        // every entity in every scene would be noise in the file and would
        // have rewritten every scene in the project the first time it saved.
        if (!entity.IsActiveSelf()) {
            entityJson["active"] = false;
        }

        const Transform2D* parent = entity.Transform().Parent();
        if (parent != nullptr && transformNames.contains(parent)) {
            entityJson["parent"] = transformNames.at(parent);
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

    // ---- and the prefab instances -----------------------------------------
    //
    // DIFFED AS THEY ARE WRITTEN, which is what makes the override list in the
    // file describe the scene rather than describe whatever it described when
    // it was last loaded. See Prefab.h on why overrides are compared for
    // rather than recorded as they happen.
    Json instances = Json::array();
    scene.ForEachPrefabInstance([&](PrefabInstance& instance) {
        std::string        assetError;
        const PrefabAsset* asset = PrefabLibrary::Load(instance.prefabPath, assetError);
        if (asset == nullptr) {
            // The asset cannot be read, so there is nothing to compare
            // against. THE RECORD IS WRITTEN OUT UNCHANGED rather than
            // dropped or re-diffed: the overrides it is already holding are
            // somebody's work, and a prefab that cannot be read is far more
            // often one that has been moved than one that is gone.
            ENGINE_LOG_WARN(Channels::kScene,
                            "saving: could not read '{}', so '{}' keeps the overrides "
                            "it already had ({})",
                            instance.prefabPath, instance.rootName, assetError);
        } else {
            CapturePrefabOverrides(scene, *asset, instance, &outSkipped);
        }
        instances.push_back(WriteInstanceToJson(instance));
    });

    if (instances.empty()) {
        // ERASED, NOT LEFT ALONE. `root` started as the document this scene
        // was loaded from, so a scene whose last instance has just been
        // unpacked would otherwise keep the old list and grow its prefabs
        // back on the next load.
        root.erase("prefabInstances");
    } else {
        root["prefabInstances"] = std::move(instances);
    }

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

    // Also after the slots: every TransformComponent's OnDetach above took its
    // own entry out, so by now this should already be empty. Cleared anyway,
    // because an entry left behind would be a pointer to a freed Transform2D
    // that the next EntityOf could match against a newly allocated one at the
    // same address - and a reverse lookup that answers with the wrong entity
    // is worse than one that answers with none.
    m_transformOwners.clear();

    // AFTER the slots, not before. Every member's slot holds a pointer to its
    // instance, and freeing the records while slots still pointed at them
    // would leave dangling pointers in a container that is about to be walked.
    //
    // The parsed ASSETS are deliberately left in PrefabLibrary. They are not
    // scene state - the same enemy.oprefab is the same file for the next
    // scene, and throwing the cache away on every scene change would re-read
    // and re-parse every prefab in the project on each load.
    m_prefabInstances.clear();

    // Otherwise a three-second gizmo would outlive the entity it was marking
    // and hang in the air pointing at nothing.
    Gizmos::Clear();

    ResourceManager::PruneCache();
    ENGINE_LOG_INFO(Channels::kScene, "scene unloaded; {} texture(s) still loaded",
                    ResourceManager::LoadedCount());

    m_name.clear();

    // m_sourcePath is NOT cleared. It means "the file this scene came from",
    // and that is still true after an unload - it is the only way back.
}

void Scene::StartNewScene() {
    Unload();

    // AND THE DOCUMENT TOO, not just the path. m_document holds the keys this
    // build did not recognise when the scene was read, so that saving puts
    // them back - see BuildSceneDocument. Carrying them into a brand new
    // scene would write another file's unknown settings into it.
    m_document   = Json::object();
    m_sourcePath.clear();

    // Back to where a scene with no camera saved in it starts, so a new scene
    // does not inherit the last one's framing.
    m_cameraPosition = Vec2{};
    m_cameraZoom     = 1.0f;

    ENGINE_LOG_INFO(Channels::kScene, "new scene started; it has no file yet");
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

    // Taken before the rename, because it is about to stop being true and two
    // things below still need it.
    const std::string oldName = entity->Name();

    // The lookup table is keyed on the name, so both the old and the new key
    // have to be fixed. Calling Entity::SetName on its own would leave Find()
    // answering for a name that no longer exists.
    m_byName.erase(oldName);
    entity->SetName(newName);
    m_byName[std::string(newName)] = id;

    // ======================================================================
    //  EVERY REFERENCE TO THIS ENTITY MOVES WITH IT.
    //
    //  A script's entity reference stores a NAME - see EntityRef.h for why -
    //  so without this, renaming something in the Hierarchy would quietly
    //  disconnect every script pointing at it. Renaming a thing is an
    //  ordinary act, and a feature that breaks when you do ordinary things is
    //  not finished.
    //
    //  DONE HERE AND NOT IN THE EDITOR, because this function is the only way
    //  a name changes - Entity::SetName alone would desync the index above,
    //  which is why that is not the path anything uses. Game code that renames
    //  an entity gets the same fix-up for free.
    //
    //  The one case it cannot reach is a script that is not compiled into this
    //  build; ScriptComponent::RetargetReferences says why, and what happens
    //  instead.
    // ======================================================================
    if (const std::size_t moved = ScriptSystem::RetargetRenamed(oldName, newName);
        moved > 0) {
        ENGINE_LOG_INFO(Channels::kScene,
                        "renamed '{}' to '{}', and moved {} script reference(s) with it",
                        oldName, newName, moved);
    }
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
    const std::string sourceTag  = source->Tag();
    const bool        sourceActive = source->IsActiveSelf();
    const EntityId    copyId     = CreateEntity(name);
    Entity*           copy       = Get(copyId);
    if (copy == nullptr) {
        outError = "could not create the copy";
        return EntityId{};
    }

    // The tag comes along. A copy of an enemy is an enemy - the name has to
    // change because names are unique and tags are exactly the label that
    // does not.
    copy->SetTag(sourceTag);
    copy->SetActive(sourceActive);

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
//
//  THIS FILE OWNS THE ENTITIES, Prefab.cpp OWNS THE FORMAT. Everything below
//  is about creating, finding and destroying the entities an instance is made
//  of; the asset file, the diff that finds overrides and the apply/revert
//  operations are all over there. The split is why neither file needs to know
//  much about the other: this one never looks inside a component's JSON, and
//  that one never touches a slot or a generation.
// ---------------------------------------------------------------------------

PrefabInstance* Scene::PrefabInstanceOf(EntityId id) {
    if (!IsValid(id)) {
        return nullptr;
    }
    return m_slots[static_cast<std::size_t>(id.index)].prefab;
}

const PrefabInstance* Scene::PrefabInstanceOf(EntityId id) const {
    if (!IsValid(id)) {
        return nullptr;
    }
    return m_slots[static_cast<std::size_t>(id.index)].prefab;
}

int Scene::PrefabLocalIdOf(EntityId id) const {
    if (!IsValid(id)) {
        return 0;
    }
    return m_slots[static_cast<std::size_t>(id.index)].prefabLocalId;
}

bool Scene::IsPrefabRoot(EntityId id) const {
    if (!IsValid(id)) {
        return false;
    }
    const Slot& slot = m_slots[static_cast<std::size_t>(id.index)];
    return slot.prefab != nullptr && slot.prefabLocalId == slot.prefab->rootLocalId;
}

EntityId Scene::PrefabMember(const PrefabInstance& instance, int localId) {
    for (Slot& slot : m_slots) {
        if (slot.occupied && slot.entity != nullptr && slot.prefab == &instance &&
            slot.prefabLocalId == localId) {
            return slot.entity->Id();
        }
    }
    return EntityId{};
}

EntityId Scene::PrefabRootOf(const PrefabInstance& instance) {
    return PrefabMember(instance, instance.rootLocalId);
}

EntityId Scene::PrefabRootOf(EntityId id) {
    const PrefabInstance* instance = PrefabInstanceOf(id);
    if (instance == nullptr) {
        return EntityId{};
    }
    return PrefabRootOf(*instance);
}

void Scene::ForEachPrefabMember(const PrefabInstance&                            instance,
                                const std::function<void(Entity&, int localId)>& fn) {
    // By index, re-reading the size, for the same reason ForEach is written
    // that way: the callback may create an entity, and adding to m_slots can
    // move the whole list. The Entity itself does not move - it is held by a
    // unique_ptr - so the reference handed to `fn` stays good even then.
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        Slot& slot = m_slots[i];
        if (slot.occupied && slot.entity != nullptr && slot.prefab == &instance) {
            fn(*slot.entity, slot.prefabLocalId);
        }
    }
}

void Scene::ForEachPrefabInstance(const std::function<void(PrefabInstance&)>& fn) {
    // By index for the same reason, and because the callback is allowed to
    // rebuild an instance - which creates and destroys entities.
    //
    // WHAT IT MAY NOT DO is unpack or destroy an instance, because that erases
    // from the very vector being walked. Nothing does; UnpackPrefabInstance is
    // reached from the editor, one instance at a time.
    for (std::size_t i = 0; i < m_prefabInstances.size(); ++i) {
        if (m_prefabInstances[i] != nullptr) {
            fn(*m_prefabInstances[i]);
        }
    }
}

EntityId Scene::InstantiatePrefab(std::string_view virtualPath, std::string& outError) {
    const PrefabAsset* asset = PrefabLibrary::Load(virtualPath, outError);
    if (asset == nullptr) {
        return EntityId{};
    }
    const PrefabEntityNode* rootNode = asset->Find(asset->RootId());
    if (rootNode == nullptr) {
        // PrefabAsset::Parse already refuses an asset whose root is not one of
        // its entities, so this cannot happen - checked anyway, because the
        // alternative to a message is a null dereference.
        outError = "this prefab has no root entity";
        return EntityId{};
    }

    auto held            = std::make_unique<PrefabInstance>();
    held->prefabPath.assign(virtualPath);
    held->rootName       = ReadString(rootNode->node, "name", asset->Name());
    held->rootLocalId    = asset->RootId();

    PrefabInstance& instance = *held;
    m_prefabInstances.push_back(std::move(held));

    const EntityId root = BuildPrefabInstance(instance, outError);
    if (root.IsNull()) {
        // Nothing was built, so there is nothing for the record to describe.
        ForgetPrefabInstance(instance);
        return EntityId{};
    }

    ENGINE_LOG_INFO(Channels::kScene, "placed an instance of '{}' called '{}'",
                    virtualPath, instance.rootName);
    return root;
}

EntityId Scene::InstantiatePrefab(std::string_view virtualPath,
                                  const SpawnOptions& placement, std::string& outError) {
    // The name is dealt with BEFORE the instance exists, because the record
    // carries rootName and BuildPrefabInstance uniquifies against the live
    // scene from it. Renaming afterwards would mean renaming the entity, the
    // record and the name table in step - three places instead of one.
    EntityId root;
    if (placement.nameOverride.empty()) {
        root = InstantiatePrefab(virtualPath, outError);
    } else {
        const PrefabAsset* asset = PrefabLibrary::Load(virtualPath, outError);
        if (asset == nullptr) {
            return EntityId{};
        }
        auto held = std::make_unique<PrefabInstance>();
        held->prefabPath.assign(virtualPath);
        held->rootName    = placement.nameOverride;
        held->rootLocalId = asset->RootId();

        PrefabInstance& instance = *held;
        m_prefabInstances.push_back(std::move(held));

        root = BuildPrefabInstance(instance, outError);
        if (root.IsNull()) {
            ForgetPrefabInstance(instance);
            return EntityId{};
        }
        ENGINE_LOG_INFO(Channels::kScene, "placed an instance of '{}' called '{}'",
                        virtualPath, instance.rootName);
    }

    if (root.IsNull()) {
        return root;
    }

    ApplyPlacement(root, placement);
    return root;
}

void Scene::BuildPrefabInstances(const Json& instancesArray) {
    if (!instancesArray.is_array()) {
        return;
    }

    std::vector<PrefabInstance*> built;
    built.reserve(instancesArray.size());

    for (const Json& node : instancesArray) {
        auto        held = std::make_unique<PrefabInstance>();
        std::string readError;
        if (!ReadInstanceFromJson(node, *held, readError)) {
            ENGINE_LOG_ERROR(Channels::kScene, "{}", readError);
            continue;
        }

        PrefabInstance& instance = *held;
        m_prefabInstances.push_back(std::move(held));
        built.push_back(&instance);

        std::string buildError;
        if (BuildPrefabInstance(instance, buildError).IsNull()) {
            // ==============================================================
            //  THE RECORD IS KEPT EVEN THOUGH NOTHING WAS BUILT.
            //
            //  The usual reason to get here is a prefab that has been moved
            //  or renamed, and the overrides in this record are somebody's
            //  work. Dropping it would mean opening the scene, seeing a
            //  missing enemy, fixing the path - and finding every change ever
            //  made to that instance gone, because saving in between wrote the
            //  scene without it.
            //
            //  So it survives, it is reported, and saving writes it back out
            //  unchanged. See BuildSceneDocument.
            // ==============================================================
            ENGINE_LOG_ERROR(Channels::kScene,
                             "the prefab instance '{}' could not be built: {}",
                             instance.rootName, buildError);
        }
    }

    // ---- and now every root's parent, in a second pass --------------------
    //
    // One instance can be parented under another, and the one it names may
    // not have existed yet when its turn came round. See
    // AttachPrefabInstanceToParent.
    for (PrefabInstance* instance : built) {
        AttachPrefabInstanceToParent(*instance);
    }
}

EntityId Scene::BuildPrefabInstance(PrefabInstance& instance, std::string& outError) {
    const PrefabAsset* asset = PrefabLibrary::Load(instance.prefabPath, outError);
    if (asset == nullptr) {
        return EntityId{};
    }
    instance.rootLocalId = asset->RootId();

    const std::vector<PrefabEntityNode>& entries = asset->Entities();

    // The asset's entities with this instance's overrides already merged in,
    // because components are filled in before they are attached. See
    // EffectiveEntityNodes.
    const std::vector<Json> nodes = EffectiveEntityNodes(*asset, instance);

    std::unordered_map<int, EntityId> created;
    EntityId                          rootId{};

    for (std::size_t i = 0; i < entries.size() && i < nodes.size(); ++i) {
        const PrefabEntityNode& entry  = entries[i];
        const bool              isRoot = entry.localId == instance.rootLocalId;

        // ==================================================================
        //  WHAT THIS MEMBER IS CALLED IN THIS SCENE.
        //
        //  Three sources, in this order: the record's own root name, the
        //  record's name map, then the asset. Then MakeUniqueName, because
        //  three instances of a prefab with a child called "Muzzle" cannot all
        //  have a child called "Muzzle" - names are unique in a scene and are
        //  what parents are recorded by.
        // ==================================================================
        std::string wanted;
        if (isRoot && !instance.rootName.empty()) {
            wanted = instance.rootName;
        } else if (const auto it = instance.names.find(entry.localId);
                   it != instance.names.end()) {
            wanted = it->second;
        } else {
            wanted = ReadString(entry.node, "name", "Entity");
        }

        const std::string name = MakeUniqueName(wanted);

        std::string    entityError;
        const EntityId id = CreateEntityFromJson(nodes[i], name, entityError);
        if (id.IsNull()) {
            ENGINE_LOG_ERROR(Channels::kScene, "prefab '{}': {}", instance.prefabPath,
                             entityError);
            continue;
        }

        Slot& slot         = m_slots[static_cast<std::size_t>(id.index)];
        slot.prefab        = &instance;
        slot.prefabLocalId = entry.localId;

        created[entry.localId] = id;

        if (isRoot) {
            rootId            = id;
            instance.rootName = name;
        } else {
            // THE NAME MAP HOLDS ONLY WHAT DIFFERS from the asset, so that a
            // scene file is not full of names that say nothing. A name that
            // had to be uniquified differs, and is kept for exactly that
            // reason: it has to come back the same way next load, or a plain
            // entity parented under this member would lose its parent.
            const std::string assetName = ReadString(entry.node, "name", "");
            if (name == assetName) {
                instance.names.erase(entry.localId);
            } else {
                instance.names[entry.localId] = name;
            }
        }
    }

    if (rootId.IsNull()) {
        outError = "none of the entities in '" + instance.prefabPath + "' could be created";
        return EntityId{};
    }

    // ---- the tree inside the instance -------------------------------------
    for (const PrefabEntityNode& entry : entries) {
        if (entry.parent == 0) {
            continue;
        }
        const auto child  = created.find(entry.localId);
        const auto parent = created.find(entry.parent);
        if (child == created.end() || parent == created.end()) {
            continue;
        }
        Entity* childEntity  = Get(child->second);
        Entity* parentEntity = Get(parent->second);
        if (childEntity != nullptr && parentEntity != nullptr) {
            // keepWorldTransform stays false. The numbers came out of the
            // asset and are already written relative to this parent, so
            // recomputing them to preserve a world position the entity has
            // never been at would move every child of every prefab.
            childEntity->Transform().SetParent(&parentEntity->Transform());
        }
    }

    AttachPrefabInstanceToParent(instance);

    outError.clear();
    return rootId;
}

void Scene::AttachPrefabInstanceToParent(PrefabInstance& instance) {
    if (instance.parentName.empty()) {
        return;
    }
    Entity* root = Get(PrefabRootOf(instance));
    if (root == nullptr) {
        return;
    }
    Entity* parent = Get(Find(instance.parentName));
    if (parent == nullptr) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "the prefab instance '{}' says its parent is '{}', but there is "
                        "no entity with that name in this scene",
                        instance.rootName, instance.parentName);
        return;
    }
    if (root->Transform().Parent() == &parent->Transform()) {
        return;   // already there; this runs a second time during a load
    }
    root->Transform().SetParent(&parent->Transform());
}

void Scene::DestroyPrefabMembers(const PrefabInstance& instance) {
    // COLLECTED FIRST. Destroying an entity frees a slot and pushes onto the
    // free list, which is the container being walked.
    std::vector<EntityId> members;
    for (const Slot& slot : m_slots) {
        if (slot.occupied && slot.entity != nullptr && slot.prefab == &instance) {
            members.push_back(slot.entity->Id());
        }
    }

    // ======================================================================
    //  THE LINKS GO BEFORE ANYTHING IS DESTROYED, and that is what keeps this
    //  from recursing. DestroyEntityImmediate sends a prefab ROOT back to
    //  DestroyPrefabInstanceImmediate - which is what makes deleting a root
    //  delete the whole instance - and that comes back here.
    // ======================================================================
    for (const EntityId id : members) {
        Slot& slot         = m_slots[static_cast<std::size_t>(id.index)];
        slot.prefab        = nullptr;
        slot.prefabLocalId = 0;
    }

    // ANY ORDER WILL DO. Destroying a parent hands its children back to the
    // world keeping their world positions, and every one of those children is
    // either in this list and about to go, or a plain entity somebody added
    // under the instance - which SHOULD be handed back rather than destroyed.
    for (const EntityId id : members) {
        DestroyEntityImmediate(id);
    }
}

void Scene::ForgetPrefabInstance(PrefabInstance& instance) {
    // EVERY SLOT, not only the occupied ones. A slot that has been freed can
    // still be holding the pointer, and the record is about to be destroyed.
    for (Slot& slot : m_slots) {
        if (slot.prefab == &instance) {
            slot.prefab        = nullptr;
            slot.prefabLocalId = 0;
        }
    }

    const auto it = std::find_if(m_prefabInstances.begin(), m_prefabInstances.end(),
                                 [&instance](const std::unique_ptr<PrefabInstance>& held) {
                                     return held.get() == &instance;
                                 });
    if (it != m_prefabInstances.end()) {
        m_prefabInstances.erase(it);
    }
}

bool Scene::UnpackPrefabInstance(EntityId member) {
    PrefabInstance* instance = PrefabInstanceOf(member);
    if (instance == nullptr) {
        return false;
    }
    // Copied before the record goes, for the message.
    const std::string path = instance->prefabPath;
    const std::string name = instance->rootName;

    // NOTHING IS DESTROYED AND NOTHING MOVES. The entities stay exactly where
    // they are and keep every component they have; what goes is the link, so
    // they are written out as ordinary entities from now on and the asset no
    // longer reaches them. That is why Unity calls it unpacking.
    ForgetPrefabInstance(*instance);

    ENGINE_LOG_INFO(Channels::kScene,
                    "unpacked '{}' - its entities are no longer linked to {}", name, path);
    return true;
}

bool Scene::DestroyPrefabInstanceImmediate(EntityId member) {
    PrefabInstance* instance = PrefabInstanceOf(member);
    if (instance == nullptr) {
        return false;
    }
    DestroyPrefabMembers(*instance);
    ForgetPrefabInstance(*instance);
    return true;
}

bool Scene::RebuildPrefabInstance(PrefabInstance& instance, std::string& outError) {
    // THE RECORD HAS TO DESCRIBE WHERE THE THING IS before its entities go.
    // An instance that was renamed or dragged onto a new parent since it was
    // built would otherwise come back under the old one, because that is what
    // the record still says. See RefreshPrefabPlacement.
    RefreshPrefabPlacement(*this, instance);

    DestroyPrefabMembers(instance);
    return !BuildPrefabInstance(instance, outError).IsNull();
}

std::size_t Scene::RebuildPrefabInstances(std::string_view virtualPath,
                                          std::string& outError) {
    const std::string path(virtualPath);

    // DROPPED FROM THE CACHE FIRST, so that the rebuilds below read the file
    // as it is now. The overrides each instance is holding were captured
    // against the asset as it WAS - by the caller, before it changed the file.
    // See the note on this function in Scene.h.
    PrefabLibrary::Forget(path);

    std::vector<PrefabInstance*> targets;
    for (const std::unique_ptr<PrefabInstance>& held : m_prefabInstances) {
        if (held != nullptr && held->prefabPath == path) {
            targets.push_back(held.get());
        }
    }

    std::size_t rebuilt = 0;
    for (PrefabInstance* instance : targets) {
        std::string error;
        if (RebuildPrefabInstance(*instance, error)) {
            ++rebuilt;
            continue;
        }
        ENGINE_LOG_ERROR(Channels::kScene,
                         "could not rebuild the prefab instance '{}': {}",
                         instance->rootName, error);
        if (outError.empty()) {
            outError = error;
        }
    }
    return rebuilt;
}

} // namespace eng
