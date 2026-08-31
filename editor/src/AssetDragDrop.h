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
//
//  The two "apply it to this entity" cases are identical in effect and would
//  be very easy to write twice with a subtle difference between them, so
//  ApplyAssetToEntity is one implementation both use.
// ============================================================================

#include <engine/Engine.h>

#include <string>

namespace editor {

// The ids ImGui matches on. Under 32 characters, which is its limit.
inline constexpr const char* kPayloadTexture = "ASSET_TEXTURE";
inline constexpr const char* kPayloadScene   = "ASSET_SCENE";
inline constexpr const char* kPayloadScript  = "ASSET_SCRIPT";

enum class AssetKind {
    Unknown,
    Texture,
    Scene,
    Script,
};

// Works out what kind of file this is, from its EXTENSION and its folder -
// not by opening it and looking inside. The browser classifies every visible
// file every frame while you scroll, and opening each one to find out what it
// is would make it the slowest thing in the editor.
AssetKind ClassifyAsset(std::string_view virtualPath);

// The ImGui drag id for a kind, or nullptr for a kind that cannot be dragged.
// One switch statement, so adding a new kind is one edit rather than three.
const char* PayloadIdFor(AssetKind kind);

// "gamescripts/PlayerController.cpp" -> "PlayerController".
//
// That is the same text ENGINE_REGISTER_SCRIPT produced when the script
// registered itself, which is what makes dropping a FILE attach a behaviour
// that is looked up by NAME.
std::string ScriptNameFromPath(std::string_view virtualPath);

// Applies a dropped file to an entity that already exists. Returns false with
// a reason when the drop makes no sense - dropping a scene onto an entity, say
// - so the caller can explain rather than silently doing nothing.
bool ApplyAssetToEntity(eng::EntityId target, std::string_view virtualPath,
                        std::string& outMessage);

// Creates a NEW entity for a dropped image at a position in the world. Used by
// the Scene view. Returns an empty id and a reason if it could not.
eng::EntityId CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                   std::string& outMessage);

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
