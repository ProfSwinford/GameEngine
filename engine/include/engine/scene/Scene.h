#pragma once

// =============================================================================
//  WEEK 9 - the scene: a world built entirely from a data file.
//
//  *** THE MILESTONE 3 BAR, STATED EXACTLY: ***
//    A scene file describing 20+ entities with transform and sprite components
//    loads and renders with ZERO HARDCODED CONTENT ANYWHERE IN THE C++.
//
//  The test is mechanical and self-administered: grep the C++ for the name of
//  any entity in the scene, any specific position, any texture filename. The
//  searches and their (empty) output are in docs/week09-milestone3.md.
//
//  The honest version: could you hand the scene file to someone who cannot
//  write C++, have them add three entities, and have it work? Yes - the
//  factory turns a type name into a component and Deserialize reads every
//  field, so a new entity needs no C++ at all.
//
//  Uses the SAME loading path as Week 8's config: ConfigDocument and
//  ConfigNode. Week 9 was a schema, not a subsystem, which is exactly what
//  Week 8's note promised.
//
//  ---------------------------------------------------------------------------
//  SLOTS AND GENERATIONS. The scene owns a dense array of slots; each slot
//  holds an Entity and a generation counter. Destroying an entity increments
//  its generation and pushes the index onto a free list, so every outstanding
//  handle to it becomes detectably stale rather than dangling.
//
//  The free list is FIFO, not LIFO: indices are reused in the order they were
//  freed rather than newest-first. That spreads reuse across the whole slot
//  array instead of hammering whichever slot was freed last, which is what
//  keeps the 12-bit generation counter in Handle.h away from its wrap point.
//  See the bit-split note there.
// =============================================================================

