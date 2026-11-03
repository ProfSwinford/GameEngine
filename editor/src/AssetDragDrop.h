#pragma once

// ============================================================================
//  AssetDragDrop.h - what happens when you drag a file out of the Assets panel
//  and drop it on something.
//
//  ImGui matches a drag SOURCE to a drop TARGET using a short text id, and a
//  typo in that text does not fail loudly - the drop is simply never accepted
//  and nothing on screen explains why. That is a bad half-hour, and it is
//  entirely avoidable by having exactly one definition of each id that both
//  ends use. Hence this file.
//
//  ==========================================================================
//  WHAT A DROP MEANS depends on where it lands:
//
//    image  -> Scene view      create a new entity there with that sprite
//    image  -> Hierarchy row   give THAT entity the sprite (adding the
//                              component if it does not have one)
//    image  -> Inspector       the same, for whatever is selected
//    script -> Hierarchy row   attach a ScriptComponent bound to that script
//    script -> Inspector       the same, for whatever is selected
//    scene  -> Scene view      load it
//    prefab -> Scene view      place an instance of it there
//    prefab -> Hierarchy row   place an instance as a child of THAT entity
//
//  The two "apply it to this entity" cases are identical in effect and would
//  be very easy to write twice with a subtle difference between them, so
//  ApplyAssetToEntity is one implementation both use.
//
//  ==========================================================================
//  AND ONE DRAG THAT GOES THE OTHER WAY: an ENTITY dragged out of the
//  Hierarchy and dropped on the Assets panel, which makes a prefab of it.
//
//  That payload is kPayloadEntity and it belongs to the Hierarchy - see
//  panels/HierarchyPanel.h - so the receiving code lives in the Assets panel
//  rather than here. Noted here because this is where somebody looks for "what
//  can be dropped where".
// ============================================================================

#include <engine/Engine.h>

#include <string>

namespace editor {

// The ids ImGui matches on. Under 32 characters, which is its limit.
inline constexpr const char* kPayloadTexture = "ASSET_TEXTURE";
inline constexpr const char* kPayloadScene   = "ASSET_SCENE";
inline constexpr const char* kPayloadScript  = "ASSET_SCRIPT";
inline constexpr const char* kPayloadFont    = "ASSET_FONT";
inline constexpr const char* kPayloadTile    = "ASSET_TILE";
inline constexpr const char* kPayloadPrefab  = "ASSET_PREFAB";

// Everything else the Assets panel can pick up: a FOLDER, and any file whose
// kind nothing here recognises. Neither of those means anything to an entity,
// so no panel outside the Assets browser accepts this id - but both can be
// moved to a different folder, and something that cannot be picked up cannot
// be reorganised.
inline constexpr const char* kPayloadAssetItem = "ASSET_ITEM";

// Every id a tile in the Assets panel can carry.
//
// A drag source sets exactly ONE payload, so a tile holding an image carries
// ASSET_TEXTURE and nothing else. A target that cares WHAT is being dragged
// therefore names the single id it wants - the Inspector's Font field takes
// ASSET_FONT and nothing else. A target that only cares WHERE the drag lands,
// which is what a folder is, has to offer all of them. Listing them in one
// place is what stops the two ends drifting apart as kinds are added.
inline constexpr const char* kAllAssetPayloads[] = {
    kPayloadTexture, kPayloadScene, kPayloadScript, kPayloadFont, kPayloadTile,
    kPayloadPrefab,  kPayloadAssetItem,
};

enum class AssetKind {
    Unknown,
    Texture,
    Scene,
    Script,
    Font,

    // A .otile: one tile material, naming the five-band sheet it draws from.
    // See engine/include/engine/tilemap/TileAsset.h.
    Tile,

    // A .oprefab: an entity and its children, saved, with every instance of
    // it still linked back. See engine/include/engine/scene/Prefab.h.
    Prefab,
};

// Works out what kind of file this is, from its EXTENSION and its folder -
// not by opening it and looking inside. The browser classifies every visible
// file every frame while you scroll, and opening each one to find out what it
// is would make it the slowest thing in the editor.
AssetKind ClassifyAsset(std::string_view virtualPath);

// The ImGui drag id for a kind. One switch statement, so adding a new kind is
// one edit rather than three.
//
// NEVER NULL. A kind nothing recognises gets kPayloadAssetItem rather than
// nothing at all, because every file in the panel has to be draggable to be
// movable - and a .json or a .txt is as much a part of the project as a .png.
const char* PayloadIdFor(AssetKind kind);

// "enemies/PlayerController.cpp" -> "PlayerController".
//
// That is the same text ENGINE_REGISTER_SCRIPT produced when the script
// registered itself, which is what makes dropping a FILE attach a behaviour
// that is looked up by NAME.
std::string ScriptNameFromPath(std::string_view virtualPath);

// The other direction: "PlayerMovement" -> "enemies/PlayerMovement.cpp".
//
// A ScriptComponent stores a NAME, because that is what survives a file being
// moved to a different folder. Opening the file it came from means finding it
// again, and the only thing tying the two together is the convention that a
// script lives in a file called after it - the same assumption
// ScriptNameFromPath makes in reverse when you drag a .cpp onto an entity.
//
// Headers are searched as well as .cpp files, because a script written
// entirely in a header is a script like any other. See ScriptBuild::Sources.
//
// Returns an empty string when nothing matches. THIS WALKS THE WHOLE assets/
// TREE, so call it when somebody asks for the file - not every frame.
std::string ScriptPathFromName(std::string_view scriptName);

// Applies a dropped file to an entity that already exists. Returns false with
// a reason when the drop makes no sense - dropping a scene onto an entity, say
// - so the caller can explain rather than silently doing nothing.
bool ApplyAssetToEntity(eng::EntityId target, std::string_view virtualPath,
                        std::string& outMessage);

// Creates a NEW entity for a dropped file at a position in the world. Used by
// the Scene view. Returns an empty id and a reason if it could not.
//
// An IMAGE becomes a new entity with that sprite. A PREFAB becomes an instance
// of itself - which is a different act with the same gesture, and the reason
// this is one function rather than two is that the Scene view should not have
// to know which kinds of file can be dropped into a world.
eng::EntityId CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                   std::string& outMessage);

// Where a new prefab made out of `entity` should be written, given the folder
// the Assets panel is showing: "enemies/Turret.oprefab".
//
// Here rather than in the Assets panel because the Scene view's own "save this
// as a prefab" will want the same answer, and two pieces of code deciding
// where prefabs go is how you get half of them in the wrong folder.
std::string PrefabPathFor(const eng::Entity& entity, std::string_view virtualDirectory);

// The whole receiving side, for the item that was drawn most recently: opens
// the drop target, accepts an image or a script, applies it, writes the result
// to the log, and closes the target. Call it directly after the widget the
// drop should land on.
//
// It is one function so that the Hierarchy row and the Inspector cannot
// disagree about which kinds of file they accept - the failure being a panel
// where scripts drop and images mysteriously do not.
bool AcceptAssetDropOnEntity(eng::EntityId target);

} // namespace editor
