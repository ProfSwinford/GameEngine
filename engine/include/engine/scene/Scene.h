#pragma once

// ============================================================================
//  Scene.h - a level: the entities in it, loaded from and saved to a file.
//
//  A SCENE FILE IS NAMED .oscene AND IS JSON INSIDE. Those are two separate
//  facts and both matter. The contents are ordinary JSON, so any text editor
//  will colour it and any JSON tool will read it; the EXTENSION is the
//  engine's own, so that "which of these files is a level?" is answered by the
//  name rather than by opening every .json under assets/ to find out. Texture
//  and spritesheet metadata are also JSON, and without a distinct extension
//  there would be no way to tell a level from a sprite description.
//
//  kSceneExtension below is the one place that spelling lives.
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
//              { "type": "SpriteComponent", "texture": "textures/player.png" }
//            ]
//          }
//        ],
//        "prefabInstances": [
//          { "prefab": "prefabs/enemy.oprefab", "name": "Enemy_3",
//            "overrides": [
//              { "entity": 1, "component": "TransformComponent",
//                "field": "position", "value": [120, 40] }
//            ] }
//        ]
//      }
//
//  THE SECOND LIST IS NOT MORE ENTITIES. A prefab instance is a short record -
//  which prefab, where, and what was changed about this copy - and its
//  entities are BUILT from the prefab as the scene loads. That is what makes
//  editing a prefab reach every instance of it, and it is why the members are
//  deliberately NOT written to this file as well. See scene/Prefab.h.
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
// Component.h, not just Entity.h. The templates at the bottom of this file ask
// a component for its TypeName() and walk a Transform2D's children, so both
// types have to be complete here rather than merely named. Neither includes
// this file back, so there is no cycle - and Engine.h pulls both in anyway.
#include <engine/scene/Component.h>
// For IsPendingDestroy, which the scene-wide searches below skip by default -
// see rule 1 in that header. It only forward-declares Scene, so no cycle.
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Prefab.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng {

// The file extension a level is saved with, dot included.
//
// IT LIVES IN ONE PLACE ON PURPOSE. Before this constant existed the spelling
// was written out separately in the editor's "list the scenes" filter and in
// its "what kind of file is this?" classifier, and the two were changed at
// different times - so the Assets panel recognised a scene that the Open Scene
// menu could not find. Two copies of a fact are two chances to be wrong, and
// nothing reports the disagreement.
//
// Code that MATCHES or GENERATES a scene name uses this. A specific file that
// exists on disk - "scenes/collector.oscene" - is still written out in full,
// because renaming the extension would mean renaming that file anyway.
inline constexpr const char* kSceneExtension = ".oscene";

// ---------------------------------------------------------------------------
//  Where a newly spawned thing goes.
//
//  Unity spells this as overloads - Instantiate(prefab), Instantiate(prefab,
//  position, rotation), Instantiate(prefab, parent) and so on. A struct is
//  used here instead, for the reason physics/Physics2D.h gives for
//  QueryFilter: a call with three of five arguments left out is unreadable at
//  the call site, and the day somebody adds a sixth, every existing call
//  silently means something slightly different.
//
//  Named initialisers make it read better than the overloads anyway:
//
//      Instantiate("prefabs/bullet.oprefab", {
//          .position        = Transform()->WorldPosition(),
//          .rotationRadians = Transform()->WorldRotation(),
//      });
//
//  EVERY FIELD HAS THE VALUE "DON'T TOUCH IT" AS ITS DEFAULT, so a default
//  SpawnOptions{} places a prefab exactly where the asset says - which is
//  what Instantiate with no placement should do.
// ---------------------------------------------------------------------------
struct SpawnOptions {
    // A prefab asset carries its own position, rotation and scale, and so
    // does CreateEntityFromJson. OVERRIDING THEM HAS TO BE OPT-IN, or a
    // default-constructed SpawnOptions would slam everything to the origin at
    // scale (1,1) and the no-placement spelling would be the destructive one.
    //
    // So each of the three is a flag plus a value rather than a value alone.
    // The Set* helpers below mean no caller has to remember that.
    bool  overridePosition = false;
    Vec2  position{0.0f, 0.0f};

    bool  overrideRotation = false;
    float rotationRadians  = 0.0f;   // anticlockwise, as everywhere else

    bool  overrideScale    = false;
    Vec2  scale{1.0f, 1.0f};

    // Null means "at the top level". A parent that no longer exists is
    // reported rather than ignored - spawning something under a dead entity is
    // a mistake worth hearing about, and the thing is still spawned.
    EntityId parent{};

    // Whether the position above is a WORLD position that should survive being
    // parented, or a local one measured against the new parent.
    //
    // TRUE BY DEFAULT, and the opposite of Transform2D::SetParent's default.
    // That is deliberate: `.position = muzzle->WorldPosition()` is what a
    // spawner actually writes, and reinterpreting that as parent-relative
    // would put the bullet in a different place for every gun. SetParent's
    // default suits the editor, where dragging in the Hierarchy should keep
    // the numbers; this suits spawning, where it should keep the place.
    bool positionIsWorldSpace = true;

    // A name to use instead of the asset's. It goes through MakeUniqueName
    // either way, so passing "Bullet" fifty times gives fifty distinct names.
    std::string nameOverride;

    // ---- the spellings that read well at a call site ---------------------
    SpawnOptions& At(Vec2 worldPosition) {
        overridePosition = true;
        position         = worldPosition;
        return *this;
    }
    SpawnOptions& Facing(float radians) {
        overrideRotation = true;
        rotationRadians  = radians;
        return *this;
    }
    SpawnOptions& Scaled(Vec2 factor) {
        overrideScale = true;
        scale         = factor;
        return *this;
    }
    SpawnOptions& Under(EntityId parentEntity) {
        parent = parentEntity;
        return *this;
    }
    SpawnOptions& Named(std::string_view name) {
        nameOverride = name;
        return *this;
    }
};

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

    // ======================================================================
    //  THE SAME DOCUMENT TO AND FROM A STRING INSTEAD OF A FILE.
    //
    //  THE CONTENTS ONLY. These two carry every entity, every component and
    //  the scene's name, and they deliberately do NOT touch SourcePath() -
    //  see LoadFromString's own comment for why, and TakePlaySnapshot below
    //  for the one caller that needs the other behaviour.
    //
    //  The editor's Ctrl+Z is built on this pair: an undo step is the scene's
    //  text, and stepping back must not re-point the file. Save As followed by
    //  Ctrl+Z would otherwise send the scene back to the file it was saved
    //  away from. See editor/src/UndoStack.h.
    // ======================================================================
    bool SaveToString(std::string& outText, std::string& outError);
    bool LoadFromString(std::string_view text, std::string& outError);

    // ======================================================================
    //  AND THE WHOLE SCENE, IDENTITY INCLUDED - which is what Play needs.
    //
    //  ----------------------------------------------------------------------
    //  WHY THIS IS NOT JUST SaveToString, AND WHAT IT COST TO FIND OUT
    //
    //  Play used to be SaveToString on the way in and LoadFromString on the
    //  way out, and for a session that stays in one scene those are exactly
    //  right. A session that CHANGES scene - a menu button calling
    //  Engine::RequestScene, which is what the feature is for - broke it,
    //  silently and expensively:
    //
    //      Scene::Load, which carries out the request, ASSIGNS m_sourcePath.
    //      LoadFromString, which puts the snapshot back, deliberately does
    //      not. So Stop restored the menu's entities under the LEVEL's
    //      filename, every one of the editor's six "which scene is open"
    //      readouts agreed that the level was open, and the next Ctrl+S
    //      wrote the menu over the level. The level was gone, and the only
    //      sign beforehand was a window title nobody had reason to doubt.
    //
    //  It was reported by somebody who lost an afternoon's level to it.
    //
    //  ----------------------------------------------------------------------
    //  SO THE SNAPSHOT CARRIES THE PATH, AND IT IS NOT IN THE TEXT.
    //
    //  The path is a FIELD BESIDE the text, not a key inside it. Scene::Save
    //  and SaveToString build the same document - that is a deliberate
    //  property, so the file-writing path is exercised on every Play rather
    //  than only when somebody saves - and a path written into the document
    //  would therefore be written into every .oscene on disk. A scene file
    //  that names its own location is a scene file that lies the moment it is
    //  copied, renamed or moved: loading a duplicate would hand it the
    //  original's identity and the next save would overwrite the original.
    // ======================================================================
    struct PlaySnapshot {
        std::string text;

        // The file the scene was being edited as when the snapshot was taken.
        // Empty is meaningful and must survive: it is a scene that has never
        // been saved anywhere, and restoring a path onto it would make the
        // first Ctrl+S overwrite a file nobody chose. StartNewScene clears
        // the path for that exact reason; this must not put one back.
        std::string sourcePath;

        bool Empty() const { return text.empty(); }
    };

    // Takes one. Fails for the same reason SaveToString does - a component
    // that cannot be written down - so that Play is refused rather than
    // entered without a way back.
    bool TakePlaySnapshot(PlaySnapshot& outSnapshot, std::string& outError);

    // Puts one back, CONTENTS AND IDENTITY TOGETHER.
    //
    // ON FAILURE THE SCENE IS LEFT WITH NO FILE. A restore that did not parse
    // leaves entities that match nothing on disk, and the two honest choices
    // are both bad - point at the pre-play file and Ctrl+S overwrites it with
    // wreckage, point at the played file and Ctrl+S overwrites that instead.
    // So it points at neither: Save has nowhere to go and asks for a name,
    // which is the one outcome that cannot destroy a file somebody still has.
    bool RestorePlaySnapshot(const PlaySnapshot& snapshot, std::string& outError);

    // Destroys every entity, which releases every texture they were using.
    // ======================================================================
    //  WHAT AN UNLOAD IS ALLOWED TO LEAVE BEHIND.
    //
    //  Everything, by default - which is what every caller written before
    //  persistence existed meant, and is why the parameter has a default
    //  rather than every call site being revisited.
    //
    //  Only Scene::Load asks for KeepPersistent, because only a scene CHANGE
    //  is the thing DontDestroyOnLoad is about. See SetPersistent for the
    //  whole list and why each one is what it is.
    // ======================================================================
    enum class UnloadScope {
        Everything,
        KeepPersistent,
    };

    void Unload(UnloadScope scope = UnloadScope::Everything);

    // ---- starting a scene from nothing ------------------------------------
    //
    // Empties the scene AND FORGETS THE FILE IT CAME FROM, which is the whole
    // difference from Unload and the reason this is not a flag on it.
    //
    // Unload means "put this away": the path survives, because "reload what I
    // just unloaded" needs it. This means "start something else", so the next
    // save has to be told where to go - otherwise File -> New Scene would
    // leave Ctrl+S pointing at the file you were previously editing, and the
    // first save of a brand new scene would silently overwrite it.
    //
    // This is what makes a freshly created project usable. Project -> New
    // Project deliberately creates no scene, and until this existed the only
    // route to the first one was to add an object and notice that Save As had
    // become available.
    void StartNewScene();

    const std::string& Name() const { return m_name; }

    // The file this scene was last loaded from. Deliberately survives Unload,
    // because "reload what I just unloaded" needs it.
    const std::string& SourcePath() const { return m_sourcePath; }

    // ======================================================================
    //  THE SAME SCENE, AT A DIFFERENT PATH.
    //
    //  FOR A RENAME OR A MOVE OF THE OPEN SCENE'S OWN FILE, and for nothing
    //  else. The editor renames the file on disk; without this the scene in
    //  memory goes on believing it came from the old name, so the next Ctrl+S
    //  writes the file straight back under it - leaving two copies of the
    //  level and a rename that appears to have undone itself.
    //
    //  IT DOES NOT LOAD, SAVE OR TOUCH THE CONTENTS. The entities are already
    //  correct; only the name of the file they will be written to is wrong.
    //  That is why this is a setter rather than a reload: a reload would throw
    //  away unsaved work to fix a filename.
    // ======================================================================
    void SetSourcePath(std::string_view virtualPath) {
        m_sourcePath = std::string(virtualPath);
    }

    // ======================================================================
    //  "HAS AT LEAST ONE ENTITY", which is NOT the same as "a scene is open".
    //
    //  Two different questions, and using this one for the other cost a file.
    //  See IsOpen below, and do not reach for this to gate a save.
    // ======================================================================
    bool IsLoaded() const { return m_liveCount > 0; }

    // ======================================================================
    //  IS THERE A SCENE HERE AT ALL?
    //
    //  False after Unload - File -> Close Scene - and true from a Load or a
    //  StartNewScene. It exists because the source path deliberately SURVIVES
    //  an unload ("reload what I just unloaded" needs it), so after Close
    //  Scene the path still names a real asset while there is nothing to
    //  write to it. Every save route that asked only whether the path was
    //  empty therefore offered to save, and saving serialised the zero live
    //  entities straight over the file.
    //
    //  AN EMPTY SCENE IS STILL OPEN, which is the whole reason this is not
    //  IsLoaded(). Emptying a scene on purpose and saving it is a thing
    //  people do, and the File menu records a bug where IsLoaded() stopped
    //  them - see the note on Save Scene in EditorApp::DrawFileMenu.
    // ======================================================================
    bool IsOpen() const { return m_open; }

    // ---- entities ---------------------------------------------------------

    // Creates an entity with EXACTLY this name.
    //
    // ==================================================================
    //  THIS IS THE LOADER'S FUNCTION, NOT GAME CODE'S. It does not
    //  de-duplicate, and it cannot: a scene file's names are already settled,
    //  and a load that quietly turned "Player" into "Player_1" would detach
    //  every parent link and every entity reference that names it.
    //
    //  SPAWNING CODE WANTS CreateEntityUniquelyNamed BELOW. Call this twice
    //  with "Bullet" and there are two live entities with one name, which is
    //  not a tidiness problem - names are an IDENTITY here:
    //
    //    Find("Bullet") answers for whichever won the name table;
    //    every EntityRef naming it points at that same one;
    //    ResolveParents reparents BOTH of the second one's children onto it
    //    on the next load;
    //    and destroying EITHER erases the shared table entry, so Find then
    //    answers for NEITHER while one is still alive.
    //
    //  That shipped as a bug once already - see the post-mortem in
    //  editor/src/panels/TilePalettePanel.cpp. So the collision is now
    //  REPORTED, at Error level, naming both the name and the fix. It is not
    //  refused: refusing would mean a loader that hits a malformed file with
    //  two identical names loses an entity, and a message plus a usable scene
    //  beats silence plus a broken one.
    // ==================================================================
    EntityId CreateEntity(std::string_view name);

    // Creates an entity, putting a number on the end of the name until it is
    // free. THIS IS THE ONE GAME CODE AND SCRIPTS SHOULD CALL - see the note
    // above for what the other one costs when it is used for spawning.
    EntityId CreateEntityUniquelyNamed(std::string_view baseName);
    EntityId CreateEntityUniquelyNamed(std::string_view     baseName,
                                       const SpawnOptions&  placement);

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

    // The same, placed - which is what Unity's Instantiate(someSceneObject,
    // position, rotation) is. It exists so that a script cloning a scene
    // object can place the copy through the SAME SpawnOptions that placing a
    // prefab uses: two ways of saying "put it here" would eventually
    // disagree about what a parent or a local position means.
    EntityId DuplicateEntity(EntityId id, const SpawnOptions& placement,
                             std::string& outError);

    // Adds a number to the end of a name until it is free. Clicking "Create
    // Entity" fifty times should not require inventing fifty names.
    std::string MakeUniqueName(std::string_view base) const;

    Entity*       Get(EntityId id);
    const Entity* Get(EntityId id) const;
    bool          IsValid(EntityId id) const;
    EntityId      Find(std::string_view name) const;

    // The entity itself rather than its id, which is what calling code almost
    // always wanted. Find hands back an id that then has to go straight back
    // into Get, and `scene->Get(scene->Find("Player"))` is a spelling nobody
    // should have to write - or read.
    //
    // Hold the ID if it has to survive the frame, and the POINTER only within
    // it. That rule has not changed; this just removes a step from the common
    // case where it never leaves the function.
    Entity*       FindEntity(std::string_view name);
    const Entity* FindEntity(std::string_view name) const;

    // ---- from a transform back to its entity ------------------------------
    //
    // Transform2D::Children() hands back TRANSFORMS, and that was a dead end:
    // engine/math deliberately knows nothing about entities, so there was no
    // way to get from a child's transform to the components beside it. "The
    // sprite on my first child" had no spelling at all, and the two places in
    // this codebase that needed it both paid for the gap - the Hierarchy panel
    // ran a whole Scene::ForEach per child, O(n^2) over a subtree, and
    // Prefab.cpp built a temporary map over every entity in the scene.
    //
    // ==================================================================
    //  WHY THE TABLE LIVES HERE AND NOT ON Transform2D
    //
    //  The obvious fix is an EntityId member on the transform. It was
    //  rejected: engine/math/Transform2D.h includes nothing from engine/scene
    //  and that independence is worth more than the hash lookup it would save.
    //  A transform is a node in a tree with a position; it is used by things
    //  that are not entities and it should stay usable by them.
    //
    //  The SCENE is the thing whose job is knowing which entity is which, so
    //  the scene keeps the table. It is filled in by TransformComponent's
    //  OnAttach and OnDetach - the one place in the engine where a transform
    //  and the entity owning it are both in hand - which is also why there is
    //  exactly one writer and no way for game code to repoint it.
    // ==================================================================
    //
    // Null for a transform that is not part of an entity in this scene, which
    // is an ordinary answer rather than an error: a Transform2D built on the
    // stack in a test belongs to nothing.
    Entity*       EntityOf(const Transform2D& transform);
    const Entity* EntityOf(const Transform2D& transform) const;
    EntityId      IdOf(const Transform2D& transform) const;

    // ---- by tag -----------------------------------------------------------
    //
    // See Entity::Tag for what a tag is and the three identities it is not.
    // Both of these SKIP anything already queued for destruction by default,
    // because rule 1 in DeferredOps.h means a destroyed entity is still there
    // for the rest of the frame - and "how many enemies are left" answered
    // with the one that died this tick is the bug that rule creates.
    Entity*     FindByTag(std::string_view tag, bool skipPendingDestroy = true);
    std::size_t ForEachWithTag(std::string_view                    tag,
                               const std::function<void(Entity&)>& fn,
                               bool skipPendingDestroy = true);
    std::size_t CountWithTag(std::string_view tag, bool skipPendingDestroy = true);

    void        ForEach(const std::function<void(Entity&)>& fn);
    std::size_t EntityCount() const { return m_liveCount; }

    // ForEach with a way out: return false from the callback to stop. True if
    // the walk ran to the end, false if the callback stopped it - so "did I
    // find one" needs no captured flag.
    //
    // ForEach has no early exit, so every "find the first entity like this"
    // written with it visits the whole scene however early it succeeds.
    bool ForEachUntil(const std::function<bool(Entity&)>& fn);

    // ---- scene-wide search by component type ------------------------------
    //
    // Unity's FindObjectOfType / FindObjectsOfType. Defined at the bottom of
    // this file.
    //
    // THEY DO NOT FIND SCRIPTS. Every script's component reports its type name
    // as "ScriptComponent", so FindFirstWith<Chaser>() cannot compile and
    // FindFirstWith<ScriptComponent>() finds an arbitrary script. Use
    // FindScriptIn<T> / FindScriptNamedIn from scene/ScriptComponent.h, which
    // ask each behaviour what it really is.
    template <class T> Entity* FindFirstWith(bool skipPendingDestroy = true);
    template <class T>
    typename FindTargetOf<T>::type* FindFirstComponent(
        bool skipPendingDestroy = true);

    template <class T>
    std::size_t ForEachWith(
        const std::function<void(Entity&, typename FindTargetOf<T>::type&)>& fn,
        bool skipPendingDestroy = true);
    template <class T>
    std::size_t CountWith(bool skipPendingDestroy = true);

    // Builds one entity from a chunk of JSON in the scene-file entity shape.
    // Public because game code that generates its own entities from its own
    // data should not have to reimplement it.
    EntityId CreateEntityFromJson(const Json& node, std::string_view nameOverride,
                                  std::string& outError);

    // ---- prefabs ----------------------------------------------------------
    //
    // A PREFAB INSTANCE IS NOT A SET OF ENTITIES IN THIS FILE. It is a short
    // record - which asset, where, and what was changed - and the entities
    // are built from the asset. That is what makes editing the asset reach
    // every instance of it. See scene/Prefab.h for the whole argument.
    //
    // The scene owns the records; Prefab.cpp owns the format and the diffing.
    // Lifetime here, meaning there.

    // Stamps a copy of a prefab asset into this scene. Returns the root.
    EntityId InstantiatePrefab(std::string_view virtualPath, std::string& outError);

    // The same, placed. THIS IS THE SPAWN CALL - Unity's Instantiate, and the
    // reason SpawnOptions exists at the top of this file.
    //
    // Doing the placement HERE rather than leaving it to the caller is the
    // whole point. The hand-written version is:
    //
    //     std::string error;
    //     const EntityId id = scene->InstantiatePrefab(path, error);
    //     if (id.IsNull()) { ...report... }
    //     Entity* spawned = scene->Get(id);
    //     if (spawned == nullptr) { return; }
    //     spawned->Transform().SetWorldPosition(where);
    //     spawned->Transform().SetLocalRotation(angle);
    //     if (Entity* under = scene->Get(parent)) {
    //         spawned->Transform().SetParent(&under->Transform(), true);
    //         if (PrefabInstance* record = PrefabInstanceOf(id)) {
    //             record->parentName = under->Name();   // forgetting this
    //         }                                         // loses the parent
    //     }                                            // on the next save
    //
    // - fourteen lines, and the commented one is a trap that is invisible
    // until somebody saves the scene. See Prefab.h on why the record and the
    // live tree are two pieces of state.
    //
    // THE ROOT IS PLACED, NOT THE MEMBERS. A prefab's children sit where the
    // asset says relative to their root, which is what makes a prefab a
    // prefab; moving them individually is what overrides are for.
    EntityId InstantiatePrefab(std::string_view virtualPath,
                               const SpawnOptions& placement, std::string& outError);

    // Which instance an entity belongs to, or nullptr for an ordinary entity.
    // Every member answers, not just the root.
    PrefabInstance*       PrefabInstanceOf(EntityId id);
    const PrefabInstance* PrefabInstanceOf(EntityId id) const;

    // Which entity of the asset this is. 0 when it belongs to no instance.
    int PrefabLocalIdOf(EntityId id) const;

    // ---- entities this scene file does not describe -----------------------
    //
    // A DERIVED ENTITY IS BUILT FROM SOMETHING ELSE, and the save skips it at
    // the same point and for the same reason it skips a prefab instance's
    // members: writing it out would freeze a copy of whatever built it into
    // this scene file, so editing that thing would stop reaching the scenes
    // using it, and loading would produce the entity twice over.
    //
    // WHAT BUILDS THEM TODAY: a tilemap, for every painted cell whose tile
    // carries components - see TilemapComponent::SyncTileObjects. There is no
    // record to write for one, because the map's cells and the tile's .otile
    // already say everything there is to say about it.
    //
    // UNLIKE A PREFAB INSTANCE there are no overrides, because there is
    // nothing to override: a tile is a kind of thing and every cell painted
    // with it is the same thing. Something that needs to differ per placement
    // is an entity, or a prefab, and not a tile.
    //
    // The flag lives in the SLOT rather than in the Entity, for exactly the
    // reason the prefab link does: it is something the scene knows about an
    // entity rather than something the entity is, and the slot is cleared by
    // the same code that frees it - so a reused slot cannot inherit it.
    void SetDerived(EntityId id, bool derived);
    bool IsDerived(EntityId id) const;

    // ======================================================================
    //  SURVIVES A SCENE CHANGE. Unity's DontDestroyOnLoad.
    //
    //  The entity, its components, its scripts WITH THEIR RUNNING STATE, and
    //  its children all stay exactly as they are while the scene around them
    //  is replaced. Not re-created from a file afterwards - the same objects,
    //  at the same addresses, with the same EntityId. That is the whole point:
    //  a behaviour that has been counting something goes on counting.
    //
    //  ==========================================================================
    //  ONLY A SCENE CHANGE CARRIES THEM, which is what the name says and is a
    //  narrower promise than it first looks. Unload() destroys everything by
    //  default; it is Load() that asks for them to be kept, and nothing else
    //  does:
    //
    //    Load(path)        KEEPS them. This is a scene change - GoToScene,
    //                      RequestScene, the thing the feature is for.
    //    LoadFromString    destroys them. It is how play mode restores its
    //                      snapshot, and a Stop that left objects behind would
    //                      leave the editor holding something the scene file
    //                      does not describe.
    //    StartNewScene     destroys them. "New scene" means nothing carried.
    //    ~Scene, Shutdown  destroy them. Nothing survives the process.
    //
    //  ==========================================================================
    //  CHILDREN COME TOO, and a persistent child of a non-persistent parent is
    //  DETACHED rather than orphaned - see the note in Unload. Unity solves
    //  the same problem by only honouring this on a root; detaching says the
    //  same thing out loud instead of ignoring the call.
    //
    //  ==========================================================================
    //  AND THEY ARE NOT SAVED, for the reason a derived entity is not: an
    //  entity that came from scene A is not part of scene B's description, and
    //  writing it there would make a copy appear every time that scene loaded.
    // ==========================================================================
    void SetPersistent(EntityId id, bool persistent);
    bool IsPersistent(EntityId id) const;

    // How many entities are currently marked. For tests, and for anything
    // that wants to report it rather than leaving survivors looking ordinary.
    std::size_t PersistentCount() const;

    // The root of the instance this entity belongs to, or a null id.
    //
    // FOUND BY WALKING UP rather than by asking the asset which id is the
    // root: the member whose parent is not a member of the same instance is
    // the root, which is true without reading the file. That matters because
    // the root has to be findable when the asset is missing - which is exactly
    // when the editor needs to tell you which instance is broken.
    EntityId PrefabRootOf(EntityId id);
    EntityId PrefabRootOf(const PrefabInstance& instance);
    bool     IsPrefabRoot(EntityId id) const;

    // The entity for one local id of one instance, or a null id.
    EntityId PrefabMember(const PrefabInstance& instance, int localId);

    void ForEachPrefabMember(const PrefabInstance& instance,
                             const std::function<void(Entity&, int localId)>& fn);
    void ForEachPrefabInstance(const std::function<void(PrefabInstance&)>& fn);

    std::size_t PrefabInstanceCount() const { return m_prefabInstances.size(); }

    // Turns every member into an ordinary entity and forgets the record.
    // NOTHING MOVES and nothing is destroyed - the link is what goes, which
    // is why Unity calls it unpacking rather than deleting.
    bool UnpackPrefabInstance(EntityId member);

    // Destroys the whole instance: every member, right now.
    //
    // IT HAS TO BE THE WHOLE THING. DestroyEntityImmediate hands an entity's
    // children back to the world, which for a prefab root would leave its
    // members behind as orphans still claiming to belong to an instance that
    // no longer exists. See the note on it in Scene.cpp.
    bool DestroyPrefabInstanceImmediate(EntityId member);

    // Re-reads an asset from disk and rebuilds every instance of it in this
    // scene, keeping each one's own overrides.
    //
    // THIS IS THE ONE THAT MAKES THE LINK LIVE. Call it after the file has
    // been written - it captures each instance's overrides against the asset
    // as it was, THEN drops the cached copy and reads the new one. Doing those
    // in the other order would diff against the new asset and quietly pin
    // every instance to the values it had before the change, which is the
    // exact opposite of what a prefab is for.
    std::size_t RebuildPrefabInstances(std::string_view virtualPath,
                                       std::string& outError);

    // Rebuilds ONE instance from its asset, keeping its overrides. Used after
    // reverting, where the asset has not changed and the other instances have
    // no reason to be disturbed.
    bool RebuildPrefabInstance(PrefabInstance& instance, std::string& outError);

    // The camera position and zoom the scene file asked for, so the game can
    // apply them without knowing what a scene file looks like.
    Vec2  InitialCameraPosition() const { return m_cameraPosition; }
    float InitialCameraZoom() const     { return m_cameraZoom; }

    // ======================================================================
    //  HOW MANY WORLD UNITS TALL THE SHOT IS, or ZERO when the file did not
    //  say - which is every scene written before this existed.
    //
    //  ZERO AS "NOT SAID" rather than a second boolean beside it, because the
    //  two can then never disagree: a frame height of zero is not a shot, so
    //  there is no state in which the flag says yes and the number says
    //  nothing. See render/Camera.h for what the two framings mean.
    // ======================================================================
    float InitialCameraFrameHeight() const { return m_cameraFrameHeight; }

    // Pushed back in before saving, so framing a shot in the editor and
    // pressing save keeps the framing.
    //
    // THE TWO SETTERS ARE EXCLUSIVE, and that is the point: a scene is framed
    // one way or the other, and whichever one is called is the one the file
    // gets. Writing both keys would make the file ambiguous and make a
    // round trip depend on which the reader preferred.
    void SetCameraState(Vec2 position, float zoom) {
        m_cameraPosition    = position;
        m_cameraZoom        = zoom;
        m_cameraFrameHeight = 0.0f;
    }

    void SetCameraFrameHeight(Vec2 position, float frameHeight, float effectiveZoom) {
        m_cameraPosition = position;
        m_cameraFrameHeight = frameHeight;

        // KEPT IN STEP ANYWAY, even though it is not written to the file. The
        // engine reads InitialCameraZoom when it has no frame height, and a
        // stale value here would be the one the editor handed back after a
        // Play session that never touched the camera.
        m_cameraZoom = effectiveZoom;
    }

    // The scene the engine is currently running.
    static Scene* Active();
    static void   SetActive(Scene* scene);

private:
    // TransformComponent is the ONE writer of the transform table that
    // EntityOf reads. Declared a friend rather than given a public setter
    // because a public one would let any script repoint the reverse lookup
    // for an entity it does not own, and every EntityOf in the engine would
    // then be answering a question somebody else had changed.
    friend class TransformComponent;
    void RegisterTransform(const Transform2D& transform, EntityId owner);
    void ForgetTransform(const Transform2D& transform);

    // Shared by the two InstantiatePrefab overloads and by
    // CreateEntityUniquelyNamed, so that "where does a spawned thing go" has
    // exactly one implementation.
    void ApplyPlacement(EntityId id, const SpawnOptions& placement);

    struct Slot {
        std::unique_ptr<Entity> entity;
        int                     generation = 1;   // never 0; see EntityId.h
        bool                    occupied   = false;

        // ---- which prefab instance this entity belongs to, if any --------
        //
        // A RAW POINTER, and safely so: the instances are held by
        // unique_ptr in a vector, so adding one never moves the others.
        // Removing one clears every slot pointing at it first - see
        // ForgetPrefabInstance.
        //
        // It lives in the SLOT rather than in the Entity because prefab
        // linkage is something the scene knows about an entity, not
        // something the entity is. An Entity is a name, an id and a bag of
        // components, and it has stayed that way. It also means the link
        // is cleared by the same code that frees the slot, so a reused slot
        // cannot inherit the last occupant's instance.
        PrefabInstance* prefab        = nullptr;
        int             prefabLocalId = 0;

        // Built from something else and not written when the scene is saved.
        // See SetDerived above.
        bool derived = false;

        // Survives a scene CHANGE. See Scene::SetPersistent.
        bool persistent = false;
    };

    // ======================================================================
    //  GIVES A SURVIVOR ITS NAME BACK, once the incoming scene is built.
    //
    //  During the build the newcomer owns the name, because a scene file
    //  resolves its own parents by name and a survivor holding "Manager"
    //  throughout would have the new scene's child of Manager parented to the
    //  SURVIVOR - quietly rewriting the hierarchy of the scene being loaded.
    //
    //  Afterwards the survivor takes it, because the singleton check that
    //  every Unity user writes looks for an existing one BY NAME, and a
    //  survivor nothing can find is a survivor nothing can stand down to.
    // ======================================================================
    void ReclaimCarriedNames();

    // The names survivors held when the last scene was unloaded, waiting for
    // the build to finish. Empty except between an Unload(KeepPersistent) and
    // the ReclaimCarriedNames that follows it.
    std::vector<std::pair<std::string, EntityId>> m_carriedNames;

    void ResolveParents(const Json& entitiesArray);

    // Builds the entities for one instance from its asset and its overrides,
    // and links them. Shared by InstantiatePrefab, by loading a scene and by
    // rebuilding after the asset changed, so all three produce an instance
    // that is put together the same way.
    EntityId BuildPrefabInstance(PrefabInstance& instance, std::string& outError);

    // Puts the instance's root under the scene entity its record names.
    //
    // SEPARATE FROM BUILDING because loading needs it twice. One instance can
    // be parented under another, and the one it names may not have been built
    // yet when its turn comes - so every instance is built first and every
    // root is attached afterwards. Calling it twice is harmless: it does
    // nothing when the root is already where it should be.
    void AttachPrefabInstanceToParent(PrefabInstance& instance);

    // Destroys every member of an instance without touching the record, so
    // that rebuilding can put new entities under the same overrides.
    void DestroyPrefabMembers(const PrefabInstance& instance);

    // Drops the record itself, clearing every slot that points at it FIRST.
    void ForgetPrefabInstance(PrefabInstance& instance);

    // Reads the "prefabInstances" list and builds what it describes. Part of
    // loading, after the plain entities exist - a prefab's root may name one
    // of them as its parent and vice versa.
    void BuildPrefabInstances(const Json& instancesArray);

    // Shared by Load (from a file) and LoadFromString (from a Play snapshot),
    // so restoring a snapshot takes exactly the same path as a normal load and
    // the two cannot drift apart.
    bool BuildFromDocument(std::string& outError);

    // ---- DECLARED BEFORE m_slots, AND THAT MATTERS -----------------------
    //
    // Transform -> entity, so EntityOf is a hash lookup rather than a scan.
    // See EntityOf for why the table is here at all.
    //
    // Destroying an entity calls OnDetach on its components, and
    // TransformComponent's OnDetach erases from this map. Members are
    // destroyed in REVERSE declaration order, so a table declared after
    // m_slots would already be gone by the time the entities in m_slots were
    // taken apart. ~Scene calls Unload() first, which clears the slots while
    // everything is still alive, so this ordering is a second line of defence
    // rather than the only one - but it costs nothing and the failure it
    // prevents is a use-after-free during shutdown, which is the hardest kind
    // to find.
    std::unordered_map<const Transform2D*, EntityId> m_transformOwners;

    std::vector<Slot> m_slots;
    std::vector<int>  m_freeIndices;   // slots that can be reused

    // Name -> entity, so Find() does not have to search every slot.
    std::unordered_map<std::string, EntityId> m_byName;

    std::size_t m_liveCount = 0;

    // ---- the prefab instances in this scene -------------------------------
    //
    // unique_ptr so that adding one never moves the others, because every
    // member's slot holds a pointer to its instance. A vector of
    // PrefabInstance by value would invalidate all of them on the next
    // push_back, which is the sort of bug that shows up as one wrong prefab
    // in a big scene weeks later.
    std::vector<std::unique_ptr<PrefabInstance>> m_prefabInstances;

    // The parsed scene file, kept so that saving puts back any key this build
    // did not recognise. See BuildSceneDocument in Scene.cpp.
    Json        m_document = Json::object();
    std::string m_name;
    std::string m_sourcePath;

    // See IsOpen. Set by Load, LoadFromString and StartNewScene; cleared by
    // Unload. NOT derived from the entity count, because an empty scene is
    // open and a closed one is not.
    bool m_open = false;
    Vec2        m_cameraPosition{0.0f, 0.0f};
    float       m_cameraZoom = 1.0f;

    // 0 means "the file framed by zoom" - see InitialCameraFrameHeight.
    float       m_cameraFrameHeight = 0.0f;
};

