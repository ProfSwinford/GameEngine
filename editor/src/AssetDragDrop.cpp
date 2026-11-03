// ============================================================================
//  AssetDragDrop.cpp - what a dropped file does. See AssetDragDrop.h.
// ============================================================================

#include "AssetDragDrop.h"

#include "EditorApp.h"
#include "ScriptBuild.h"

#include <engine/resource/ImportSettings.h>
#include <engine/scene/Prefab.h>
#include <engine/scene/SpriteAnimation.h>

#include <imgui.h>

#include <cctype>
#include <vector>

namespace editor {
namespace {

// "does this filename end with this extension?", ignoring capitals - so
// PLAYER.PNG and player.png are both recognised as images.
bool EndsWithNoCase(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const int a = std::tolower(static_cast<unsigned char>(text[offset + i]));
        const int b = std::tolower(static_cast<unsigned char>(suffix[i]));
        if (a != b) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
//  If this path names an ANIMATION, give the entity something that plays it.
//
//  A FRAME AND AN ANIMATION ARE THE SAME SHAPE OF PATH - "hero.png#f0" and
//  "hero.png#walk" - and ClassifyAsset answers Texture for both, correctly,
//  because each is a picture. So the sprite is set either way by the caller
//  and this adds the second half only when the name is an animation.
//
//  THE TEST IS FindAnimation ON THE SHEET'S SLICE, which is the only thing
//  that can tell the two apart: nothing about the path itself says which it
//  is. Asked here rather than in the component so that dropping a FRAME stays
//  exactly what it was - a still picture, with no component added.
//
//  THE SPRITE KEEPS THE ANIMATION'S OWN PATH as its texture, set by the
//  caller, and that is deliberate: loading an animation's path gives a still
//  of its first surviving frame, so the entity looks right in the editor where
//  nothing is playing, and the two fields agree without anything keeping them
//  in step.
//
//  Silent when there is already one: re-dropping onto an animated entity means
//  "play this instead", which SetAnimationPath does.
// ---------------------------------------------------------------------------
void GiveAnimationIfItIsOne(eng::Entity& entity, std::string_view virtualPath) {
    if (!eng::IsSubAsset(virtualPath)) {
        return;
    }

    const std::string sheet = eng::SheetPathOf(virtualPath);
    const std::string name  = eng::SubAssetNameOf(virtualPath);

    const eng::TextureImport settings = eng::ImportLibrary::ForTexture(sheet);
    if (settings.sheet.FindAnimation(name) == nullptr) {
        return;   // a frame, or a name the sheet does not have
    }

    auto* animation = entity.Find<eng::SpriteAnimationComponent>();
    if (animation == nullptr) {
        animation = static_cast<eng::SpriteAnimationComponent*>(
            entity.AddComponent(eng::SpriteAnimationComponent::kTypeName));
    }
    if (animation == nullptr) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "could not add a SpriteAnimationComponent to '{}'",
                         entity.Name());
        return;
    }

    if (!animation->SetAnimationPath(virtualPath)) {
        // It refuses only a path that is not part of an image, which cannot
        // happen here - IsSubAsset was the first thing asked. Reported rather
        // than ignored, because an impossible branch that stays silent is how
        // a later change to either side goes unnoticed.
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "'{}' refused the animation {}: {}",
                         entity.Name(), virtualPath, animation->Problem());
        return;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "'{}' will play {}", entity.Name(),
                    virtualPath);
}

} // namespace

