// WEEK 6 PANELS - viewport and debug draw. See ViewportPanel.h.

#include "panels/ViewportPanel.h"

#include "EditorApp.h"

#include <imgui.h>

namespace editor {

void ViewportPanel::Draw() {
    // THE GAME CAMERA - the one the Game view renders through and the one a
    // scene file's `camera` block sets. Not the Scene view's, which is the
    // editor's own and is driven by dragging in the view itself.
    //
    // Saying so matters: with two cameras on screen, a panel of unlabelled
    // position and zoom fields is a panel you cannot use, because you cannot
    // tell which of the two pictures it is going to move.
    eng::Camera& camera = eng::Engine::Get().GetCamera();

    ImGui::TextDisabled("The GAME camera - what the Game view shows and what "
                        "Save writes into the scene file.");
    ImGui::TextDisabled("The Scene view has its own; pan and zoom it in the view.");
    ImGui::Separator();

    float position[2] = {camera.Position().x, camera.Position().y};
    if (ImGui::DragFloat2("Position", position, 1.0f)) {
        camera.SetPosition(eng::Vec2{position[0], position[1]});
    }

    float zoom = camera.Zoom();
    if (ImGui::DragFloat("Zoom", &zoom, 0.01f, 0.05f, 20.0f, "%.3f")) {
        camera.SetZoom(zoom);
    }

    if (ImGui::Button("Reset View")) {
        camera.Reset();
    }

    ImGui::SeparatorText("ScreenToWorld round trip");

    // THE SCREENTOWORLD TEST. It used to read the raw WINDOW mouse position
    // and print the world point under the cursor, which was a genuinely useful
    // live check while the world filled the window - and became a lie the day
    // the world moved into a panel drawn at an offset, because the window
    // mouse position is no longer the view's mouse position.
    //
    // The cursor readout moved to the Scene view, where the number sits beside
    // the cursor that produced it. What is left here is the part that never
    // depended on the mouse: a fixed point pushed through the transform and
    // back, which still fails loudly if the inverse is wrong.
    const eng::Vec2 probe{100.0f, 60.0f};
    const eng::Vec2 world     = camera.ScreenToWorld(probe);
    const eng::Vec2 roundTrip = camera.WorldToScreen(world);

    ImGui::Text("screen %6.1f, %6.1f  ->  world %8.2f, %8.2f",
                static_cast<double>(probe.x), static_cast<double>(probe.y),
                static_cast<double>(world.x), static_cast<double>(world.y));
    ImGui::Text("world  %8.2f, %8.2f  ->  screen %6.1f, %6.1f",
                static_cast<double>(world.x), static_cast<double>(world.y),
                static_cast<double>(roundTrip.x), static_cast<double>(roundTrip.y));

    // Shown rather than asserted, because seeing it agree is more convincing
    // than being told it does.
    const eng::Vec2 error{roundTrip.x - probe.x, roundTrip.y - probe.y};
    const bool      agrees = eng::ApproxEqual(roundTrip, probe, 0.01f);
    ImGui::TextColored(agrees ? ImVec4(0.55f, 0.80f, 0.60f, 1.0f)
                              : ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                       "error %.4f, %.4f  %s", static_cast<double>(error.x),
                       static_cast<double>(error.y), agrees ? "" : "<- THE INVERSE IS WRONG");

    ImGui::SeparatorText("Visible bounds");
    const eng::AABB bounds = camera.VisibleBounds();
    ImGui::Text("min %.1f, %.1f   max %.1f, %.1f", static_cast<double>(bounds.min.x),
                static_cast<double>(bounds.min.y), static_cast<double>(bounds.max.x),
                static_cast<double>(bounds.max.y));
    ImGui::TextDisabled("viewport %.0f x %.0f px",
                        static_cast<double>(camera.ViewportSize().x),
                        static_cast<double>(camera.ViewportSize().y));
}

void DebugDrawPanel::Draw() {
    bool enabled = eng::DebugDraw::IsEnabled();
    if (ImGui::Checkbox("Debug draw enabled", &enabled)) {
        eng::DebugDraw::SetEnabled(enabled);
    }

    ImGui::SeparatorText("Categories");
    for (eng::u8 i = 0; i < static_cast<eng::u8>(eng::DebugCategory::Count); ++i) {
        const auto category = static_cast<eng::DebugCategory>(i);
        bool on = eng::DebugDraw::IsCategoryEnabled(category);
        if (ImGui::Checkbox(eng::ToString(category), &on)) {
            eng::DebugDraw::SetCategoryEnabled(category, on);
        }
    }

    ImGui::SeparatorText("Tessellation");
    // Wired through the CVar rather than straight into DebugDraw, so that the
    // value survives into the config file and so the CVar panel and this
    // slider cannot disagree.
    if (eng::CVar* segments = eng::CVarRegistry::Find("debug.circleSegments");
        segments != nullptr) {
        int value = segments->GetInt();
        if (ImGui::SliderInt("Circle segments", &value, 3, 96)) {
            segments->SetInt(value);
            eng::DebugDraw::SetCircleSegments(value);
        }
    }

    ImGui::SeparatorText("World helpers");
    if (eng::CVar* grid = eng::CVarRegistry::Find("debug.showGrid"); grid != nullptr) {
        bool value = grid->GetBool();
        if (ImGui::Checkbox("Grid and origin axes", &value)) {
            grid->SetBool(value);
        }
    }

    ImGui::SeparatorText("Queue");
    ImGui::Text("commands queued this frame : %zu", eng::DebugDraw::QueuedCommandCount());
    ImGui::Text("surviving from prior frames: %zu",
                eng::DebugDraw::PersistentCommandCount());
    ImGui::TextDisabled("A persistent count that climbs every frame means something is "
                        "enqueuing a lifetimed shape from an update.");

    if (ImGui::Button("Clear all debug geometry")) {
        eng::DebugDraw::Clear();
    }

    ImGui::SameLine();
    if (ImGui::Button("Drop a 3s marker at the origin")) {
        // Demonstrates the lifetime feature, which is the thing that makes
        // this a queue rather than a set of immediate draw calls.
        eng::DebugDraw::Circle(eng::Vec2{0.0f, 0.0f}, 40.0f, eng::Color::Magenta(), 3.0f);
        eng::DebugDraw::Text(eng::Vec2{12.0f, 12.0f}, "3s marker", eng::Color::Magenta(),
                             3.0f, eng::DebugSpace::World);
    }
}

} // namespace editor
