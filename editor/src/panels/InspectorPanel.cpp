// WEEK 10 PANELS - inspector and toolbar. See InspectorPanel.h for the three
// recorded answers about editing.

#include "panels/InspectorPanel.h"

#include "AssetDragDrop.h"

#include "EditorApp.h"

#include <imgui.h>
#include <imgui_internal.h>   // GetCurrentWindow, for a window-wide drop target

#include <cstring>

namespace editor {
namespace {

void DrawTransform(eng::TransformComponent& component, bool running) {
    eng::Transform2D& transform = component.Transform();

    // ANSWER 2: while the simulation is running, a movement system may own
    // this. It is shown but not editable, and the reason is said out loud
    // rather than left as a mystery.
    ImGui::BeginDisabled(running);

    float position[2] = {transform.LocalPosition().x, transform.LocalPosition().y};
    if (ImGui::DragFloat2("Position", position, 0.5f)) {
        transform.SetLocalPosition(eng::Vec2{position[0], position[1]});
        EditorState::Get().dirty = true;
    }

    float degrees = transform.LocalRotation() * eng::kRadToDeg;
    if (ImGui::DragFloat("Rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
        transform.SetLocalRotation(degrees * eng::kDegToRad);
        EditorState::Get().dirty = true;
    }

    float scale[2] = {transform.LocalScale().x, transform.LocalScale().y};
    if (ImGui::DragFloat2("Scale", scale, 0.01f)) {
        transform.SetLocalScale(eng::Vec2{scale[0], scale[1]});
        EditorState::Get().dirty = true;
    }

    ImGui::EndDisabled();
    if (running) {
        ImGui::TextDisabled("read-only while running: a system may own these. Pause to "
                            "edit.");
    }

    const eng::Vec2 world = transform.WorldPosition();
    ImGui::TextDisabled("world position: %.2f, %.2f   depth %d",
                        static_cast<double>(world.x), static_cast<double>(world.y),
                        transform.Depth());
}

void DrawSprite(eng::SpriteComponent& sprite) {
    char path[192];
    std::snprintf(path, sizeof(path), "%s", sprite.TexturePath().c_str());
    if (ImGui::InputText("Texture", path, sizeof(path),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        sprite.SetTexture(path);
        EditorState::Get().dirty = true;   // Acquire-then-Release; see Component.cpp
    }
    ImGui::TextDisabled("press Enter to reload");

    const eng::Color tint = sprite.Tint();
    float rgba[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", rgba)) {
        sprite.SetTint(eng::Color{static_cast<eng::u8>(rgba[0] * 255.0f),
                                  static_cast<eng::u8>(rgba[1] * 255.0f),
                                  static_cast<eng::u8>(rgba[2] * 255.0f),
                                  static_cast<eng::u8>(rgba[3] * 255.0f)});
        EditorState::Get().dirty = true;
    }

    int layer = sprite.Layer();
    if (ImGui::DragInt("Layer", &layer, 0.2f)) {
        sprite.SetLayer(layer);
        EditorState::Get().dirty = true;
    }

    if (eng::Texture* texture = eng::ResourceManager::Get(sprite.GetTexture());
        texture != nullptr && texture->native != nullptr) {
        ImGui::Image(reinterpret_cast<ImTextureID>(texture->native), ImVec2(64, 64));
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d\n%s", texture->width, texture->height,
                            eng::ToString(texture->state));
    }
}

// THE LAYER MASK GRID - a Milestone 4 verification item. Uncheck a box and the
// collision events for that pair stop; check it and they resume, with no
// rebuild and no relaunch.
void DrawLayerMask(const char* label, eng::CollisionLayer& mask) {
    ImGui::TextUnformatted(label);
    for (eng::u32 bit = 0; bit < eng::CollisionLayers::kNamedLayerCount; ++bit) {
        const eng::CollisionLayer flag = 1u << bit;
        bool on = (mask & flag) != 0;
        if (bit % 4 != 0) {
            ImGui::SameLine();
        }
        // ## plus the label and the bit: two grids in one window would
        // otherwise share widget ids, which is the ImGui trap from Panel.h.
        char id[64];
        std::snprintf(id, sizeof(id), "%s##%s_%u", eng::CollisionLayers::Name(bit), label,
                      bit);
        if (ImGui::Checkbox(id, &on)) {
            mask = on ? (mask | flag) : (mask & ~flag);
        }
    }
}

void DrawCollider(eng::ColliderComponent& collider) {
    bool trigger = collider.IsTrigger();
    if (ImGui::Checkbox("Trigger", &trigger)) {
        collider.SetTrigger(trigger);
        EditorState::Get().dirty = true;
    }

    float offset[2] = {collider.Offset().x, collider.Offset().y};
    if (ImGui::DragFloat2("Offset", offset, 0.5f)) {
        collider.SetOffset(eng::Vec2{offset[0], offset[1]});
        EditorState::Get().dirty = true;
    }

    if (collider.Shape() == eng::ColliderShape::Box) {
        auto& box = static_cast<eng::AABBColliderComponent&>(collider);
        float half[2] = {box.HalfExtents().x, box.HalfExtents().y};
        if (ImGui::DragFloat2("Half extents", half, 0.5f, 0.0f, 10000.0f)) {
            box.SetHalfExtents(eng::Vec2{half[0], half[1]});
            EditorState::Get().dirty = true;
        }
    } else {
        auto& circle = static_cast<eng::CircleColliderComponent&>(collider);
        float radius = circle.Radius();
        if (ImGui::DragFloat("Radius", &radius, 0.5f, 0.0f, 10000.0f)) {
            circle.SetRadius(radius);
            EditorState::Get().dirty = true;
        }
    }

    ImGui::SeparatorText("Layers and mask");
    eng::CollisionLayer layer = collider.Layer();
    DrawLayerMask("Layer (what it is)", layer);
    collider.SetLayer(layer);

    eng::CollisionLayer mask = collider.Mask();
    DrawLayerMask("Mask (what it cares about)", mask);
    collider.SetMask(mask);

    ImGui::TextDisabled("A pair is tested only if BOTH masks include the other's layer.");
}

// The one field that makes the Milestone 1 scene move. Editable here because
// the argument SpinComponent.h makes - that three numbers in a data file
// produce a three-deep orbiting system - is far more convincing when you can
// drag one of them and watch the whole hierarchy respond.
//
// EDITED IN DEGREES, stored in radians. Every angle the user sees in this
// editor is degrees; every angle the engine holds is radians. The conversion
// happens at the UI, exactly once, the same way DrawTransform does it.
void DrawSpin(eng::SpinComponent& spin) {
    float degrees = spin.RadiansPerSecond() * eng::kRadToDeg;
    if (ImGui::DragFloat("Degrees/sec", &degrees, 1.0f, -720.0f, 720.0f, "%.1f")) {
        spin.SetRadiansPerSecond(degrees * eng::kDegToRad);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("0")) {
        spin.SetRadiansPerSecond(0.0f);
        EditorState::Get().dirty = true;
    }

    ImGui::TextDisabled("%.4f rad/s. Negative spins clockwise.",
                        static_cast<double>(spin.RadiansPerSecond()));
    ImGui::TextDisabled("Children ORBIT this entity - a child's world matrix is "
                        "local * parentWorld, so spinning a parent sweeps everything "
                        "under it. There is no orbit code.");
}

// The script binding. Two states, and the difference between them is the whole
// reason the component stores a name rather than a type.
void DrawScript(eng::ScriptComponent& script) {
    const bool resolved = script.IsResolved();

    if (resolved) {
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.60f, 1.0f), "%s",
                           script.ScriptName().c_str());
        ImGui::TextDisabled("compiled in and running");
    } else {
        // RED AND EXPLICIT. A script that silently does nothing because it was
        // never compiled is the single worst failure this feature can have, so
        // it is the loudest thing in the panel.
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s  -  UNRESOLVED",
                           script.ScriptName().c_str());
        ImGui::TextWrapped("No script by that name is compiled into this build, so it is "
                           "attached and saved but does not run. Build the project and "
                           "relaunch - the binding is by name, so nothing needs "
                           "reattaching.");
    }