AssetKind ClassifyAsset(std::string_view virtualPath) {
    // ---- a sliced frame is whatever the image it came out of is -----------
    //
    // "textures/hero.png#walk_1" ends in "lk_1", so every extension test below
    // misses it and it would come back Unknown. That is not a cosmetic
    // mislabel: PayloadIdFor(Unknown) hands out the generic move payload, so a
    // frame could not be dragged onto an entity at all; the Assets panel would
    // give it no thumbnail; and the Inspector would send it down the "this kind
    // of file has no import settings" branch.
    //
    // ONE LINE, AND IT CORRECTS EVERY CALLER. Each of them is asking what KIND
    // of thing a path names, and a frame of an image is an image.
    if (eng::IsSubAsset(virtualPath)) {
        return ClassifyAsset(eng::SheetPathOf(virtualPath));
    }
    // A script is recognised by its extension alone. Everything the browser
    // can see is already under assets/, and every .cpp and .h under assets/ is
    // compiled into the project's script library - so if it is visible here
    // and it is source, it is a script.
    if (EndsWithNoCase(virtualPath, ".cpp") || EndsWithNoCase(virtualPath, ".h") ||
        EndsWithNoCase(virtualPath, ".hpp")) {
        return AssetKind::Script;
    }
    if (EndsWithNoCase(virtualPath, ".png") || EndsWithNoCase(virtualPath, ".bmp") ||
        EndsWithNoCase(virtualPath, ".jpg") || EndsWithNoCase(virtualPath, ".jpeg")) {
        return AssetKind::Texture;
    }
    // A scene is recognised by its extension ALONE - there is deliberately no
    // "and it has to be in scenes/" test any more. .oscene belongs to this
    // engine and nothing else uses it, so the name is already proof; requiring
    // a particular folder would only mean a level organised into
    // scenes/world1/ stopped being draggable for no reason anyone could see.
    //
    // Note what is NOT here: plain .json. Scene files used to be .json, which
    // forced exactly that folder check to tell them apart from every other
    // JSON file under assets/. Giving levels their own extension is what
    // freed .json up for texture and spritesheet metadata - which will need
    // its own branch here when that lands, because it is Unknown today.
    if (EndsWithNoCase(virtualPath, eng::kSceneExtension)) {
        return AssetKind::Scene;
    }
    // A font, which a UI label can be given. .otf is listed because stb reads
    // the TrueType outlines inside one; an OpenType file that uses CFF curves
    // instead is refused when it is baked, and says so rather than drawing
    // nothing.
    if (EndsWithNoCase(virtualPath, ".ttf") || EndsWithNoCase(virtualPath, ".otf")) {
        return AssetKind::Font;
    }
    // A tile material. Its own extension for the same reason .oscene has one:
    // it is JSON, and telling it apart from every other .json under assets/ by
    // folder would mean tiles could only live in one place.
    if (EndsWithNoCase(virtualPath, ".otile")) {
        return AssetKind::Tile;
    }
    // A prefab. Its own extension for the third time and the same reason: it
    // is JSON, and "is this a level, a tile or a prefab?" has to be answerable
    // from the name. See eng::kPrefabExtension.
    if (EndsWithNoCase(virtualPath, eng::kPrefabExtension)) {
        return AssetKind::Prefab;
    }
    return AssetKind::Unknown;
}

const char* PayloadIdFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return kPayloadTexture;
        case AssetKind::Scene:   return kPayloadScene;
        case AssetKind::Script:  return kPayloadScript;
        case AssetKind::Font:    return kPayloadFont;
        case AssetKind::Tile:    return kPayloadTile;
        case AssetKind::Prefab:  return kPayloadPrefab;
        case AssetKind::Unknown: break;
    }
    // Not nullptr. See the header: an unrecognised file is still a file that
    // belongs somewhere, and the Assets panel moves things by dragging them.
    return kPayloadAssetItem;
}

