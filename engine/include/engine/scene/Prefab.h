#pragma once

// ============================================================================
//  Prefab.h - a saved entity you can stamp into any scene, which stays LINKED
//  to the thing it was stamped from.
//
//  This is Unity's prefab, and the linking is the whole point. Twenty copies
//  of an enemy are not twenty enemies: they are twenty instances of one enemy.
//  Change the asset and all twenty change. Change one instance and only that
//  one changes, and the engine remembers exactly WHICH parts of it you changed
//  so that everything else still follows the asset.
//
//  Without that link a "prefab" is a copy-paste button, and copy-paste is
//  precisely what stops working at the point a game gets big enough to need
//  it: the moment you have to fix a mistake in all twenty.
//
//  ==========================================================================
//  THE ONE IDEA THAT MAKES THE REST FOLLOW
//
//  A PREFAB INSTANCE IS NOT STORED IN THE SCENE FILE AS ENTITIES. It is
//  stored as a short record - which asset, where, and what was changed about
//  it - and the entities are BUILT from the asset when the scene loads.
//
//      "prefabInstances": [
//        {
//          "prefab": "prefabs/enemy.oprefab",
//          "name":   "Enemy_3",
//          "overrides": [
//            { "entity": 1, "component": "TransformComponent",
//              "field": "position", "value": [120, 40] }
//          ]
//        }
//      ]
//
//  That is three lines of scene file for a nine-entity enemy, and it is the
//  reason editing the asset reaches the instances at all. The alternative -
//  write the entities out and remember where they came from - means the scene
//  file holds a stale copy of every instance, and "the asset changed" turns
//  into a migration problem across every scene in the project.
//
//  ==========================================================================
//  LOCAL IDS, AND WHY NOT NAMES
//
//  Everything else in this engine identifies an entity by its NAME, because
//  names are unique within a scene and survive the ids being regenerated on
//  load - see EntityRef.h and the selection in UndoStack.h.
//
//  THAT DOES NOT WORK HERE, for two reasons that each kill it on their own:
//
//    A prefab with a child called "Muzzle", instantiated three times, wants
//    three entities called "Muzzle" - and the scene cannot have three. So the
//    instances get "Muzzle", "Muzzle_1", "Muzzle_2", and the name is no longer
//    a property of the asset at all.
//
//    Renaming a child in the asset would silently detach every override
//    anybody had ever made to it, in every scene, with nothing reported.
//
//  So each entity in a prefab asset carries a number that never changes:
//
//      { "id": 2, "parent": 1, "name": "Muzzle", "components": [ ... ] }
//
//  An override says "entity 2", not "Muzzle". Unity calls this a fileID and
//  does the same thing for the same reason.
//
//  ==========================================================================
//  OVERRIDES ARE WORKED OUT BY COMPARING, NOT BY BEING TOLD
//
//  Unity records a modification at the moment you make one. This engine
//  instead DIFFS the live instance against its asset whenever the answer is
//  wanted - when the scene is saved, and when the Inspector draws.
//
//  THE REASON IS THE SAME ONE UNDO HAS. The editor changes the scene from
//  about forty places, and a design where each of them must also remember to
//  record an override is a design where the first one somebody forgets
//  produces an instance that silently snaps back to the asset. There is no
//  compiler check for a forgotten call. Diffing cannot be forgotten, because
//  nobody has to remember it: whatever an edit did, the comparison sees it -
//  including edits written years after this file.
//
//  WHAT IT COSTS, stated plainly: Unity can record "this property is
//  overridden to the same value the asset happens to have", and so pin it
//  against a later change to the asset. A diff cannot see that - a value
//  equal to the asset's is indistinguishable from one nobody touched, so it
//  tracks the asset. That is the only behavioural difference, and it is worth
//  the forty edit sites.
//
//  ARRAYS ARE ATOMIC, OBJECTS RECURSE. "position": [3, 4] is one value, not
//  two, because half a position is not a thing anybody wants to override. But
//  a script's variables arrive nested - "vars": { "damage": 5 } - so the diff
//  walks into objects and produces a dotted path, "vars.damage". Without that,
//  changing one variable would override the whole set and a later change to a
//  DIFFERENT variable in the asset would stop arriving.
//
//  ==========================================================================
//  WHAT IS NOT HERE, so that nobody looks for it
//
//  NESTED PREFABS. A prefab asset holding an instance of another prefab.
//  Creating a prefab out of an entity that is already an instance FLATTENS it:
//  the new asset gets plain copies. The format leaves room - "prefab": 1 is a
//  version number, and an asset's entity list could carry instance records of
//  its own - but nothing reads that yet.
//
//  PREFAB VARIANTS. An asset that inherits from another asset.
//
//  PREFAB MODE. Unity opens an asset in its own isolated scene view. Here an
//  asset is edited by editing one of its instances and pressing Apply, which
//  is the older Unity workflow and needs no second scene.
//
//  ADDED CHILD ENTITIES ARE PLAIN ENTITIES. Parent something new under an
//  instance and it is an ordinary member of the scene that happens to sit
//  below a prefab child. It saves and loads correctly, it is not disturbed by
//  the asset changing, and it is not offered to Apply. Unity tracks it as an
//  "added GameObject" so it can be pushed into the asset; that is the one
//  override category deliberately left out.
//
//  DELETING A PREFAB CHILD IS REFUSED, with a message pointing at Unpack.
//  That is Unity's long-standing behaviour, and the alternative is a fourth
//  override category ("removed objects") for a gesture almost nobody wants.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/scene/EntityId.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng {

