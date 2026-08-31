// WEEK 2 PANEL. Note what is NOT included here: any SDL header. That absence
// is the point of the panel.

#include "panels/EventInspectorPanel.h"

#include <imgui.h>

#include <algorithm>

namespace editor {
namespace {

const char* KindName(eng::RawEventKind kind) {
    return eng::ToString(kind);
}

} // namespace

EventInspectorPanel::EventInspectorPanel(const eng::EventPump& pump) : m_pump(&pump) {}

void EventInspectorPanel::Draw() {
    if (m_pump == nullptr) {
        ImGui::TextUnformatted("no event pump");
        return;
    }

    ImGui::Checkbox("Pause capture", &m_paused);
    ImGui::SameLine();
    ImGui::TextDisabled("(freezes the display, not the pump)");

    if (!m_paused) {
        m_frozenCount = std::min(m_pump->Count(), static_cast<eng::usize>(32));
        for (eng::usize i = 0; i < m_frozenCount; ++i) {
            m_frozen[i] = m_pump->At(i);
        }
    }

    ImGui::Separator();
    ImGui::Text("this frame: %zu event(s)", m_pump->Count());
    ImGui::Text("mouse: %.0f, %.0f (window pixels)",
                static_cast<double>(m_pump->MouseX()),
                static_cast<double>(m_pump->MouseY()));

    ImGui::SeparatorText("Running totals");
    if (ImGui::BeginTable("totals", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("kind");
        ImGui::TableSetupColumn("count");
        ImGui::TableHeadersRow();

        for (eng::usize i = 1; i < kKindCount; ++i) {   // skip None
            const auto kind = static_cast<eng::RawEventKind>(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KindName(kind));
            ImGui::TableNextColumn();
            ImGui::Text("%llu",
                        static_cast<unsigned long long>(m_pump->TotalOfKind(kind)));
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset totals")) {
        // const_cast, and it is worth being honest about why: the panel holds
        // a const reference because it should not be able to Poll(), and this
        // is the one mutation it legitimately performs. The alternative is a
        // non-const reference that permits everything, which is worse.
        const_cast<eng::EventPump*>(m_pump)->ResetTotals();
    }

    ImGui::SeparatorText("Events this frame");
    if (ImGui::BeginTable("events", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 200.0f))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("kind");
        ImGui::TableSetupColumn("code / key");
        ImGui::TableSetupColumn("mouse");
        ImGui::TableHeadersRow();

        for (eng::usize i = 0; i < m_frozenCount; ++i) {
            const eng::RawEvent& event = m_frozen[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%zu", i);

            ImGui::TableNextColumn();
            const bool isKey = event.kind == eng::RawEventKind::KeyDown ||
                               event.kind == eng::RawEventKind::KeyUp;
            ImGui::TextColored(isKey ? ImVec4(0.6f, 0.9f, 1.0f, 1.0f)
                                     : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                               "%s", KindName(event.kind));

            ImGui::TableNextColumn();
            if (isKey) {
                // The key NAME, from the engine. The panel does not know what
                // a scancode is and must not learn.
                ImGui::Text("%d (%s)", event.code, eng::EventPump::KeyName(event.code));
            } else {
                ImGui::Text("%d", event.code);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.0f, %.0f", static_cast<double>(event.mouseX),
                        static_cast<double>(event.mouseY));
        }
        ImGui::EndTable();
    }
}

} // namespace editor
