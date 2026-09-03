#pragma once

// ============================================================================
//  Scene.h - a level: the entities in it, loaded from and saved to a file.
//
//  A scene file is JSON and looks like this:
//
//      {
//        "name": "Level 1",
//        "camera": { "position": [0, 0], "zoom": 1.0 },
//        "entities": [
//          {
//            "name": "Player",
//            "components": [
//              { "type": "TransformComponent", "position": [0, 0] },
//              { "type": "SpriteComponent", "texture": "textures/player.bmp" }
//            ]
//          }
//        ]
//      }
//
//  NOTHING ABOUT ANY PARTICULAR GAME IS IN THE C++. No entity name, no
//  position, no texture filename. Somebody who cannot write C++ can add three
//  entities to a level by editing the file, and it works. That is the point of
//  the whole component model, and it is easy to check: search the engine's
//  source for the name of anything in a scene and you will not find it.
//
//  SLOTS AND GENERATIONS
//  The scene owns a list of slots; each holds one entity and a generation
//  number. Destroying an entity increases its slot's generation and puts the
//  slot back on a free list, so every EntityId still referring to it can be
//  recognised as out of date instead of quietly pointing at whatever moved in.
//  See EntityId.h.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/scene/Entity.h>

#include <functional>
#include <memory>
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

    // ---- loading and saving ----------------------------------------------

    // Reads the file, creates the entities, attaches their components and
    // loads the images they name. Problems are reported naming the ENTITY and
    // the FIELD, because that is what tells you which line to fix.
    bool Load(std::string_view virtualPath, std::string& outError);

    // Writes the live scene back out.
    //
    // Load -> Save -> Load gives back exactly what you started with: every
    // component writes the same keys its Deserialize reads, and parent/child
    // relationships are taken from the live transform tree rather than from
    // whatever the original file said - so an entity reparented in the editor
    // saves correctly.
    bool Save(std::string_view virtualPath, std::string& outError);

    // The same document to and from a string instead of a file.
    //
    // This is what makes the editor's Play button non-destructive: the scene
    // is snapshotted into a string before Play and restored from it on Stop,
    // so a play session that moved the player and collected half the pickups
    // leaves the level exactly as it was authored. Unity does this, and
    // everybody relies on it without noticing until an editor does not.
    bool SaveToString(std::string& outText, std::string& outError);
    bool LoadFromString(std::string_view text, std::string& outError);

    // Destroys every entity, which releases every texture they were using.
    void Unload();

    const std::string& Name() const { return m_name; }

    // The file this scene was last loaded from. Deliberately survives Unload,
    // because "reload what I just unloaded" needs it.
    const std::string& SourcePath() const { return m_sourcePath; }

    bool IsLoaded() const { return m_liveCount > 0; }

    // ---- entities ---------------------------------------------------------
    EntityId CreateEntity(std::string_view name);

    // Destroys an entity RIGHT NOW.
    //
    // This is correct while loading and while unloading. Game code should use
    // DeferredOps::QueueDestroy instead, because destroying an entity while a
    // system is in the middle of walking its list of components is how you end
    // up reading memory that has just been freed. See DeferredOps.h.
    void DestroyEntityImmediate(EntityId id);

    // ---- editing, used by the Hierarchy panel -----------------------------

    // Renaming goes through the scene rather than through Entity::SetName
    // directly, because the scene keeps a name-to-entity table and a rename
    // done behind its back would leave that table pointing at the old name.
    // Returns false if something else already has that name.
    bool RenameEntity(EntityId id, std::string_view newName);

    // Copies an entity by writing it out and reading it straight back in. Done
    // that way rather than by copying members one at a time, because the
    // hand-written version needs extending every time somebody adds a
    // component type - and will be forgotten exactly once.
    //
    // Children are NOT copied; silently duplicating a whole subtree is rarely
    // what was meant.
    EntityId DuplicateEntity(EntityId id, std::string& outError);

    // Adds a number to the end of a name until it is free. Clicking "Create
    // Entity" fifty times should not require inventing fifty names.
    std::string MakeUniqueName(std::string_view base) const;

    Entity*  Get(EntityId id);
    bool     IsValid(EntityId id) const;
    EntityId Find(std::string_view name) const;

    void        ForEach(const std::function<void(Entity&)>& fn);
    std::size_t EntityCount() const { return m_liveCount; }

    // Builds one entity from a chunk of JSON in the scene-file entity shape.
    // Public because game code that generates its own entities from its own
    // data should not have to reimplement it.
    EntityId CreateEntityFromJson(const Json& node, std::string_view nameOverride,
                                  std::string& outError);

    // The camera position and zoom the scene file asked for, so the game can
    // apply them without knowing what a scene file looks like.
    Vec2  InitialCameraPosition() const { return m_cameraPosition; }
    float InitialCameraZoom() const     { return m_cameraZoom; }

    // Pushed back in before saving, so framing a shot in the editor and
    // pressing save keeps the framing.
    void SetCameraState(Vec2 position, float zoom) {
        m_cameraPosition = position;
        m_cameraZoom     = zoom;
    }

    // The scene the engine is currently running.
    static Scene* Active();
    static void   SetActive(Scene* scene);

private:
    struct Slot {
        std::unique_ptr<Entity> entity;
        int                     generation = 1;   // never 0; see EntityId.h
        bool                    occupied   = false;
    };

    void ResolveParents(const Json& entitiesArray);

    // Shared by Load (from a file) and LoadFromString (from a Play snapshot),
    // so restoring a snapshot takes exactly the same path as a normal load and
    // the two cannot drift apart.
    bool BuildFromDocument(std::string& outError);

    std::vector<Slot> m_slots;
    std::vector<int>  m_freeIndices;   // slots that can be reused

    // Name -> entity, so Find() does not have to search every slot.
    std::unordered_map<std::string, EntityId> m_byName;

    std::size_t m_liveCount = 0;

    // The parsed scene file is kept because prefabs are read out of it on
    // demand rather than copied at load time.
    Json        m_document = Json::object();
    std::string m_name;
    std::string m_sourcePath;
    Vec2        m_cameraPosition{0.0f, 0.0f};
    float       m_cameraZoom = 1.0f;
};

} // namespace eng
