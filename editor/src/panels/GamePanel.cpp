// THE GAME VIEW. See GamePanel.h for the focus contract.

#include "panels/GamePanel.h"

#include "EditorApp.h"

#include <imgui.h>

namespace editor {

void GamePanel::Draw() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_target.Resize(static_cast<eng::i32>(avail.x), static_cast<eng::i32>(avail.y));

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(static_cast<float>(m_target.Width()),
                      static_cast<float>(m_target.Height()));

    if (!m_target.IsValid()) {
        ImGui::TextDisabled("no render target");
        return;
    }

    ImGui::Image(reinterpret_cast<ImTextureID>(m_target.NativeTexture()), size);

    // Clicking the view takes focus. That is how you get the keyboard back
    // after tuning a CVar mid-play without stopping the game.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::SetWindowFocus();
    }

    // FOCUS is what routes the keyboard, and it is the window's focus rather
    // than mere hover: hover would mean the player stops responding whenever
    // the mouse strays over the Hierarchy, which is maddening.
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    eng::Engine& engine = eng::Engine::Get();

    // A thin border while the game has the keyboard - the only reliable way to
    // answer "why is nothing responding" at a glance.
    if (m_focused && engine.IsInPlayMode()) {
        ImGui::GetWindowDrawList()->AddRect(
            origin, ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(90, 200, 110, 220), 0.0f, 0, 2.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 6.0f));
    if (!engine.IsInPlayMode()) {
        ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.78f, 0.85f),
                           "not playing - press Play on the Toolbar");
    } else if (!m_focused) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 0.95f),
                           "click here to give the game the keyboard");
    }
}

void GamePanel::RenderView() {
    if (!m_target.IsValid()) {
        return;
    }

    eng::Renderer::SetRenderTarget(&m_target);

    // NO debug draw, no grid, no axes. What ships.
    eng::Engine::Get().RenderWorld(eng::Engine::Get().GetCamera(),
                                   /*includeDebugDraw=*/false);

    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
