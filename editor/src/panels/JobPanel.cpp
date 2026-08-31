// WEEK 5 PANEL - jobs. See JobPanel.h for the race-condition discussion.

#include "panels/JobPanel.h"

#include <engine/concurrency/JobSystem.h>
#include <engine/fs/FileSystem.h>

#include <imgui.h>

namespace editor {

void JobPanel::Draw() {
    const eng::u32 workers = eng::JobSystem::WorkerCount();

    ImGui::Text("workers: %u %s", workers,
                eng::JobSystem::IsRunning() ? "(running)" : "(stopped)");

    // Per-thread state. Each IsWorkerBusy is an atomic load; nothing here
    // takes a lock a worker needs.
    for (eng::u32 i = 0; i < workers; ++i) {
        const bool busy = eng::JobSystem::IsWorkerBusy(i);
        if (i % 4 != 0) {
            ImGui::SameLine();
        }
        ImGui::TextColored(busy ? ImVec4(0.95f, 0.75f, 0.30f, 1.0f)
                                : ImVec4(0.45f, 0.55f, 0.45f, 1.0f),
                           "[%u:%s]", i, busy ? "work" : "idle");
    }

    ImGui::SeparatorText("Queue");

    // SizeApprox, through JobSystem::QueueDepth. DISPLAYED, never branched on.
    const eng::usize depth = eng::JobSystem::QueueDepth();
    m_depthHistory[m_head] = static_cast<float>(depth);
    m_head = (m_head + 1) % kHistory;

    ImGui::PlotLines("##depth", m_depthHistory, kHistory, m_head, "queue depth", 0.0f,
                     16.0f, ImVec2(-1.0f, 60.0f));

    ImGui::Text("depth now: %zu   (approximate - display only)", depth);
    ImGui::Text("pushed: %llu    completed: %llu    in flight: %llu",
                static_cast<unsigned long long>(eng::JobSystem::JobsPushed()),
                static_cast<unsigned long long>(eng::JobSystem::JobsCompleted()),
                static_cast<unsigned long long>(eng::JobSystem::JobsPushed() -
                                                eng::JobSystem::JobsCompleted()));
    ImGui::Text("async file reads pending: %zu", eng::FileSystem::PendingReadCount());

    ImGui::SeparatorText("Platform costs (measured once at startup)");
    const eng::JobSystem::PlatformCosts& costs = eng::JobSystem::GetPlatformCosts();
    if (!costs.measured) {
        ImGui::TextDisabled("not measured in this process.");
        ImGui::TextWrapped("Run  sandbox --os-measure  to produce them; the numbers and "
                           "their implications are written up in "
                           "docs/week05-os-measurements.md.");
    } else {
        ImGui::Text("thread create+join : %8.2f us", costs.threadCreateJoinMicros);
        ImGui::Text("context switch     : %8.2f us", costs.contextSwitchMicros);
        ImGui::Text("first-touch page   : %8.2f us", costs.firstTouchPerPageMicros);
    }
}

} // namespace editor
