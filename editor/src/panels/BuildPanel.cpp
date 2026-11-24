// ============================================================================
//  BuildPanel.cpp - the build's scene list. See BuildPanel.h.
// ============================================================================

#include "panels/BuildPanel.h"

#include "EditorApp.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace editor {

void BuildPanel::RefreshAvailable() {
    if (!eng::FileSystem::ListFiles("scenes", eng::kSceneExtension, m_available)) {
        m_available.clear();
    }
}

void BuildPanel::MoveEntry(int index, int delta) {
    const int target = index + delta;
    if (index < 0 || target < 0 || index >= static_cast<int>(m_included.size()) ||
        target >= static_cast<int>(m_included.size())) {
        return;
    }
    std::swap(m_included[static_cast<std::size_t>(index)],
              m_included[static_cast<std::size_t>(target)]);
    m_dirty = true;
}

void BuildPanel::Draw() {
    // The engine already read the list at start-up, so the panel adopts that
    // rather than reading the file a second time and risking two answers.
    if (!m_loaded) {
        m_included = eng::BuildScenes::All();
        m_loaded   = true;
        RefreshAvailable();
    }

    // ---- what this list means --------------------------------------------
    ImGui::TextWrapped("Only these scenes are packed into a build. The one at the top "
                       "is the scene the game opens with, and the number beside each "
                       "is what LoadSceneByIndex takes.");
    ImGui::Separator();

    // ---- save / revert ----------------------------------------------------
    ImGui::BeginDisabled(!m_dirty);
    if (ImGui::Button("Save", ImVec2(90, 0))) {
        std::string error;
        if (eng::BuildScenes::Save(m_included, error)) {
            m_dirty = false;
            std::snprintf(m_status, sizeof(m_status), "saved %zu scene(s)",
                          m_included.size());
        } else {
            std::snprintf(m_status, sizeof(m_status), "save failed: %s", error.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(90, 0))) {
        m_included = eng::BuildScenes::All();
        m_dirty    = false;
        std::snprintf(m_status, sizeof(m_status), "reverted to the saved list");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (m_dirty) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "UNSAVED");
    } else if (m_status[0] != '\0') {
        ImGui::TextDisabled("%s", m_status);
    }

    ImGui::Separator();

    // ---- the list ---------------------------------------------------------
    if (m_included.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "No scenes in the build.");
        ImGui::TextWrapped("Exporting now would fall back to packing every scene in "
                           "assets/scenes/ and warn about it. Add the ones you want "
                           "below, starting with the one the game should open on.");
    }

    // ======================================================================
    //  THERE IS NO "OPEN SCENE" WHILE THE GAME IS RUNNING.
    //
    //  During a session the scene is whatever the playtest is in, which after
    //  a script's RequestScene is a different file from the one being edited.
    //  "Add the open scene" wrote THAT into build.json - and if the list was
    //  empty it became index 0, the scene the shipped game opens with. The
    //  highlight below told the same lie more quietly.
    //
    //  BY VALUE, not by reference: the empty case is a temporary, and a
    //  reference would outlive it.
    // ======================================================================
    const bool        playing = eng::Engine::Get().IsInPlayMode();
    const std::string openScene =
        playing ? std::string{} : eng::Engine::Get().GetScene().SourcePath();
    int removeAt = -1;

    for (int i = 0; i < static_cast<int>(m_included.size()); ++i) {
        const std::string& path = m_included[static_cast<std::size_t>(i)];
        ImGui::PushID(i);

        // The index, which is the whole point of the ordering.
        ImGui::TextDisabled("%2d", i);
        ImGui::SameLine();

        // Up and down rather than drag-and-drop. Reordering by dragging inside
        // a list is genuinely fiddly to get right in immediate mode, and two
        // arrows cannot be half-done or dropped in the wrong place.
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
            MoveEntry(i, -1);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(i + 1 >= static_cast<int>(m_included.size()));
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
            MoveEntry(i, +1);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::SmallButton("Remove")) {
            removeAt = i;
        }
        ImGui::SameLine();

        // Open it for editing. The list is about the build, not about what you
        // are allowed to look at.
        if (ImGui::SmallButton("Open")) {
            EditorState::Get().requestedScene = path;
        }
        ImGui::SameLine();

        const bool isOpen  = (path == openScene);
        const bool isFirst = (i == 0);
        if (isOpen) {
            ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.98f, 1.0f), "%s", path.c_str());
        } else {
            ImGui::TextUnformatted(path.c_str());
        }

        if (isFirst) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.50f, 1.0f), "(loads first)");
        }

        // A scene on the list whose file has gone is worth saying, because the
        // export will fail to pack it and the game will open on nothing.
        if (!eng::FileSystem::Exists(path)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "- MISSING");
        }

        ImGui::PopID();
    }

    if (removeAt >= 0) {
        m_included.erase(m_included.begin() + removeAt);
        m_dirty = true;
    }

    // ---- adding -----------------------------------------------------------
    ImGui::Separator();

    if (ImGui::Button("Rescan assets/scenes/")) {
        RefreshAvailable();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("Add the open scene") && !openScene.empty()) {
        if (std::find(m_included.begin(), m_included.end(), openScene) ==
            m_included.end()) {
            m_included.push_back(openScene);
            m_dirty = true;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && playing) {
        ImGui::SetTooltip("Not while playing - the scene on screen is a copy that "
                          "Stop puts back.");
    }

    if (ImGui::TreeNodeEx("Scenes not in the build", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool anyOffered = false;
        for (const std::string& path : m_available) {
            if (std::find(m_included.begin(), m_included.end(), path) !=
                m_included.end()) {
                continue;   // already in
            }
            anyOffered = true;
            ImGui::PushID(path.c_str());
            if (ImGui::SmallButton("Add")) {
                m_included.push_back(path);
                m_dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(path.c_str());
            ImGui::PopID();
        }
        if (!anyOffered) {
            ImGui::TextDisabled("every scene in assets/scenes/ is in the build");
        }
        ImGui::TreePop();
    }
}

} // namespace editor