class Entity;
class Scene;

// The extension a prefab asset is saved with, dot included.
//
// ONE PLACE, for the reason kSceneExtension gives in Scene.h: the spelling is
// needed by the Assets panel's classifier, by the exporter's "follow this
// document for further assets" list, and by every "is this a prefab?" test.
// Three copies of a fact are three chances to be wrong.
inline constexpr const char* kPrefabExtension = ".oprefab";

// ---------------------------------------------------------------------------
//  THE ASSET
// ---------------------------------------------------------------------------

// One entity inside a prefab asset.
//
// `node` is exactly the shape Scene::CreateEntityFromJson already reads - a
// name and a list of components - so instantiating a prefab goes down the same
// road as loading a scene rather than a second, parallel one that can drift
// from it.
struct PrefabEntityNode {
    int  localId = 0;
    int  parent  = 0;   // 0 means "no parent inside this prefab"
    Json node    = Json::object();
};

class PrefabAsset {
public:
    // Reads a parsed .oprefab document. Returns false with a message naming
    // what is wrong, because the file is JSON a person can open and fix.
    bool Parse(const Json& document, std::string& outError);

    const std::string& Name() const { return m_name; }

    // The entity everything else in the prefab hangs off. An instance's root
    // is the entity that gets a parent in the scene, and the one the
    // Hierarchy and the Inspector treat as "the instance".
    int RootId() const { return m_rootId; }

    const std::vector<PrefabEntityNode>& Entities() const { return m_entities; }

    // nullptr for an id this asset does not have, which happens whenever a
    // scene holds overrides written against an older version of the asset.
    // That is an ordinary event, not an error: the override is dropped.
    const PrefabEntityNode* Find(int localId) const;

    // One more than the largest id in use, so a child added by Apply gets an
    // id that has never been used before. REUSING A FREED ID WOULD BE THE
    // WORST KIND OF BUG HERE: overrides in a scene nobody has open would
    // quietly re-attach themselves to a different entity.
    int NextFreeId() const;

    // The document this was parsed from, so Apply can rewrite the parts it
    // understands and leave everything else alone - the same promise
    // BuildSceneDocument makes about a scene file.
    const Json& Document() const { return m_document; }

private:
    Json                          m_document = Json::object();
    std::string                   m_name;
    int                           m_rootId = 0;
    std::vector<PrefabEntityNode> m_entities;
};

// ---------------------------------------------------------------------------
//  THE LIBRARY - path to asset, parsed once.
//
//  A scene with forty instances of one prefab must not read and parse that
//  file forty times, and the Inspector asks for the asset EVERY FRAME while
//  it draws an instance's override list.
//
//  IT IS A CACHE, so it has to be told when a file changes. Apply calls
//  Forget; so does the Assets panel when a .oprefab is deleted or renamed.
//  Nothing watches the disk.
// ---------------------------------------------------------------------------
class PrefabLibrary {
public:
    // Reads and parses the asset, or hands back the copy it already has.
    // nullptr with a message when the file is missing or malformed.
    //
    // A FAILURE IS NOT CACHED. A prefab that is missing because the project
    // has not finished being set up should start working the moment the file
    // appears, without restarting the editor.
    static const PrefabAsset* Load(std::string_view virtualPath, std::string& outError);

