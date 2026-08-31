// WEEK 7 PANEL - allocator statistics. See MemoryPanel.h.

#include "panels/MemoryPanel.h"

#include <engine/memory/MemorySystem.h>
#include <engine/memory/PoolAllocator.h>
#include <engine/memory/StackAllocator.h>

#include <imgui.h>

#include <algorithm>

namespace editor {
namespace {

void HumanBytes(char* out, eng::usize size, eng::usize bytes) {
    if (bytes >= 1024u * 1024u) {
        std::snprintf(out, size, "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        std::snprintf(out, size, "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(out, size, "%zu B", bytes);
    }
}

} // namespace

void MemoryPanel::Draw() {
    char used[32];
    char capacity[32];
    char peak[32];

    HumanBytes(used, sizeof(used), eng::MemorySystem::TotalBytesUsed());
    HumanBytes(capacity, sizeof(capacity), eng::MemorySystem::TotalBytesCapacity());
    HumanBytes(peak, sizeof(peak), eng::MemorySystem::TotalPeakBytes());

    ImGui::Text("engine allocators: %s used of %s   (peak %s)", used, capacity, peak);
    ImGui::TextDisabled("%zu stack allocator(s), %zu pool(s)", eng::MemorySystem::StackCount(),
                        eng::MemorySystem::PoolCount());

    // THE PANEL'S OWN FOOTPRINT, shown rather than hidden. Fixed-size rings,
    // so it is a constant - which is the whole reason it is safe to state.
    ImGui::TextDisabled("this panel's own history buffers: %zu B (fixed, never grows)",
                        sizeof(m_history));

    ImGui::Separator();

    int plotIndex = 0;

    eng::MemorySystem::ForEachStack([&](eng::StackAllocator& allocator) {
        ImGui::PushID(&allocator);
        ImGui::SeparatorText(allocator.Name());

        const eng::usize inUse = allocator.BytesUsed();
        const eng::usize total = allocator.BytesCapacity();
        const eng::usize peakBytes = allocator.PeakBytes();

        char a[32], b[32], c[32];
        HumanBytes(a, sizeof(a), inUse);
        HumanBytes(b, sizeof(b), total);
        HumanBytes(c, sizeof(c), peakBytes);

        char overlay[80];
        std::snprintf(overlay, sizeof(overlay), "%s / %s", a, b);
        ImGui::ProgressBar(total > 0 ? static_cast<float>(inUse) / static_cast<float>(total)
                                     : 0.0f,
                           ImVec2(-1.0f, 0.0f), overlay);

        // PEAK, MARKED DISTINCTLY. It is the number that answers "how big does
        // this buffer actually need to be", which is the Phase 2 question, so
        // it gets a colour and its own line rather than being a column.
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "peak: %s (%.1f%% of capacity)",
                           c,
                           total > 0 ? 100.0 * static_cast<double>(peakBytes) /
                                           static_cast<double>(total)
                                     : 0.0);
        ImGui::Text("allocations: %zu", allocator.AllocationCount());

        if (plotIndex < kMaxTracked) {
            m_history[plotIndex][m_head] = static_cast<float>(inUse);
            char label[64];
            std::snprintf(label, sizeof(label), "bytes in use - look for the SHAPE");
            ImGui::PlotLines("##plot", m_history[plotIndex], kHistory, m_head, label, 0.0f,
                             std::max(1.0f, static_cast<float>(peakBytes) * 1.2f),
                             ImVec2(-1.0f, 55.0f));
            ++plotIndex;
        }
        ImGui::PopID();
    });

    eng::MemorySystem::ForEachPool([&](eng::PoolAllocator& allocator) {
        ImGui::PushID(&allocator);
        ImGui::SeparatorText(allocator.Name());

        const eng::usize inUse = allocator.BlocksInUse();
        const eng::usize total = allocator.BlockCount();

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%zu / %zu blocks", inUse, total);
        ImGui::ProgressBar(total > 0 ? static_cast<float>(inUse) / static_cast<float>(total)
                                     : 0.0f,
                           ImVec2(-1.0f, 0.0f), overlay);

        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "peak blocks: %zu",
                           allocator.PeakBlocksInUse());
        ImGui::Text("block size: %zu B (effective, after alignment rounding)",
                    allocator.BlockSize());

        if (plotIndex < kMaxTracked) {
            m_history[plotIndex][m_head] = static_cast<float>(allocator.BytesUsed());
            ImGui::PlotLines("##plot", m_history[plotIndex], kHistory, m_head,
                             "bytes in use", 0.0f,
                             std::max(1.0f, static_cast<float>(allocator.BytesCapacity())),
                             ImVec2(-1.0f, 55.0f));
            ++plotIndex;
        }
        ImGui::PopID();
    });

    m_head = (m_head + 1) % kHistory;

    ImGui::SeparatorText("Reading the plots");
    ImGui::TextWrapped("Sawtooth: allocated and freed each frame - healthy for the frame "
                       "stack, suspicious for anything else. Staircase up: a leak, one "
                       "step per event. Flat: what an update path should look like after "
                       "warm-up.");
}

} // namespace editor
