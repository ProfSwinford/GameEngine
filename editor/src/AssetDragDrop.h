#pragma once
// =============================================================================
//  THE DRAG-AND-DROP VOCABULARY, in one place.
//
//  ImGui's drag-and-drop matches a source to a target by a STRING id, and a
//  typo in that string does not fail loudly - the drop is simply never
//  accepted, and there is nothing on screen to explain why. That is a bad
//  half-hour, and it is entirely avoidable by having exactly one definition of
//  each id that both ends include.
//
//  ---------------------------------------------------------------------------
//  WHAT AN ASSET DROP MEANS depends on where it lands, and the rules are:
//
//    texture -> Scene view     create an entity there, with that sprite
//    texture -> Hierarchy row  give THAT entity the sprite (adding the
//                              component if it has none)
//    texture -> Inspector      the same, for the selected entity
//    script  -> Hierarchy row  attach a ScriptComponent bound to that script
//    script  -> Inspector      the same, for the selected entity
//    scene   -> Scene view     load it (after the unsaved-changes prompt)
//
//  The two "apply to an entity" cases are identical in effect and would be
//  easy to write twice with a subtle difference; ApplyAssetToEntity is one
//  implementation so they cannot drift.
// =============================================================================

#include <engine/Engine.h>

#include <string>

namespace editor {

// ImGui payload ids. Under 32 characters, which is ImGui's limit.
inline constexpr const char* kPayloadTexture = "ASSET_TEXTURE";
inline constexpr const char* kPayloadScene   = "ASSET_SCENE";
inline constexpr const char* kPayloadScript  = "ASSET_SCRIPT";

enum class AssetKind {
    Unknown,
    Texture,
    Scene,
    Script,
};

// By EXTENSION, not by sniffing the contents. A browser has to classify
// hundreds of files per frame while the user scrolls, and opening each one to
// find out what it is would make the panel the slowest thing in the editor.
AssetKind ClassifyAsset(std::string_view virtualPath);

// The ImGui payload id for a kind, or nullptr for a kind that cannot be
// dragged. One switch, so a new asset kind is one edit rather than three.
const char* PayloadIdFor(AssetKind kind);

// The bare name a script binds by: "gamescripts/PlayerController.cpp" ->
// "PlayerController". This is the same string ENGINE_REGISTER_SCRIPT produced,
// which is what makes dropping a FILE attach a behaviour registered by NAME.
std::string ScriptNameFromPath(std::string_view virtualPath);

// Applies a dropped asset to an existing entity. Returns false with a reason
// when the drop makes no sense - dropping a scene onto an entity, say - so the
// caller can say so rather than silently doing nothing.
bool ApplyAssetToEntity(eng::EntityHandle target, std::string_view virtualPath,
                        std::string& outMessage);

// Creates a new entity for a dropped asset at a world position. Used by the
// Scene view. Returns a null handle and a reason on failure.
eng::EntityHandle CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                       std::string& outMessage);

// The whole target side, for the LAST SUBMITTED ImGui ITEM: begins the target,
// accepts a texture or a script, applies it, logs the outcome, ends the
// target. Call it directly after the widget the drop should land on.
//
// One function so the Hierarchy row and the Inspector window cannot disagree
// about which payloads they accept - the failure mode being a panel where
// scripts drop and textures mysteriously do not.
//
// Returns true if something was accepted, for a caller that wants to react.
bool AcceptAssetDropOnEntity(eng::EntityHandle target);

} // namespace editor