    // Drops one entry, so the next Load re-reads the file.
    static void Forget(std::string_view virtualPath);

    // Drops everything. Called when the project changes underneath us - a
    // different project's prefabs/enemy.oprefab is a different file with the
    // same virtual path, which is exactly the sort of thing a cache gets
    // wrong.
    static void Clear();

    static std::size_t Count();

    // Every path the library currently holds a parsed copy of.
    //
    // THIS IS WHAT MAKES "EDIT THE FILE, ALT-TAB BACK, SEE IT" POSSIBLE. The
    // editor checks the write time of each one when its window regains focus,
    // and only these matter: a project can hold hundreds of prefabs and the
    // ones in use are the ones somebody could be looking at.
    static void ForEachLoaded(const std::function<void(const std::string&)>& fn);
};

// ---------------------------------------------------------------------------
//  OVERRIDES
// ---------------------------------------------------------------------------

// One field of one component of one entity, changed on this instance.
//
// `slot` tells apart several components of the same type on one entity -
// three scripts, most obviously. It is the index among components of that
// type, so the second ScriptComponent is slot 1 whatever sits between them.
//
// `field` may be dotted: "vars.damage" reaches inside a nested object. See
// the note about arrays and objects at the top of this file.
struct PrefabOverride {
    int         localId = 0;
    std::string component;
    int         slot = 0;
    std::string field;
    Json        value;
};

// A component this instance has and the asset does not.
struct PrefabAddedComponent {
    int  localId   = 0;
    Json component = Json::object();   // a whole component node, "type" included
};

// A component the asset has and this instance does not.
struct PrefabRemovedComponent {
    int         localId = 0;
    std::string component;
    int         slot = 0;
};

// ---------------------------------------------------------------------------
//  AN INSTANCE, as the scene holds it
// ---------------------------------------------------------------------------
struct PrefabInstance {
    // The asset, as a virtual path: "prefabs/enemy.oprefab".
    std::string prefabPath;

    // The root entity's name in THIS scene. Doubles as the instance's
    // identity in the scene file, because a plain entity parented under the
    // instance names it here - parents are recorded by name.
    std::string rootName;

    // Which of the asset's entities is the root, remembered when the instance
    // was built.
    //
    // NOT IN THE FILE - it belongs to the asset, and reading it back out of
    // the scene would be two copies of one fact. It is kept here so that "is
    // this entity the root of its instance?" is one integer comparison: the
    // Hierarchy asks it of every row, every frame, and the alternative is
    // walking up the transform tree looking for the first parent that is not a
    // member, which needs a transform-to-entity search at every step.
    int rootLocalId = 0;

    // The scene-level parent of the root, by name. Empty for top level.
    std::string parentName;

    // localId -> the name that member has in this scene, for every member
    // whose name is NOT what the asset says.
    //
    // THIS IS NOT AN OVERRIDE AND IS NOT OFFERED AS ONE. It holds two
    // different things that cannot be told apart and should not be: a child
    // somebody renamed, and the "_1" the scene had to add because three
    // instances cannot all have a child called "Muzzle". Reverting the second
    // kind would do nothing visible, and "Revert All" renaming your instances
    // is not a thing anybody wants. Renames are simply kept.
    std::unordered_map<int, std::string> names;

    std::vector<PrefabOverride>         overrides;
    std::vector<PrefabAddedComponent>   addedComponents;
    std::vector<PrefabRemovedComponent> removedComponents;

    // How many overrides of every kind, for the Inspector's header and for
    // deciding whether Apply and Revert do anything.
    std::size_t OverrideCount() const {
        return overrides.size() + addedComponents.size() + removedComponents.size();
    }
};

// ---------------------------------------------------------------------------
//  THE FILE FORMAT, in one place each way round
// ---------------------------------------------------------------------------

// An instance record as it appears in a scene file.
Json WriteInstanceToJson(const PrefabInstance& instance);

// And back. Returns false only when there is no usable "prefab" path; an
// override that cannot be understood is skipped and reported, because a scene
// written by a newer build should still open.
bool ReadInstanceFromJson(const Json& node, PrefabInstance& outInstance,
                          std::string& outError);

// ---------------------------------------------------------------------------
//  BUILDING AND COMPARING
// ---------------------------------------------------------------------------