    // The list of what IS available, so a typo is one click from fixed rather
    // than a hunt through the gamescripts folder.
    if (ImGui::BeginCombo("Bind to", script.ScriptName().c_str())) {
        if (eng::ScriptRegistry::Count() == 0) {
            ImGui::TextDisabled("no scripts are compiled into this build");
        }
        eng::ScriptRegistry::ForEachScript([&](const char* name) {
            if (ImGui::Selectable(name, script.ScriptName() == name)) {
                script.SetScriptName(name);
                EditorState::Get().dirty = true;
            }
        });
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("Drag a .cpp from the Assets panel onto this window to rebind.");
}

} // namespace

void InspectorPanel::Draw() {
    EditorState& state = EditorState::Get();
    eng::Scene&  scene = eng::Engine::Get().GetScene();

    // RESOLVED FROM THE HANDLE, EVERY FRAME. A destroyed selection is detected
    // and reported here rather than dereferenced - which is the whole reason
    // the selection is a handle.
    eng::Entity* entity = scene.Get(state.selected);
    if (entity == nullptr) {
        if (!state.selected.IsNull()) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "the selected entity no longer exists");
            ImGui::TextDisabled("(handle index %u generation %u - stale, not dangling)",
                                state.selected.Index(), state.selected.Generation());
            if (ImGui::Button("Clear selection")) {
                state.selected = eng::EntityHandle{};
            }
        } else {
            ImGui::TextDisabled("select an entity in the Hierarchy.");
        }
        return;
    }

    // ---- THE WHOLE WINDOW IS A DROP TARGET -------------------------------
    //
    // Not a strip, not a header - the entire panel, so a script can be dropped
    // anywhere on it. That needs the WINDOW rect rather than an item, which is
    // what SetNextWindowUseDropTarget... does not exist for; the documented
    // way is a zero-size dummy over the window rect. ImGui's own demo uses
    // this pattern.
    //
    // Placed before the fields rather than after, so it does not fight the
    // widgets for the drop - an item-level target under the cursor wins.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                         ImGui::GetID("##inspector_drop"))) {
        std::string message;
        for (const char* payloadId : {kPayloadTexture, kPayloadScript}) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId);
            if (payload == nullptr) {
                continue;
            }
            const auto* path = static_cast<const char*>(payload->Data);
            if (ApplyAssetToEntity(state.selected, path, message)) {
                ENGINE_LOG_INFO(eng::Channels::kEditor, "inspector drop: {}", message);
            } else {
                ENGINE_LOG_WARN(eng::Channels::kEditor, "inspector drop refused: {}",
                                message);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool running = !eng::Engine::Get().Clock().IsPaused();

    ImGui::Text("%s", entity->Name().c_str());
    ImGui::TextDisabled("handle: index %u, generation %u", entity->Handle().Index(),
                        entity->Handle().Generation());

    ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
    if (ImGui::Button("Destroy")) {
        // ANSWER 3: THROUGH THE DEFERRED QUEUE. The IDE is not exempt.
        eng::DeferredOps::QueueDestroy(entity->Handle());
    }

    ImGui::Separator();

    entity->ForEachComponent([&](eng::Component& component) {
        ImGui::PushID(&component);
        if (ImGui::CollapsingHeader(component.TypeName(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (component.TypeId() == eng::TransformComponent::TypeIdStatic()) {
                DrawTransform(static_cast<eng::TransformComponent&>(component), running);
            } else if (component.TypeId() == eng::SpriteComponent::TypeIdStatic()) {
                DrawSprite(static_cast<eng::SpriteComponent&>(component));
            } else if (component.TypeId() == eng::AABBColliderComponent::TypeIdStatic() ||
                       component.TypeId() == eng::CircleColliderComponent::TypeIdStatic()) {
                DrawCollider(static_cast<eng::ColliderComponent&>(component));
            } else if (component.TypeId() == eng::SpinComponent::TypeIdStatic()) {
                DrawSpin(static_cast<eng::SpinComponent&>(component));
            } else if (component.TypeId() == eng::ScriptComponent::TypeIdStatic()) {
                DrawScript(static_cast<eng::ScriptComponent&>(component));
            } else {
                // An unknown component type is not an error - a gameplay layer
                // may define its own, and the Inspector should say so rather
                // than pretend it does not exist.
                ImGui::TextDisabled("no inspector for this component type yet");
            }
        }
        ImGui::PopID();
    });

    ImGui::Separator();
    ImGui::SeparatorText("Add component");
    eng::ComponentFactory::ForEachType([&](const char* typeName) {
        if (entity->FindComponent(eng::StringId(typeName)) != nullptr) {
            return;   // already has one
        }
        char label[96];
        std::snprintf(label, sizeof(label), "+ %s", typeName);
        if (ImGui::SmallButton(label)) {
            entity->AddComponent(typeName);
            EditorState::Get().dirty = true;
        }
    });
}

} // namespace editor