// ===========================================================================
//  The templates, below the class because they need it complete.
//
//  All of them compare component type names AS TEXT rather than by pointer.
//  The literal in T::kTypeName and the literal the component returns live in
//  two different binaries whenever the asking code is a compiled script, and
//  nothing promises the linker folded them to one address. Entity::Find has
//  the same note.
// ===========================================================================

template <class T>
Entity* Scene::FindFirstWith(bool skipPendingDestroy) {
    Entity* found = nullptr;
    // ForEachUntil rather than ForEach: "the game rules object" is usually
    // near the front, and walking the remaining four hundred entities after
    // finding it is pure waste sixty times a second.
    ForEachUntil([&](Entity& entity) {
        if (skipPendingDestroy && DeferredOps::IsPendingDestroy(entity.Id())) {
            return true;
        }
        if (entity.Find<T>() != nullptr) {
            found = &entity;
            return false;
        }
        return true;
    });
    return found;
}

template <class T>
typename FindTargetOf<T>::type* Scene::FindFirstComponent(
    bool skipPendingDestroy) {
    Entity* owner = FindFirstWith<T>(skipPendingDestroy);
    return owner != nullptr ? owner->Find<T>() : nullptr;
}

template <class T>
std::size_t Scene::ForEachWith(
    const std::function<void(Entity&, typename FindTargetOf<T>::type&)>& fn,
    bool skipPendingDestroy) {
    std::size_t seen = 0;
    ForEach([&](Entity& entity) {
        if (skipPendingDestroy && DeferredOps::IsPendingDestroy(entity.Id())) {
            return;
        }
        if (auto* component = entity.Find<T>(); component != nullptr) {
            ++seen;
            fn(entity, *component);
        }
    });
    return seen;
}