// The entity nodes an instance should actually be built from: the asset's
// nodes with this instance's overrides merged in, its removed components
// dropped and its added components appended.
//
// Returned in the same order as asset.Entities(), so the caller can pair them
// up by position and knows each one's local id and parent.
//
// WHY MERGE BEFORE BUILDING rather than build and then patch: components are
// filled in BEFORE they are attached, deliberately - see the note in
// Scene::CreateEntityFromJson - because OnAttach is what loads a sprite's
// image. Patching afterwards would attach every sprite with the asset's
// texture, load it, and then load the override's texture a moment later.
std::vector<Json> EffectiveEntityNodes(const PrefabAsset& asset,
                                       const PrefabInstance& instance);

// Reads the instance's root name and its scene-level parent back out of the
// live scene.
//
// RENAMING OR REPARENTING AN INSTANCE IS NOT AN OVERRIDE - it is where the
// thing is and what it is called, which belongs to the scene rather than to
// the prefab. So the record FOLLOWS those two rather than recording a
// difference from the asset. Without this, dragging an instance onto a new
// parent and then reverting it would put it back under the old one.
void RefreshPrefabPlacement(Scene& scene, PrefabInstance& instance);

// Replaces `instance`'s overrides with what its live entities actually say,
// and refreshes its placement.
//
// This is the diff, and it is the heart of the feature. Called before the
// scene is saved, before the asset is reloaded under an instance, and by the
// Inspector to list what is overridden.
//
// `outUnsaveable` counts components that cannot be serialised at all, which
// Scene::SaveToString needs: an override it cannot write down is an override
// that Stop would silently revert. Pass nullptr when the count is of no
// interest.
void CapturePrefabOverrides(Scene& scene, const PrefabAsset& asset,
                            PrefabInstance& instance,
                            std::size_t* outUnsaveable = nullptr);

// The same for every instance of one asset in this scene, against the asset as
// it currently stands.
//
// THE STEP THAT MUST COME BEFORE THE FILE CHANGES. Applying or re-importing a
// prefab rewrites the asset; every instance's deltas have to be worked out
// against the old one first, or each instance is pinned to the values it
// happened to have. Returns how many instances were captured.
std::size_t CapturePrefabOverridesForAsset(Scene& scene, std::string_view virtualPath);

// Builds a prefab document out of a live entity and everything below it.
//
// The entity it is given becomes local id 1 - the root - and its descendants
// are numbered in the order they are walked. Flattens any prefab instance it
// finds inside, as the header says.
bool BuildPrefabDocumentFromEntity(Scene& scene, EntityId root, Json& outDocument,
                                   std::string& outError);

// Writes an instance's current state back into its asset document, which is
// what "Apply All" means. Does NOT touch the disk and does NOT reload - the
// orchestration below does that, in the one order that works.
bool BuildAssetDocumentFromInstance(Scene& scene, const PrefabAsset& asset,
                                    const PrefabInstance& instance, Json& outDocument,
                                    std::string& outError);

// ---------------------------------------------------------------------------
//  IS THIS OVERRIDE THE INSTANCE'S PLACEMENT?
//
//  The root's position and rotation are where you PUT this instance, and
//  every instance is somewhere different. They are stored like any other
//  override - that is how an instance remembers where it is - but they are
//  left out of the override list, out of Apply and out of Revert.
//
//  Leaving them in would mean "Apply All" moving every other instance in
//  every other scene to wherever this one happens to stand, and "Revert All"
//  teleporting the thing you were working on back to the asset's origin.
//  Unity makes the same exception for the same reason.
//
//  SCALE IS NOT EXCLUDED. Scaling an instance is a decision about the object
//  rather than about where it stands, and it is one worth being able to push
//  into the asset.
//
//  IT ASKS THE INSTANCE WHICH ENTITY IS THE ROOT, NOT THE ASSET, and that is
//  not a style choice. Applying writes the asset file, which drops it from
//  PrefabLibrary - so a PrefabAsset* taken before the write is DANGLING after
//  it. This used to take one, and the placement overrides it was meant to
//  protect were being filtered by reading freed memory: apply an instance and
//  it jumped back to the asset's origin. PrefabInstance::rootLocalId is the
//  same number and outlives the write.
// ---------------------------------------------------------------------------
bool IsRootPlacement(const PrefabInstance& instance, const PrefabOverride& item);

