// ============================================================================
//  AssetDragDrop.cpp - what a dropped file does. See AssetDragDrop.h.
// ============================================================================

#include "AssetDragDrop.h"

#include "EditorApp.h"

#include <imgui.h>

#include <cctype>

namespace editor {
namespace {

// "does this filename end with this extension?", ignoring capitals - so
// PLAYER.BMP and player.bmp are both recognised as images.
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

} // namespace

AssetKind ClassifyAsset(std::string_view virtualPath) {
    // A script is recognised by its extension alone. Everything the browser
    // can see is already under assets/, and every .cpp and .h under assets/ is
    // compiled into the project's script library - so if it is visible here
    // and it is source, it is a script.
    if (EndsWithNoCase(virtualPath, ".cpp") || EndsWithNoCase(virtualPath, ".h") ||
        EndsWithNoCase(virtualPath, ".hpp")) {
        return AssetKind::Script;
    }
    if (EndsWithNoCase(virtualPath, ".bmp")) {
        return AssetKind::Texture;
    }
    if (EndsWithNoCase(virtualPath, ".json") && virtualPath.starts_with("scenes/")) {
        return AssetKind::Scene;
    }
    return AssetKind::Unknown;
}

const char* PayloadIdFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return kPayloadTexture;
        case AssetKind::Scene:   return kPayloadScene;
        case AssetKind::Script:  return kPayloadScript;
        case AssetKind::Unknown: break;
    }
    return nullptr;
}

std::string ScriptNameFromPath(std::string_view virtualPath) {
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
            EditorState::Get().dirty = true;
            outMessage = entity->Name() + " now uses " + std::string(virtualPath);
            return true;
        }

        case AssetKind::Script: {
            const std::string scriptName = ScriptNameFromPath(virtualPath);

            auto* script = entity->Find<eng::ScriptComponent>();
            if (script == nullptr) {
                script = static_cast<eng::ScriptComponent*>(
                    entity->AddComponent(eng::ScriptComponent::kTypeName));
            }
            if (script == nullptr) {
                outMessage = "could not add a ScriptComponent";
                return false;
            }
            script->SetScriptName(scriptName);
            EditorState::Get().dirty = true;

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

        case AssetKind::Scene:
            outMessage = "a scene cannot be attached to an entity - drop it on the "
                         "Scene view to open it";
            return false;

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

    // Both kinds are offered here, so a caller does not have to know which
    // ones can land on an entity - and adding a new kind is one edit.
    for (const char* payloadId : {kPayloadTexture, kPayloadScript}) {
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

eng::EntityId CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                   std::string& outMessage) {
    if (ClassifyAsset(virtualPath) != AssetKind::Texture) {
        outMessage = "only an image can be dropped into the scene to make a new entity";
        return {};
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

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

    EditorState::Get().dirty = true;
    outMessage = "created " + entity->Name();
    return id;
}

} // namespace editor