template <class T>
std::size_t Scene::CountWith(bool skipPendingDestroy) {
    std::size_t count = 0;
    ForEach([&](Entity& entity) {
        if (skipPendingDestroy && DeferredOps::IsPendingDestroy(entity.Id())) {
            return;
        }
        if (entity.Find<T>() != nullptr) {
            ++count;
        }
    });
    return count;
}

// ---------------------------------------------------------------------------
//  Entity's template lookups, declared in Entity.h and defined here.
//
//  The hierarchy ones need the complete Scene, to get from a child transform
//  to its entity. FindAll needs only the complete Component - see the note at
//  its declaration - but it lives here with them rather than in a third
//  place.
// ---------------------------------------------------------------------------

template <typename T>
std::size_t Entity::FindAll(std::vector<typename FindTargetOf<T>::type*>& into) {
    // UNWRAPPED ONCE, HERE. Inside a script a bare component name is a
    // ComponentRef, and the vector a caller can actually declare holds the
    // COMPONENT - a std::vector<ComponentRef<SpriteComponent>*> is not
    // something anybody would write. See FindTargetOf in this header.
    using Target = typename FindTargetOf<T>::type;
    into.clear();
    const std::string_view wanted = Target::kTypeName;
    for (std::size_t i = 0; i < ComponentCount(); ++i) {
        Component* component = ComponentAt(i);
        // Compared as TEXT rather than by pointer. The two string literals
        // live in engine.dll and in the compiled script that is asking, and
        // nothing promises the linker folded them to one address.
        if (component != nullptr && std::string_view(component->TypeName()) == wanted) {
            into.push_back(static_cast<Target*>(component));
        }
    }
    return into.size();
}


