// WEEK 4 PANEL - the profiler. See ProfilerPanel.h.

#include "panels/ProfilerPanel.h"

#include <engine/physics/Collider.h>
#include <engine/scene/SystemOrder.h>

#include <imgui.h>

#include <algorithm>

namespace editor {

void ProfilerPanel::Draw() {
    // --- the plot ----------------------------------------------------------
    const eng::usize frameCount = eng::TimerRegistry::FrameHistoryCount();
    if (frameCount > 0) {
        const float* history = eng::TimerRegistry::FrameHistory();

        float worst = 0.0f;
        for (eng::usize i = 0; i < frameCount; ++i) {
            worst = std::max(worst, history[i]);
        }
        // The scale floor is 20 ms so that a healthy 16.6 ms frame is not
        // drawn as a full-height mountain range. It grows for genuine spikes.
        const float scale = std::max(worst * 1.1f, 20.0f);

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "frame ms (last %zu, worst %.1f)",
                      frameCount, static_cast<double>(worst));
        ImGui::PlotLines("##frametime", history, static_cast<int>(frameCount), 0, overlay,
                         0.0f, scale, ImVec2(-1.0f, 70.0f));
    }

    if (ImGui::Button("Reset statistics")) {
        eng::TimerRegistry::Reset();
    }
    ImGui::SameLine();
    ImGui::Text("%zu timed site(s)", eng::TimerRegistry::SiteCount());

    // Week 10: collision as its own line item, called out above the table
    // because it is the number that decides whether Phase 2 needs a broad
    // phase - and the answer should come from a measurement, not intuition.
    ImGui::SeparatorText("Collision");
    const eng::TimerStats collision = eng::TimerRegistry::Get("CollisionSystem");
    ImGui::Text("colliders %zu | pair tests last frame %llu | active pairs %zu",
                eng::CollisionSystem::ColliderCount(),
                static_cast<unsigned long long>(eng::CollisionSystem::PairTestsLastFrame()),
                eng::CollisionSystem::ActivePairCount());
    ImGui::Text("collision cost: avg %.4f ms, max %.4f ms", collision.AverageMs(),
                collision.maxMs);

    // --- the table ---------------------------------------------------------
    ImGui::SeparatorText("All measurement sites");

    m_rows.clear();
    eng::TimerRegistry::ForEach([this](const char* name, const eng::TimerStats& stats) {
        m_rows.push_back(Row{name, stats});
    });

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("timers", 5, kFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("site", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("min ms");
        ImGui::TableSetupColumn("avg ms");
        ImGui::TableSetupColumn("max ms");
        ImGui::TableSetupColumn("samples");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
            specs != nullptr && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
            std::stable_sort(m_rows.begin(), m_rows.end(),
                             [&spec](const Row& a, const Row& b) {
                                 bool less = false;
                                 switch (spec.ColumnIndex) {
                                     case 0: less = a.name < b.name; break;
                                     case 1: less = a.stats.minMs < b.stats.minMs; break;
                                     case 2: less = a.stats.AverageMs() < b.stats.AverageMs(); break;
                                     case 3: less = a.stats.maxMs < b.stats.maxMs; break;
                                     default: less = a.stats.samples < b.stats.samples; break;
                                 }
                                 return spec.SortDirection == ImGuiSortDirection_Ascending
                                            ? less
                                            : !less;
                             });
        }

        for (const Row& row : m_rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", row.stats.minMs);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", row.stats.AverageMs());
            ImGui::TableNextColumn();
            // The max is coloured, because the max is the stutter and the
            // stutter is what the plot above is for.
            ImGui::TextColored(row.stats.maxMs > 8.0 ? ImVec4(0.95f, 0.55f, 0.30f, 1.0f)
                                                     : ImVec4(1, 1, 1, 1),
                               "%.4f", row.stats.maxMs);
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.stats.samples));
        }
        ImGui::EndTable();
    }
}

} // namespace editor
