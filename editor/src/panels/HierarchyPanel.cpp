// ============================================================================
//  HierarchyPanel.cpp - the tree of entities. See HierarchyPanel.h.
// ============================================================================

#include "panels/HierarchyPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"

#include <imgui.h>

#include <cstdio>

namespace editor {

void HierarchyPanel::DrawNode(eng::Entity& entity) {
    EditorState&      state     = EditorState::Get();
    eng::Scene&       scene     = eng::Engine::Get().GetScene();
    eng::Transform2D& transform = entity.Transform();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;

    // An entity with no children is a leaf: no expand arrow, and no matching
    // TreePop needed later.
    if (transform.Children().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (state.selected == entity.Id()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // The hidden part after ## is the entity's id rather than its name,
    // because two entities can end up displaying the same name and ImGui would
    // treat them as one row. See the note about widget identity in Panel.h.
    char label[192];
    std::snprintf(label, sizeof(label), "%s##%d_%d", entity.Name().c_str(),
                  entity.Id().index, entity.Id().generation);

    const bool open = ImGui::TreeNodeEx(label, flags);

    // IsItemToggledOpen tells clicking the expand arrow apart from clicking
    // the row, so opening a branch does not also change the selection.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        state.selected = entity.Id();   // an ID. Never a pointer.
    }

    // A drop target for this row, placed immediately after the row was drawn
    // because ImGui attaches drop targets to the most recently drawn item.
    // Drop a texture here to give this entity a sprite; drop a .cpp to attach
    // that script.
    AcceptAssetDropOnEntity(entity.Id());

    DrawContextMenu(entity);

    if (open && !transform.Children().empty()) {
        for (eng::Transform2D* child : transform.Children()) {
            // The transform tree knows about parenting; the scene's list of
            // entities is flat. So finding the child ENTITY for a child
            // TRANSFORM means searching for the entity whose transform this is.
            eng::Entity* childEntity = nullptr;
            scene.ForEach([&](eng::Entity& candidate) {
                if (&candidate.Transform() == child) {
                    childEntity = &candidate;
                }
            });
            if (childEntity != nullptr) {
                DrawNode(*childEntity);   // recursion: a child may have children
            }
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::DrawContextMenu(eng::Entity& entity) {
    // BeginPopupContextItem opens a menu when the previous item is
    // right-clicked, and returns false on every frame it is not open.
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }
    eng::Scene& scene = eng::Engine::Get().GetScene();

    if (ImGui::MenuItem("Rename...")) {
        m_renameTarget    = entity.Id();
        m_openRenamePopup = true;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", entity.Name().c_str());
    }

    if (ImGui::MenuItem("Duplicate")) {
        std::string        error;
        const eng::EntityId copy = scene.DuplicateEntity(entity.Id(), error);
        if (copy.IsNull()) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not duplicate: {}", error);
        } else {
            // Select the copy, because the next thing anybody does after
            // duplicating something is move it.
            EditorState::Get().selected = copy;
            EditorState::Get().dirty    = true;
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Destroy")) {
        // Through the DEFERRED QUEUE, not straight away. Deleting an entity
        // from inside a panel, mid-frame, is exactly the problem DeferredOps
        // exists to prevent - and the editor does not get an exemption from
        // the engine's own rules. The entity disappears at the end of the tick.
        eng::DeferredOps::QueueDestroy(entity.Id());
        EditorState::Get().dirty = true;
    }
    ImGui::EndPopup();
}

void HierarchyPanel::DrawRenamePopup() {
    if (m_openRenamePopup) {
        ImGui::OpenPopup("Rename Entity");
        m_openRenamePopup = false;
    }

    // Centred, because a dialog that opens under the mouse in a docked editor
    // is as likely to be half off the screen as not.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Rename Entity", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* target = scene.Get(m_renameTarget);

    if (target == nullptr) {
        // Destroyed while the dialog was open. Noticed, not crashed on - which
        // is the whole reason the dialog remembers an id.
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

    // Names are how the scene looks entities up, so two entities cannot share
    // one. Refused up front rather than after pressing the button.
    const bool taken = !scene.Find(m_renameBuffer).IsNull() &&
                       scene.Find(m_renameBuffer) != m_renameTarget;
    if (taken) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                           "something else is already called that");
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
        const eng::EntityId created = scene.CreateEntity(scene.MakeUniqueName("Entity"));
        if (!created.IsNull()) {
            // Put it where the camera is looking, so it is on screen rather
            // than at the world origin somewhere off in the distance.
            if (eng::Entity* entity = scene.Get(created); entity != nullptr) {
                entity->Transform().SetLocalPosition(
                    eng::Engine::Get().GetCamera().Position());
            }
            EditorState::Get().selected = created;
            EditorState::Get().dirty    = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(appears where the camera is looking)");

    ImGui::SetNextItemWidth(-1.0f);   // -1 means "use all the remaining width"
    ImGui::InputTextWithHint("##filter", "filter by name...", m_filter, sizeof(m_filter));

    ImGui::TextDisabled("%zu entities", scene.EntityCount());
    ImGui::Separator();

    const bool filtering = m_filter[0] != '\0';

    if (filtering) {
        // A flat list while filtering. A tree with most of its branches hidden
        // is harder to read than a list, and the point of a filter is to find
        // one thing.
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Name().find(m_filter) == std::string::npos) {
                return;
            }
            char label[192];
            std::snprintf(label, sizeof(label), "%s##%d_%d", entity.Name().c_str(),
                          entity.Id().index, entity.Id().generation);
            if (ImGui::Selectable(label, EditorState::Get().selected == entity.Id())) {
                EditorState::Get().selected = entity.Id();
            }
            DrawContextMenu(entity);
        });
    } else {
        // Only the entities with no parent are drawn here; DrawNode draws each
        // one's children itself.
        scene.ForEach([&](eng::Entity& entity) {
            if (entity.Transform().Parent() == nullptr) {
                DrawNode(entity);
            }
        });
    }

    // Outline whatever is selected, in the Scene view.
    //
    // Looked up from the ID every frame, which is the rule. A selection that
    // was destroyed simply comes back as nothing and no outline is drawn.
    if (eng::Entity* selected = scene.Get(EditorState::Get().selected);
        selected != nullptr) {
        const eng::Vec2 position = selected->Transform().WorldPosition();
        eng::Gizmos::Box(
            eng::AABB::FromCenterHalfExtents(position, eng::Vec2{22.0f, 22.0f}),
            eng::Color::Yellow(), 0.0f, eng::GizmoSpace::World,
            eng::GizmoCategory::Bounds);

        // ABOVE the outline rather than beside it. Beside it is where the
        // Scene view puts its move handle, and a name label sitting on top of
        // the handle you are trying to grab makes a tool feel broken.
        eng::Gizmos::Text(position + eng::Vec2{-20.0f, 34.0f}, selected->Name(),
                          eng::Color::Yellow(), 0.0f, eng::GizmoSpace::World,
                          eng::GizmoCategory::Bounds);
    }

    DrawRenamePopup();
}

} // namespace editor