#include <engine/core/Config.h>
#include <engine/scene/Entity.h>

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng {

class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    // Parses the file, creates entities, attaches components, acquires the
    // resources they name. Errors report the ENTITY and the FIELD that failed.
    bool Load(std::string_view virtualPath, std::string& outError);

    // Writes the live scene back out as JSON.
    //
    // ROUND TRIP: `Load -> Save -> Load` is a fixed point. Every component's
    // Serialize writes the same keys its Deserialize reads, and parenting is
    // recovered from the transform tree rather than from whatever the source
    // file happened to say - so an entity reparented in the editor saves
    // correctly.
    //
    // PRESERVES UNKNOWN TOP-LEVEL KEYS from the file it is overwriting -
    // `_comment`, `prefabs`, anything a newer build understands and this one
    // does not. Only `name`, `camera` and `entities` are regenerated. Same
    // policy as the CVar panel's Save, and for the same reason: a tool that
    // silently drops the parts of a file it did not understand is a tool people
    // stop trusting with their files.
    //
    // A component whose Serialize returns false is REPORTED by name and
    // skipped, so a lossy save says so.
    bool Save(std::string_view virtualPath, std::string& outError);

    // The same document, to and from a string instead of a file.
    //
    // This is what makes the editor's Play button NON-DESTRUCTIVE: snapshot
    // before Play, restore on Stop, so a play session that moves the player and
    // destroys half the pickups leaves the authored scene exactly as it was.
    // Unity does this and everyone relies on it without noticing until an
    // editor does not.
    //
    // It costs one JSON document per Play rather than a bespoke undo system,
    // which is a good trade for something that already has to be correct
    // because the file format depends on it.
    bool SaveToString(std::string& outText, std::string& outError);
    bool LoadFromString(std::string_view text, std::string& outError);

    // Destroys every entity and RELEASES EVERY RESOURCE. After this,
    // ResourceManager::TotalRefCount() reads ZERO - that is the milestone
    // check, and it is done live on the resource panel.
    void Unload();

    const std::string& Name() const { return m_name; }

    // The path this scene was LAST LOADED FROM. Survives Unload() on purpose,
    // because "reload what I just unloaded" needs it and an unloaded scene has
    // not forgotten where it came from. Empty only before the first load.
    const std::string& SourcePath() const { return m_sourcePath; }

    bool IsLoaded() const { return m_liveCount > 0; }

    // --- entities ---------------------------------------------------------
    EntityHandle CreateEntity(std::string_view name);

    // IMMEDIATE destruction. Correct at load time and from Scene::Unload;
    // gameplay code must go through DeferredOps::QueueDestroy instead, because
    // destroying an entity while a system is iterating its component array is
    // precisely the iterator-invalidation bug Week 10 exists to prevent. The
    // editor's Inspector delete button goes through the queue too - the IDE is
    // not exempt from the engine's rules.
    void DestroyEntityImmediate(EntityHandle handle);

    // --- editing operations, all used by the editor's Hierarchy panel ------
    //
    // Renaming has to go through the scene rather than through Entity::SetName
    // directly, because the scene keeps a name -> handle map and a rename done
    // behind its back leaves that map pointing at the old key. Returns false if
    // the name is already taken.
    bool RenameEntity(EntityHandle handle, std::string_view newName);

    // Serializes the entity and rebuilds it, so a duplicate is a genuine deep
    // copy of every component rather than a shallow one somebody has to
    // remember to extend when a component type is added. Children are NOT
    // duplicated - copying a subtree silently is rarely what was meant.
    EntityHandle DuplicateEntity(EntityHandle handle, std::string& outError);

    // Appends a numeric suffix until the name is free. Gameplay spawning fifty
    // bullets should not have to invent fifty names, and neither should a
    // person clicking "Create Entity" fifty times.
    std::string MakeUniqueName(std::string_view base) const;

    Entity*      Get(EntityHandle handle);
    bool         IsValid(EntityHandle handle) const;
    EntityHandle Find(StringId name) const;
    EntityHandle Find(std::string_view name) const;

    void  ForEach(const std::function<void(Entity&)>& fn);
    usize EntityCount() const { return m_liveCount; }

    // --- prefabs ----------------------------------------------------------
    // A named entity template from the scene file's "prefabs" section, used by
    // DeferredOps::QueueSpawn. The document is kept alive by the scene for
    // exactly this reason - a prefab is a ConfigNode into it.
    bool       HasPrefab(StringId name) const;
    ConfigNode Prefab(StringId name) const;
    EntityHandle InstantiatePrefab(StringId prefab, std::string_view name,
                                   std::string& outError);

    // Builds one entity from a ConfigNode of the scene-file entity shape. Used
    // by Load and by InstantiatePrefab, and public because a gameplay layer
    // that generates entities from its own data should not have to reimplement
    // it.
    EntityHandle CreateEntityFromNode(const ConfigNode& node, std::string_view nameOverride,
                                      std::string& outError);

    // The camera position and zoom the scene file asked for, so the sandbox
    // can apply them without knowing what a scene file looks like.
    Vec2 InitialCameraPosition() const { return m_cameraPosition; }
    f32  InitialCameraZoom() const     { return m_cameraZoom; }

    // Pushed back in before a save, so framing a shot in the editor and saving
    // keeps the framing. Engine::SaveScene does this from the live camera,
    // which is the mirror of Engine::LoadScene applying the scene's camera to
    // the live one.
    void SetCameraState(Vec2 position, f32 zoom) {
        m_cameraPosition = position;
        m_cameraZoom     = zoom;
    }

    // The scene the engine is currently running. Set by the scene subsystem.
    static Scene* Active();
    static void   SetActive(Scene* scene);

private:
    struct Slot {
        std::unique_ptr<Entity> entity;
        u32                     generation = 1;   // never 0 - see Handle.h
        bool                    occupied   = false;
    };

    void ResolveParents(const ConfigNode& entitiesNode);

    // Shared by Load (file) and LoadFromString (Play-mode snapshot), so a
    // restore takes exactly the same path as a load rather than a parallel one
    // that can drift.
    bool BuildFromDocument(std::string& outError);

    // The document BUILDER shared by Save and SaveToString is deliberately NOT
    // a member: it would have to name the parser's type in its signature, and
    // no public header in this engine mentions JSON. It lives as a free
    // function in Scene.cpp and is handed the few private fields it needs.

    std::vector<Slot>                     m_slots;
    std::deque<u32>                       m_freeIndices;   // FIFO - see the header
    std::unordered_map<u64, EntityHandle> m_byName;
    usize                                 m_liveCount = 0;

    ConfigDocument m_document;      // kept alive: prefabs are nodes into it
    std::string    m_name;
    std::string    m_sourcePath;
    Vec2           m_cameraPosition{0.0f, 0.0f};
    f32            m_cameraZoom = 1.0f;
};

} // namespace eng
