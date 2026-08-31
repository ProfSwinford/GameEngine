// Drag-and-drop payloads and what a dropped asset does. See AssetDragDrop.h.

#include "AssetDragDrop.h"

#include "EditorApp.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace editor {
namespace {

bool EndsWithNoCase(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    const eng::usize offset = text.size() - suffix.size();
    for (eng::usize i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[offset + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

} // namespace

AssetKind ClassifyAsset(std::string_view virtualPath) {
    // Scripts are recognised by extension AND by living under gamescripts/. A .cpp
    // anywhere else is engine or game source that this panel has no business
    // offering to attach to an entity.
    if (virtualPath.starts_with("gamescripts/") &&
        (EndsWithNoCase(virtualPath, ".cpp") || EndsWithNoCase(virtualPath, ".h"))) {
        return AssetKind::Script;
    }
    if (EndsWithNoCase(virtualPath, ".bmp") || EndsWithNoCase(virtualPath, ".png") ||
        EndsWithNoCase(virtualPath, ".jpg") || EndsWithNoCase(virtualPath, ".jpeg")) {
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

    if (const eng::usize slash = name.find_last_of('/'); slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    if (const eng::usize dot = name.find_last_of('.'); dot != std::string_view::npos) {
        name = name.substr(0, dot);
    }
    return std::string(name);
}

bool ApplyAssetToEntity(eng::EntityHandle target, std::string_view virtualPath,
                        std::string& outMessage) {
    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* entity = scene.Get(target);
    if (entity == nullptr) {
        // The handle resolved to nothing, which is the handle system doing its
        // job rather than a bug: the entity can be destroyed between the frame
        // the drag started and the frame it was dropped.
        outMessage = "the entity no longer exists";
        return false;
    }

    switch (ClassifyAsset(virtualPath)) {
        case AssetKind::Texture: {
            auto* sprite = entity->Find<eng::SpriteComponent>();
            if (sprite == nullptr) {
                // ADDS the component rather than refusing. Dropping a texture
                // on an entity that has no sprite obviously means "give it
                // one" - refusing and making the user press + SpriteComponent
                // first would be pedantry.
                sprite = static_cast<eng::SpriteComponent*>(
                    entity->AddComponent(eng::SpriteComponent::kTypeName));
            }
            if (sprite == nullptr) {
                outMessage = "could not add a SpriteComponent";
                return false;
            }
            sprite->SetTexture(virtualPath);
            EditorState::Get().dirty = true;
            outMessage = entity->Name() + " -> " + std::string(virtualPath);
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

            // ATTACHING SUCCEEDS EVEN IF THE SCRIPT IS NOT COMPILED YET, and
            // says so. That is the whole design - see ScriptComponent.h - and
            // the message is the only thing standing between "it works" and
            // "it does nothing and I do not know why".
            outMessage = script->IsResolved()
                             ? entity->Name() + " runs " + scriptName
                             : entity->Name() + " -> " + scriptName +
                                   " (attached, but not compiled into this build yet - "
                                   "rebuild to run it)";
            return true;
        }

        case AssetKind::Scene:
            outMessage = "a scene cannot be attached to an entity - drop it on the "
                         "Scene view to load it";
            return false;

        case AssetKind::Unknown:
            break;
    }

    outMessage = "nothing in this editor knows what to do with that file";
    return false;
}

bool AcceptAssetDropOnEntity(eng::EntityHandle target) {
    if (!ImGui::BeginDragDropTarget()) {
        return false;
    }

    bool accepted = false;

    // Both payload types are offered, so the caller does not have to know which
    // kinds are droppable on an entity - and a new kind is one edit here.
    for (const char* payloadId : {kPayloadTexture, kPayloadScript}) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId);
        if (payload == nullptr) {
            continue;
        }

        // ImGui copied the payload when the drag started, so this string is
        // valid even though the browser may have refreshed since.
        const auto* path = static_cast<const char*>(payload->Data);

        std::string message;
        const bool  ok = ApplyAssetToEntity(target, path, message);
        accepted       = accepted || ok;

        // LOGGED EITHER WAY. A refused drop that says nothing is the single
        // most frustrating outcome in a drag-and-drop interface, because there
        // is no way to tell it apart from a drag that never started.
        if (ok) {
            ENGINE_LOG_INFO(eng::Channels::kEditor, "drop: {}", message);
        } else {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "drop refused: {}", message);
        }
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

eng::EntityHandle CreateEntityForAsset(std::string_view virtualPath, eng::Vec2 worldPosition,
                                       std::string& outMessage) {
    if (ClassifyAsset(virtualPath) != AssetKind::Texture) {
        outMessage = "only a texture can be dropped into the scene to make an entity";
        return {};
    }

    eng::Scene& scene = eng::Engine::Get().GetScene();

    // NAMED AFTER THE FILE, uniquified. Dropping checker_red.bmp three times
    // gives checker_red, checker_red 1, checker_red 2 rather than three
    // entities with the same name - which the Hierarchy could show but nothing
    // could tell apart, and which Scene::Save's parent-by-name would then get
    // wrong.
    std::string base = ScriptNameFromPath(virtualPath);   // same trim: strip dir + extension
    if (base.empty()) {
        base = "Sprite";
    }

    const eng::EntityHandle handle = scene.CreateEntity(scene.MakeUniqueName(base));
    eng::Entity*            entity = scene.Get(handle);
    if (entity == nullptr) {
        outMessage = "the scene refused to create an entity";
        return {};
    }

    entity->Transform().SetWorldPosition(worldPosition);

    auto* sprite = static_cast<eng::SpriteComponent*>(
        entity->AddComponent(eng::SpriteComponent::kTypeName));
    if (sprite == nullptr) {
        outMessage = "could not add a SpriteComponent";
        return handle;
    }
    sprite->SetTexture(virtualPath);

    EditorState::Get().dirty = true;
    outMessage = "created " + entity->Name();
    return handle;
}

} // namespace editor
