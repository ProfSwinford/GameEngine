// ============================================================================
//  ConsolePanel.cpp - the log window. See ConsolePanel.h.
// ============================================================================

#include "panels/ConsolePanel.h"

#include <engine/core/Log.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace editor {
namespace {

// A colour per level, so the eye finds the errors without reading anything.
// ImVec4 is ImGui's colour type: red, green, blue and opacity, each 0 to 1.
ImVec4 ColorFor(eng::LogLevel level) {
    switch (level) {
        case eng::LogLevel::Info:    return ImVec4(0.88f, 0.88f, 0.90f, 1.0f);
        case eng::LogLevel::Warning: return ImVec4(0.95f, 0.78f, 0.30f, 1.0f);
        case eng::LogLevel::Error:   return ImVec4(0.95f, 0.38f, 0.32f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

// Case-insensitive "does this text contain that text?", for the search box.
//
// std::search is the standard-library substring search. The lambda at the end
// tells it how to compare two characters - here, by lower-casing both first,
// which is what makes the search ignore capitals.
bool ContainsIgnoringCase(const std::string& haystack, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;   // an empty search box matches everything
    }
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle, needle + std::strlen(needle),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

} // namespace

void ConsolePanel::Draw() {
    // ---- the filter bar along the top ------------------------------------
    static const char* kLevelNames[] = {"Info", "Warning", "Error"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("Show", &m_minLevel, kLevelNames, IM_ARRAYSIZE(kLevelNames));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##search", "search...", m_search, sizeof(m_search));

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        eng::LogBuffer::Clear();
    }

    // The channel list is asked for fresh every frame, so a channel invented
    // by game code appears here as soon as it logs anything.
    eng::LogBuffer::Channels(m_channels);

    if (ImGui::TreeNodeEx("Channels", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        int column = 0;
        for (const std::string& channel : m_channels) {
            // A channel seen for the first time starts ticked.
            if (!m_channelEnabled.contains(channel)) {
                m_channelEnabled[channel] = true;
            }
            // Four checkboxes per row. SameLine() puts the next widget beside
            // the previous one instead of below it.
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

    // ---- the messages -----------------------------------------------------
    eng::LogBuffer::Snapshot(m_snapshot);

    // BeginChild makes a scrollable region inside the panel. Everything drawn
    // until EndChild goes inside it.
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
            if (!ContainsIgnoringCase(record.message, m_search) &&
                !ContainsIgnoringCase(record.channel, m_search)) {
                continue;
            }

            ImGui::TextColored(ColorFor(record.level), "[%8.3f] [%-7s] [%-11s] %s",
                               record.timeSeconds, eng::ToString(record.level),
                               record.channel.c_str(), record.message.c_str());
        }

        // Follow the newest message, but ONLY when the view is already at the
        // bottom. Scrolling up to read something and having it yanked away by
        // the next log line is the difference between an auto-scroll that
        // helps and one that is infuriating.
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace editor