template <typename T>
typename FindTargetOf<T>::type* Entity::FindInChildren(bool includeSelf) {
    if (includeSelf) {
        if (auto* mine = Find<T>(); mine != nullptr) {
            return mine;
        }
    }

    Scene* scene = GetScene();
    if (scene == nullptr) {
        return nullptr;
    }

    // DEPTH FIRST, and by an explicit stack rather than recursion. A
    // hand-built hierarchy can be arbitrarily deep - nothing in the engine
    // caps it - and a recursive walk would turn a deep one into a stack
    // overflow, which reports as an access violation with no message.
    //
    // A COPY of each child list, because T's constructor cannot run here but
    // the callback in FindAllInChildren below shares this shape and a caller
    // there may reparent. Transform2D::SetParent rewrites both the old and the
    // new parent's child vector, and a range-for over the live one would be
    // holding freed iterators. That already shipped as a bug once - see
    // editor/src/panels/HierarchyPanel.cpp.
    std::vector<const Transform2D*> pending(Transform().Children().begin(),
                                            Transform().Children().end());
    while (!pending.empty()) {
        const Transform2D* node = pending.back();
        pending.pop_back();
        if (node == nullptr) {
            continue;
        }
        if (Entity* owner = scene->EntityOf(*node); owner != nullptr) {
            if (auto* found = owner->Find<T>(); found != nullptr) {
                return found;
            }
        }
        pending.insert(pending.end(), node->Children().begin(), node->Children().end());
    }
    return nullptr;
}

