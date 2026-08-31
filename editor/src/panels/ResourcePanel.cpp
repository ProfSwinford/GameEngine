// WEEK 9 PANEL - the resource browser. Declared alongside HierarchyPanel
// because they were assigned together; in its own translation unit because
// they are two panels, and a file per panel is what keeps "add a panel" a
// one-line change.
//
// THIS PANEL IS THE MILESTONE 3 VERIFICATION, MADE LIVE - see HierarchyPanel.h.

#include "panels/HierarchyPanel.h"

#include "EditorApp.h"

#include <imgui.h>

namespace editor {

void ResourcePanel::Draw() {
    eng::ResourceManager::Snapshot(m_entries);

    const eng::u64 totalRefs = eng::ResourceManager::TotalRefCount();

    // TOTAL REFCOUNT, PROMINENTLY. This is the number the milestone is checked
    // against, live, in front of an instructor. Green at zero, amber otherwise,
    // so "did it reach zero" is answerable from across the room.
    ImGui::TextColored(totalRefs == 0 ? ImVec4(0.45f, 0.90f, 0.50f, 1.0f)
                                      : ImVec4(0.95f, 0.85f, 0.40f, 1.0f),
                       "TOTAL REFCOUNT: %llu", static_cast<unsigned long long>(totalRefs));
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu resident | %.1f KiB", eng::ResourceManager::LoadedCount(),
                        static_cast<double>(eng::ResourceManager::BytesResident()) / 1024.0);
    ImGui::TextDisabled("File > Unload Scene, and watch this reach zero. When it does not, "
                        "the stuck resource is named in the table below - which is a "
                        "better answer than a printed number.");

    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("resources", 5, kFlags)) {
        return;
    }
    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("virtual path");
    ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("size");
    ImGui::TableSetupColumn("state");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    for (const eng::ResourceManager::Entry& entry : m_entries) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        // ImGui::Image takes the native texture id directly with the
        // SDL_Renderer backend, so previews are nearly free - and startlingly
        // satisfying the first time they appear.
        if (eng::Texture* texture = eng::ResourceManager::Get(entry.handle);
            texture != nullptr && texture->native != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture->native), ImVec2(32, 32));
        } else {
            ImGui::TextDisabled("-");
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(entry.path.c_str());

        ImGui::TableNextColumn();
        // The provided scene has 18 entities sharing one texture. A refcount
        // that is not 18 on checker_red.bmp says the path lookup is not
        // finding the existing entry and the texture has been loaded twice.
        ImGui::Text("%u", entry.refCount);

        ImGui::TableNextColumn();
        ImGui::Text("%dx%d, %.1f KiB", entry.width, entry.height,
                    static_cast<double>(entry.bytes) / 1024.0);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(eng::ToString(entry.state));
    }

    ImGui::EndTable();
}

} // namespace editor