std::string ScriptNameFromPath(std::string_view virtualPath) {
    // ---- a sliced frame is named after both halves ------------------------
    //
    // The last '.' in "hero.png#walk_1" is inside ".png", so the extension
    // strip below would leave "hero" and twelve frames dropped into a scene
    // would arrive as hero, hero_1, hero_2... - the indistinguishable naming
    // the note on PrefabPathFor warns about, and a saved scene records a
    // parent BY NAME.
    if (eng::IsSubAsset(virtualPath)) {
        return ScriptNameFromPath(eng::SheetPathOf(virtualPath)) + "_" +
               eng::SubAssetNameOf(virtualPath);
    }

    std::string_view name = virtualPath;

    // Drop everything up to and including the last '/'.
    if (const std::size_t slash = name.find_last_of('/');
        slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    // Drop the extension.
    if (const std::size_t dot = name.find_last_of('.'); dot != std::string_view::npos) {
        name = name.substr(0, dot);
    }
    return std::string(name);
}

std::string ScriptPathFromName(std::string_view scriptName) {
    if (scriptName.empty()) {
        return {};
    }

    // The same list the compiler is given, so this finds a script wherever it
    // has been put - which is the whole point of the project having no
    // designated scripts folder. See ScriptBuild::GatherSources.
    const ScriptBuild::Sources sources = ScriptBuild::GatherSources();

    // .cpp first. A script defined in a header is the unusual case, and if
    // both somehow exist the compiled one is the one actually being built.
    for (const std::vector<std::string>* list : {&sources.compiled, &sources.headers}) {
        for (const std::string& path : *list) {
            if (ScriptNameFromPath(path) == scriptName) {
                return path;
            }
        }
    }
    return {};
}

bool ApplyAssetToEntity(eng::EntityId target, std::string_view virtualPath,
                        std::string& outMessage) {
    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* entity = scene.Get(target);
    if (entity == nullptr) {
        // The id points at nothing, which is the id system doing its job
        // rather than a bug: an entity can be destroyed between the frame the
        // drag started and the frame it was dropped.
        outMessage = "that entity no longer exists";
        return false;
    }

    switch (ClassifyAsset(virtualPath)) {
        case AssetKind::Texture: {
            auto* sprite = entity->Find<eng::SpriteComponent>();
            if (sprite == nullptr) {
                // Adds the component rather than refusing. Dropping an image
                // onto an entity that has no sprite obviously means "give it
                // one"; making the user press "+ SpriteComponent" first would
                // be pedantry.
                sprite = static_cast<eng::SpriteComponent*>(
                    entity->AddComponent(eng::SpriteComponent::kTypeName));
            }
            if (sprite == nullptr) {
                outMessage = "could not add a SpriteComponent";
                return false;
            }
            sprite->SetTexture(virtualPath);
            GiveAnimationIfItIsOne(*entity, virtualPath);
            EditorState::Get().MarkDirty();
            outMessage = entity->Name() + " now uses " + std::string(virtualPath);
            return true;
        }

        case AssetKind::Script: {
            const std::string scriptName = ScriptNameFromPath(virtualPath);

            // ONE ENTITY, SEVERAL SCRIPTS.
            //
            // This used to be Find<eng::ScriptComponent>(), which returns the
            // first one - so dropping a second script onto an entity that
            // already had one OVERWROTE it. The old script vanished, nothing
            // said so, and the only clue was that a behaviour you set up
            // earlier had quietly stopped happening.
            //
            // Three cases, in order:
            //   - already running this script  -> say so, change nothing
            //   - an empty slot going spare    -> fill it
            //   - otherwise                    -> add another component
            eng::ScriptComponent* script = nullptr;
            for (std::size_t i = 0; i < entity->ComponentCount(); ++i) {
                auto* candidate = dynamic_cast<eng::ScriptComponent*>(entity->ComponentAt(i));
                if (candidate == nullptr) {
                    continue;
                }
                if (candidate->ScriptName() == scriptName) {
                    // Refused rather than duplicated. Running one script twice
                    // on one object does its work twice over - and a dropped
                    // file landing on something that already has it is far
                    // more likely to be a slip than a request.
                    outMessage = entity->Name() + " already runs " + scriptName;
                    return false;
                }
                if (script == nullptr && candidate->ScriptName().empty()) {
                    script = candidate;
                }
            }

            if (script == nullptr) {
                script = static_cast<eng::ScriptComponent*>(
                    entity->AddComponent(eng::ScriptComponent::kTypeName));
            }
            if (script == nullptr) {
                outMessage = "could not add a ScriptComponent";
                return false;
            }
            script->SetScriptName(scriptName);
            EditorState::Get().MarkDirty();

            // ATTACHING WORKS EVEN IF THE SCRIPT HAS NOT BEEN COMPILED YET,
            // and says so. That is the whole design - see ScriptComponent.h -
            // and this message is the only thing standing between "it works"
            // and "it does nothing and I have no idea why".
            outMessage = script->IsResolved()
                             ? entity->Name() + " now runs " + scriptName
                             : entity->Name() + " -> " + scriptName +
                                   " (attached and saved, but not compiled into this "
                                   "build yet - rebuild to run it)";
            return true;
        }

        case AssetKind::Prefab: {
            // ==============================================================
            //  A PREFAB DROPPED ON AN ENTITY BECOMES A CHILD OF IT, which is
            //  what Unity does and is the only reading that makes sense: a
            //  prefab is a thing in the world rather than a setting, so there
            //  is nothing about the target entity for it to change.
            // ==============================================================
            std::string         error;
            const eng::EntityId placed = scene.InstantiatePrefab(virtualPath, error);
            eng::Entity*        instance = scene.Get(placed);
            if (instance == nullptr) {
                outMessage = "could not place that prefab: " + error;
                return false;
            }

            // keepWorldTransform, so it lands ON the entity it was dropped
            // on rather than at whatever its own local numbers mean under a
            // new parent - which for a prefab authored at the origin is a
            // jump to wherever the parent is not.
            instance->Transform().SetParent(&entity->Transform(), true);

            // The record has to agree with the tree, or the next save writes
            // the instance at the top level. See PrefabInstance::parentName.
            if (eng::PrefabInstance* record = scene.PrefabInstanceOf(placed)) {
                record->parentName = entity->Name();
            }

            EditorState::Get().SelectOnly(placed);
            EditorState::Get().MarkDirty();
            outMessage = instance->Name() + " placed under " + entity->Name();
            return true;
        }

        case AssetKind::Scene:
            outMessage = "a scene cannot be attached to an entity - drop it on the "
                         "Scene view to open it";
            return false;

        case AssetKind::Font:
        case AssetKind::Tile:
        case AssetKind::Unknown:
            break;
    }

    outMessage = "the editor does not know what to do with that kind of file";
    return false;
}

bool AcceptAssetDropOnEntity(eng::EntityId target) {
    // BeginDragDropTarget attaches to the item that was drawn most recently,
    // and returns false on every frame nothing is being dragged over it.
    if (!ImGui::BeginDragDropTarget()) {
        return false;
    }

    bool accepted = false;

    // Every kind that can land on an entity is offered here, so a caller does
    // not have to know which ones those are - and adding a new kind is one
    // edit. See ApplyAssetToEntity for what each one does.
    for (const char* payloadId : {kPayloadTexture, kPayloadScript, kPayloadPrefab}) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId);
        if (payload == nullptr) {
            continue;
        }

        // ImGui took a copy of the path when the drag started, so this text is
        // still valid even if the Assets panel has refreshed since.
        const auto* path = static_cast<const char*>(payload->Data);

        std::string message;
        const bool  ok = ApplyAssetToEntity(target, path, message);
        accepted       = accepted || ok;

        // WRITTEN TO THE LOG EITHER WAY. A refused drop that says nothing is
        // the most frustrating outcome a drag-and-drop interface can have,
        // because there is no way to tell it apart from a drag that never
        // started.
        if (ok) {
            ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", message);
        } else {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", message);
        }
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

std::string PrefabPathFor(const eng::Entity& entity, std::string_view virtualDirectory) {
    std::string name = entity.Name();
    if (name.empty()) {
        name = "Prefab";
    }
    // Anything a filename cannot hold becomes an underscore. A name is free
    // text and a filename is not: "Big Door/2" would otherwise be a file in a
    // folder called "Big Door" that nothing ever created.
    for (char& c : name) {
        // '#' IS IN THE LIST FOR A REASON THAT IS NOT THE FILESYSTEM'S. Every
        // other character here is one a filename cannot hold; '#' can, and is
        // replaced because it is what separates an image from one of its
        // sliced frames. An entity called "Boss#2" would otherwise write a
        // real prefabs/Boss#2.oprefab, and the rule in ImportSettings.h is the
        // only thing standing between that and being read as a frame.
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == ' ' || c == '#') {
            c = '_';
        }
    }

    std::string folder(virtualDirectory);
    if (!folder.empty() && folder.back() != '/') {
        folder += '/';
    }
    return folder + name + eng::kPrefabExtension;
}