// ---------------------------------------------------------------------------
//  THE OVERRIDE LIST, as the Inspector shows it
//
//  One type covering all three kinds, so the panel has one list to draw and
//  one function to call rather than three of each - and so an item can be
//  handed back BY VALUE.
//
//  THAT LAST PART IS NOT TIDINESS. The Inspector decides what to apply while
//  it is drawing and acts on it after every panel has finished, because
//  applying rebuilds every entity in the instance. An INDEX into the
//  instance's override vectors would be re-diffed in between and could by
//  then mean a different item. The item itself cannot go stale.
// ---------------------------------------------------------------------------
enum class PrefabOverrideKind { Property, AddedComponent, RemovedComponent };

struct PrefabOverrideItem {
    PrefabOverrideKind kind = PrefabOverrideKind::Property;

    // Only the one matching `kind` is filled in.
    PrefabOverride         property;
    PrefabAddedComponent   added;
    PrefabRemovedComponent removed;

    // "Muzzle / SpriteComponent / tint", ready to draw. Built here so that
    // the description and the format it describes stay in one file.
    std::string description;
};

// Everything overridden on this instance, placement excluded, in a stable
// order. Diffs first, so it always describes the scene as it is now.
std::vector<PrefabOverrideItem> ListPrefabOverrides(Scene& scene, const PrefabAsset& asset,
                                                    PrefabInstance& instance);

// ---------------------------------------------------------------------------
//  THE FOUR THINGS A PERSON DOES WITH AN INSTANCE
//
//  Each takes any member of the instance - the root or a child - because the
//  Inspector is showing whichever one is selected and should not have to walk
//  up to find the root first.
//
//  APPLY WRITES A FILE AND IS NOT UNDOABLE. Undo in this editor snapshots the
//  SCENE (see editor/src/UndoStack.h), and the asset is not in the scene. This
//  is the same deal as saving, and the editor says so before doing it.
//
//  REVERT IS UNDOABLE, because all it changes is the scene.
//
//  ==========================================================================
//  ALL FOUR INVALIDATE EVERY EntityId IN THE INSTANCE, AND APPLY INVALIDATES
//  THEM IN EVERY INSTANCE OF THE SAME ASSET.
//
//  They work by rebuilding members from the asset - that is how the other
//  instances pick a change up at all - so the entities are destroyed and
//  created again, and every id referring to one of them is now out of date.
//  The `member` argument included.
//
//  SO DO NOT HOLD AN ID ACROSS ONE OF THESE. Note which instance and which
//  local id you care about first, and ask Scene::PrefabMember for the new
//  entity afterwards. The record itself survives, so the PrefabInstance
//  pointer stays good.
//
//  This is the EntityId design doing its job rather than a wart - a stale id
//  comes back as nothing instead of as somebody else's entity - but it is a
//  sharp edge, and the editor's selection has to re-find its member after
//  every one of these calls.
// ---------------------------------------------------------------------------

// Pushes every override on this instance into the asset, then reloads the
// asset and rebuilds every instance of it in this scene - so the other
// instances pick the change up immediately, keeping their own overrides.
bool ApplyPrefabInstance(Scene& scene, EntityId member, std::string& outError);

// Throws this instance's overrides away, so it matches the asset again.
bool RevertPrefabInstance(Scene& scene, EntityId member, std::string& outError);

// The same two for one entry out of the list above.
bool ApplyPrefabItem(Scene& scene, EntityId member, const PrefabOverrideItem& item,
                     std::string& outError);
bool RevertPrefabItem(Scene& scene, EntityId member, const PrefabOverrideItem& item,
                      std::string& outError);

// ---------------------------------------------------------------------------
//  MAKING ONE
// ---------------------------------------------------------------------------

// Writes `root` and its descendants to `virtualPath` as a new prefab asset,
// then REPLACES those entities with an instance of it - which is what Unity
// does when you drag a GameObject into the Project window, and the reason it
// feels like a promotion rather than an export.
//
// Refuses to overwrite an existing file. The entity keeps its name, its place
// in the scene and its parent.
//
// `outRoot` receives the new instance's root, so the caller can select it.
bool CreatePrefabFromEntity(Scene& scene, EntityId root, std::string_view virtualPath,
                            EntityId& outRoot, std::string& outError);

} // namespace eng
