// ============================================================================
//  GamePanel.cpp - the Game view. See GamePanel.h for how focus works.
// ============================================================================

#include "panels/GamePanel.h"

#include "EditorApp.h"

#include <imgui.h>

namespace editor {

void GamePanel::Draw() {
    // ---- the game camera, editable ---------------------------------------
    //
    // This is the camera a player sees through, and it is what gets written
    // into the scene file when you save. It is edited here rather than in the
    // Scene view because this is the panel showing what it points at.
    eng::Camera& camera = eng::Engine::Get().GetCamera();

    float position[2] = {camera.Position().x, camera.Position().y};
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::DragFloat2("Camera", position, 1.0f)) {
        camera.SetPosition(eng::Vec2{position[0], position[1]});
        EditorState::Get().dirty = true;
    }

    ImGui::SameLine();
    float zoom = camera.Zoom();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("Zoom", &zoom, 0.01f, 0.05f, 20.0f, "%.2fx")) {
        camera.SetZoom(zoom);
        EditorState::Get().dirty = true;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        camera.Reset();
        EditorState::Get().dirty = true;
    }

    // How much room is left inside this panel, in pixels. The off-screen
    // picture is resized to match, so the view fills the panel whatever size
    // the user drags it to.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_target.Resize(static_cast<int>(avail.x), static_cast<int>(avail.y));

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(static_cast<float>(m_target.Width()),
                      static_cast<float>(m_target.Height()));

    if (!m_target.IsValid()) {
        ImGui::TextDisabled("this view could not be created");
        return;
    }

    // ImGui::Image displays a picture. The picture itself is filled in later
    // this frame by RenderView.
    ImGui::Image(reinterpret_cast<ImTextureID>(m_target.NativeTexture()), size);

    // Clicking the view takes focus. That is how you get the keyboard back
    // after clicking on something else mid-play.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::SetWindowFocus();
    }

    // FOCUS is what routes the keyboard, and it is deliberately focus rather
    // than merely hovering: hovering would mean the player stopped responding
    // every time the mouse strayed over the Hierarchy.
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    eng::Engine& engine = eng::Engine::Get();

    // A green border while the game has the keyboard. It is the only reliable
    // way to answer "why is nothing responding?" at a glance.
    if (m_focused && engine.IsInPlayMode()) {
        ImGui::GetWindowDrawList()->AddRect(
            origin, ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(90, 200, 110, 220), 0.0f, 0, 2.0f);
    }

    // A short message over the top-left corner saying what to do next.
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

    // Send drawing into this panel's own picture instead of onto the window.
    eng::Renderer::SetRenderTarget(&m_target);

    // Through the GAME camera, and with gizmos switched OFF - no grid, no
    // origin arrows, no collider outlines. What ships.
    eng::Engine::Get().RenderWorld(eng::Engine::Get().GetCamera(),
                                   /*includeGizmos=*/false);

    // Back to the window, or the editor's own interface would be drawn inside
    // this panel's picture.
    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