template <typename T>
typename FindTargetOf<T>::type* Entity::FindInParents(bool includeSelf) {
    if (includeSelf) {
        if (auto* mine = Find<T>(); mine != nullptr) {
            return mine;
        }
    }

    Scene* scene = GetScene();
    if (scene == nullptr) {
        return nullptr;
    }

    for (const Transform2D* node = Transform().Parent(); node != nullptr;
         node                    = node->Parent()) {
        if (Entity* owner = scene->EntityOf(*node); owner != nullptr) {
            if (auto* found = owner->Find<T>(); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

template <typename T>
std::size_t Entity::FindAllInChildren(
    std::vector<typename FindTargetOf<T>::type*>& into, bool includeSelf) {
    using Target = typename FindTargetOf<T>::type;
    into.clear();

    std::vector<Target*> onThisEntity;
    if (includeSelf) {
        FindAll<Target>(onThisEntity);
        into.insert(into.end(), onThisEntity.begin(), onThisEntity.end());
    }

    Scene* scene = GetScene();
    if (scene == nullptr) {
        return into.size();
    }

    // Breadth first here, unlike FindInChildren, and the difference is worth
    // stating: this one returns a LIST, and a list whose order jumps about the
    // tree is hard to reason about. Nearest children first is the order
    // somebody reading the result expects.
    std::vector<const Transform2D*> pending(Transform().Children().begin(),
                                            Transform().Children().end());
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const Transform2D* node = pending[i];
        if (node == nullptr) {
            continue;
        }
        if (Entity* owner = scene->EntityOf(*node); owner != nullptr) {
            owner->FindAll<Target>(onThisEntity);
            into.insert(into.end(), onThisEntity.begin(), onThisEntity.end());
        }
        pending.insert(pending.end(), node->Children().begin(), node->Children().end());
    }
    return into.size();
}

template <typename T>
std::size_t Entity::FindAllInParents(
    std::vector<typename FindTargetOf<T>::type*>& into, bool includeSelf) {
    using Target = typename FindTargetOf<T>::type;
    into.clear();

    std::vector<Target*> onThisEntity;
    if (includeSelf) {
        FindAll<Target>(onThisEntity);
        into.insert(into.end(), onThisEntity.begin(), onThisEntity.end());
    }

    Scene* scene = GetScene();
    if (scene == nullptr) {
        return into.size();
    }

    // No stack and no copy needed, unlike the two above: a parent chain is a
    // single line with no branching, and walking UP cannot reach the same node
    // twice - Transform2D::SetParent refuses to make anything its own
    // ancestor, which is what makes that true rather than merely likely.
    for (const Transform2D* node = Transform().Parent(); node != nullptr;
         node                    = node->Parent()) {
        if (Entity* owner = scene->EntityOf(*node); owner != nullptr) {
            owner->FindAll<Target>(onThisEntity);
            into.insert(into.end(), onThisEntity.begin(), onThisEntity.end());
        }
    }
    return into.size();
}

} // namespace eng
