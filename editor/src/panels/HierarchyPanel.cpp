// WEEK 9 PANELS - hierarchy and resource browser. See HierarchyPanel.h.

#include "panels/HierarchyPanel.h"

#include "AssetDragDrop.h"

#include "EditorApp.h"

#include <imgui.h>

#include <cstring>

namespace editor {

void HierarchyPanel::DrawNode(eng::Entity& entity) {
    EditorState& state  = EditorState::Get();
    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Transform2D& transform = entity.Transform();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (transform.Children().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (state.selected == entity.Handle()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // The ## id is the entity's handle value, not its name: two entities may
    // legitimately end up with the same display name after a deferred spawn
    // appends a suffix, and ImGui would treat them as one node.
    char label[160];
    std::snprintf(label, sizeof(label), "%s##%u", entity.Name().c_str(),
                  entity.Handle().value);

    const bool open = ImGui::TreeNodeEx(label, flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        state.selected = entity.Handle();   // A HANDLE. Never a pointer.
    }

    // A DROP TARGET PER ROW. Immediately after TreeNodeEx, because the target
    // binds to the last submitted item, and the row is what the user is aiming
    // at. Drop a script here to attach it; drop a texture to give this entity
    // that sprite.
    //
    // The handle is captured, NOT the Entity*: this runs inside a ForEach over
    // the scene, and what is attached may add a component and reallocate.
    AcceptAssetDropOnEntity(entity.Handle());

    DrawContextMenu(entity);

    if (open && !transform.Children().empty()) {
        for (eng::Transform2D* child : transform.Children()) {
            // Walking child transforms back to their entities: the transform
            // tree is the authority on parenting, and the scene's slot array
            // is flat, so the tree view has to come from here.
            eng::Entity* childEntity = nullptr;
            scene.ForEach([&](eng::Entity& candidate) {
                if (&candidate.Transform() == child) {
                    childEntity = &candidate;
                }
            });
            if (childEntity != nullptr) {
                DrawNode(*childEntity);
            }
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::DrawContextMenu(eng::Entity& entity) {
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }
    eng::Scene& scene = eng::Engine::Get().GetScene();

    if (ImGui::MenuItem("Rename...")) {
        m_renameTarget    = entity.Handle();
        m_openRenamePopup = true;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", entity.Name().c_str());
    }

    if (ImGui::MenuItem("Duplicate")) {
        std::string error;
        const eng::EntityHandle copy = scene.DuplicateEntity(entity.Handle(), error);
        if (copy.IsNull()) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "duplicate failed: {}", error);
        } else {
            // Select the copy, because the next thing anyone does after
            // duplicating is move it.
            EditorState::Get().selected = copy;
            EditorState::Get().dirty    = true;
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Destroy")) {
        // THROUGH THE DEFERRED QUEUE. Deleting directly from a panel is
        // exactly the iterator-invalidation bug DeferredOps exists to
        // prevent, and THE IDE IS NOT EXEMPT FROM THE ENGINE'S RULES.
        eng::DeferredOps::QueueDestroy(entity.Handle());
        EditorState::Get().dirty = true;
    }
    ImGui::EndPopup();
}

void HierarchyPanel::DrawRenamePopup() {
    if (m_openRenamePopup) {
        ImGui::OpenPopup("Rename Entity");
        m_openRenamePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Rename Entity", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* target = scene.Get(m_renameTarget);

    if (target == nullptr) {
        // Destroyed while the dialog was open. Detected, not dereferenced.
        ImGui::TextUnformatted("that entity no longer exists");
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // Names index the scene's lookup map, so a duplicate is refused rather
    // than silently shadowing the entity that already had it.
    const bool taken = !scene.Find(m_renameBuffer).IsNull() &&
                       scene.Find(m_renameBuffer) != m_renameTarget;
    if (taken) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "that name is already used");
    }

    ImGui::BeginDisabled(taken || m_renameBuffer[0] == '\0');
    if ((submitted && !taken) || ImGui::Button("Rename", ImVec2(120, 0))) {
        if (scene.RenameEntity(m_renameTarget, m_renameBuffer)) {
            EditorState::Get().dirty = true;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void HierarchyPanel::Draw() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    if (ImGui::Button("+ Create Entity")) {
        const eng::EntityHandle created = scene.CreateEntity(scene.MakeUniqueName("Entity"));
        if (!created.IsNull()) {
            // Every entity gets a transform on demand; touching it here means a
            // newly created entity has one immediately rather than the first
            // time something asks, which matters because the Inspector is about
            // to ask.
            if (eng::Entity* entity = scene.Get(created); entity != nullptr) {
                entity->Transform().SetLocalPosition(
                    eng::Engine::Get().GetCamera().Position());
            }
            EditorState::Get().selected = created;
            EditorState::Get().dirty    = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(spawns at the camera centre)");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "filter by name...", m_filter, sizeof(m_filter));

    ImGui::TextDisabled("%zu entities", scene.EntityCount());
    ImGui::Separator();

    const bool filtering = m_filter[0] != '\0';

    if (filtering) {
        // A flat list while filtering: a tree with most of its nodes hidden is
        // harder to read than a list, and the point of the filter is to find
        // one thing.
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Name().find(m_filter) == std::string::npos) {
                return;
            }
            char label[160];
            std::snprintf(label, sizeof(label), "%s##%u", entity.Name().c_str(),
                          entity.Handle().value);
            if (ImGui::Selectable(label, EditorState::Get().selected == entity.Handle())) {
                EditorState::Get().selected = entity.Handle();
            }
            DrawContextMenu(entity);
        });
    } else {
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Transform().Parent() == nullptr) {
                DrawNode(entity);   // roots only; children come from the tree
            }
        });
    }

    // The selected entity's bounds, highlighted through DebugDraw. One call,
    // and it is what makes selection feel real rather than like a list row.
    //
    // Resolved from the HANDLE every frame - which is the rule. A destroyed
    // selection simply resolves to null and nothing is drawn.
    if (eng::Entity* selected = scene.Get(EditorState::Get().selected);
        selected != nullptr) {
        const eng::Vec2 position = selected->Transform().WorldPosition();
        eng::DebugDraw::Box(eng::AABB::FromCenterHalfExtents(position,
                                                             eng::Vec2{22.0f, 22.0f}),
                            eng::Color::Yellow(), 0.0f, eng::DebugSpace::World,
                            eng::DebugCategory::Bounds);
        // ABOVE the box rather than beside it. Beside it is where the Scene
        // view's gizmo puts its X-axis handle, and a name label sitting on top
        // of the handle you are trying to grab is the kind of small thing that
        // makes a tool feel broken.
        eng::DebugDraw::Text(position + eng::Vec2{-20.0f, 34.0f}, selected->Name().c_str(),
                             eng::Color::Yellow(), 0.0f, eng::DebugSpace::World,
                             eng::DebugCategory::Bounds);
    }

    DrawRenamePopup();
}


} // namespace editor