eng::EntityId CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                   std::string& outMessage) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    // ---- a prefab becomes an instance of itself, where it was dropped -----
    if (ClassifyAsset(virtualPath) == AssetKind::Prefab) {
        std::string         error;
        const eng::EntityId placed = scene.InstantiatePrefab(virtualPath, error);
        eng::Entity*        instance = scene.Get(placed);
        if (instance == nullptr) {
            outMessage = "could not place that prefab: " + error;
            return {};
        }
        instance->Transform().SetWorldPosition(worldPosition);
        EditorState::Get().MarkDirty();
        outMessage = "placed " + instance->Name();
        return placed;
    }

    if (ClassifyAsset(virtualPath) != AssetKind::Texture) {
        outMessage = "only an image or a prefab can be dropped into the scene to make "
                     "something new";
        return {};
    }

    // Named after the file, with a number added if that name is taken.
    // Dropping checker_red.bmp three times gives checker_red, checker_red_1
    // and checker_red_2 rather than three entities the Hierarchy cannot tell
    // apart - and which saving would then get the parenting wrong for, because
    // parents are recorded by name.
    std::string base = ScriptNameFromPath(virtualPath);   // strips folder and extension
    if (base.empty()) {
        base = "Sprite";
    }

    const eng::EntityId id     = scene.CreateEntity(scene.MakeUniqueName(base));
    eng::Entity*        entity = scene.Get(id);
    if (entity == nullptr) {
        outMessage = "the scene could not create an entity";
        return {};
    }

    entity->Transform().SetWorldPosition(worldPosition);

    auto* sprite = static_cast<eng::SpriteComponent*>(
        entity->AddComponent(eng::SpriteComponent::kTypeName));
    if (sprite == nullptr) {
        outMessage = "could not add a SpriteComponent";
        return id;
    }
    sprite->SetTexture(virtualPath);
    GiveAnimationIfItIsOne(*entity, virtualPath);

    EditorState::Get().MarkDirty();
    outMessage = "created " + entity->Name();
    return id;
}

} // namespace editor
