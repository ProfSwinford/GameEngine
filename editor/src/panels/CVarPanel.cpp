// WEEK 8 PANEL - the CVar editor. See CVarPanel.h.

#include "panels/CVarPanel.h"

#include <engine/core/CVar.h>
#include <engine/core/Log.h>

#include <imgui.h>

#include <cstring>

namespace editor {
namespace {

bool Matches(const std::string& name, const char* filter) {
    if (filter == nullptr || filter[0] == '\0') {
        return true;
    }
    return name.find(filter) != std::string::npos;
}

void Tooltip(const eng::CVar& variable) {
    // The description is not decoration. In Phase 2 there will be forty of
    // these and no memory of what half of them do.
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted(variable.Description().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        ImGui::TextDisabled("%s  |  %s", variable.Name().c_str(),
                            eng::ToString(variable.Type()));
        ImGui::EndTooltip();
    }
}

} // namespace

void CVarPanel::Draw() {
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "filter by name...", m_filter, sizeof(m_filter));
    ImGui::SameLine();
    ImGui::Checkbox("Modified only", &m_modifiedOnly);

    ImGui::SameLine();
    if (ImGui::Button("Save to config")) {
        std::string error;
        if (eng::CVarRegistry::SaveToConfig("config/engine.json", error)) {
            std::snprintf(m_status, sizeof(m_status), "saved to config/engine.json");
        } else {
            std::snprintf(m_status, sizeof(m_status), "save failed: %s", error.c_str());
        }
    }
    if (m_status[0] != '\0') {
        ImGui::TextDisabled("%s", m_status);
    }

    ImGui::Separator();
    ImGui::TextDisabled("%zu registered. Edits take effect immediately - no relaunch.",
                        eng::CVarRegistry::Count());

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("cvars", 3, kFlags)) {
        return;
    }
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.40f);
    ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch, 0.15f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    eng::CVarRegistry::ForEach([this](eng::CVar& variable) {
        if (!Matches(variable.Name(), m_filter)) {
            return;
        }
        if (m_modifiedOnly && !variable.IsModified()) {
            return;
        }

        ImGui::TableNextRow();
        ImGui::PushID(&variable);

        ImGui::TableNextColumn();
        if (variable.IsModified()) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "* %s",
                               variable.Name().c_str());
        } else {
            ImGui::TextUnformatted(variable.Name().c_str());
        }
        Tooltip(variable);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);

        // TYPE-APPROPRIATE EDITING. The ## in every label is the ImGui id
        // trick from Panel.h: without it, two rows with the same widget label
        // would be the SAME widget, and editing one would move the other.
        switch (variable.Type()) {
            case eng::CVarType::Bool: {
                bool value = variable.GetBool();
                if (ImGui::Checkbox("##value", &value)) {
                    variable.SetBool(value);   // immediate. No apply step.
                }
                break;
            }
            case eng::CVarType::Int: {
                int value = variable.GetInt();
                if (ImGui::DragInt("##value", &value)) {
                    variable.SetInt(value);
                }
                break;
            }
            case eng::CVarType::Float: {
                float value = variable.GetFloat();
                if (ImGui::DragFloat("##value", &value, 0.01f)) {
                    variable.SetFloat(value);
                }
                break;
            }
            case eng::CVarType::String: {
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%s", variable.GetString().c_str());
                // Typing here is the input-capture test: the player must not
                // move while this field has focus.
                if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                    variable.SetString(buffer);
                }
                break;
            }
        }
        Tooltip(variable);

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", eng::ToString(variable.Type()));

        ImGui::PopID();
    });

    ImGui::EndTable();
}

} // namespace editor
