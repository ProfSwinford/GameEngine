// WEEK 3 PANEL - the log console. See LogPanel.h.

#include "panels/LogPanel.h"

#include <engine/core/Log.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace editor {
namespace {

ImVec4 ColorFor(eng::LogLevel level) {
    switch (level) {
        case eng::LogLevel::Trace:   return ImVec4(0.55f, 0.55f, 0.58f, 1.0f);
        case eng::LogLevel::Debug:   return ImVec4(0.45f, 0.80f, 0.85f, 1.0f);
        case eng::LogLevel::Info:    return ImVec4(0.88f, 0.88f, 0.90f, 1.0f);
        case eng::LogLevel::Warning: return ImVec4(0.95f, 0.78f, 0.30f, 1.0f);
        case eng::LogLevel::Error:   return ImVec4(0.95f, 0.38f, 0.32f, 1.0f);
        case eng::LogLevel::Fatal:   return ImVec4(1.00f, 0.25f, 0.55f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle,
                                needle + std::strlen(needle),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

} // namespace

void LogPanel::Draw() {
    // --- filter bar --------------------------------------------------------
    static const char* kLevelNames[] = {"Trace", "Debug", "Info", "Warning", "Error", "Fatal"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("Min level", &m_minLevel, kLevelNames, IM_ARRAYSIZE(kLevelNames));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##search", "search...", m_search, sizeof(m_search));

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        eng::LogBuffer::Clear();
    }

    // Channels DISCOVERED AT RUNTIME. Nothing here knows what a channel is
    // called, which is why panels written in Week 3 still show the channels
    // Weeks 9 and 10 added.
    eng::LogBuffer::Channels(m_channels);
    if (ImGui::BeginPopupContextItem("channelfilter")) {
        ImGui::EndPopup();
    }

    if (ImGui::TreeNodeEx("Channels", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        int column = 0;
        for (const std::string& channel : m_channels) {
            const auto it = m_channelEnabled.find(channel);
            if (it == m_channelEnabled.end()) {
                m_channelEnabled[channel] = true;   // a new channel starts visible
            }
            if (column++ % 4 != 0) {
                ImGui::SameLine();
            }
            ImGui::Checkbox(channel.c_str(), &m_channelEnabled[channel]);
        }
        if (ImGui::SmallButton("All")) {
            for (auto& [name, enabled] : m_channelEnabled) { enabled = true; }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) {
            for (auto& [name, enabled] : m_channelEnabled) { enabled = false; }
        }
        ImGui::TreePop();
    }

    ImGui::Separator();

    // --- the list ----------------------------------------------------------
    eng::LogBuffer::Snapshot(m_snapshot);   // copied under the lock, drawn without it

    if (ImGui::BeginChild("scroll", ImVec2(0, 0), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const eng::LogRecord& record : m_snapshot) {
            if (static_cast<int>(record.level) < m_minLevel) {
                continue;
            }
            const auto it = m_channelEnabled.find(record.channel);
            if (it != m_channelEnabled.end() && !it->second) {
                continue;
            }
            if (!ContainsCaseInsensitive(record.message, m_search) &&
                !ContainsCaseInsensitive(record.channel, m_search)) {
                continue;
            }

            ImGui::TextColored(ColorFor(record.level), "[%8.3f] [%-7s] [%-11s] %s",
                               record.timeSeconds, eng::ToString(record.level),
                               record.channel.c_str(), record.message.c_str());
        }

        // Only follows the tail when the view is ALREADY at the bottom, so
        // scrolling up to read something does not get yanked away by the next
        // log line - which is the difference between an auto-scroll that helps
        // and one that is infuriating.
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace editor
