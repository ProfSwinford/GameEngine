// WEEK 10 PANEL - the toolbar. Play / Pause / Step driving the GameClock, a
// time-scale slider, the tick count and the frame time.
//
// PAUSE + STEP + INSPECTOR IS THE PAYOFF FOR THE WHOLE SEMESTER - see the note
// in InspectorPanel.h. Pause here, step one tick, and watch a single entity's
// position change by exactly one integration step in the Inspector while its
// collider is highlighted in the viewport.

#include "panels/InspectorPanel.h"

#include "EditorApp.h"

#include <imgui.h>

#include <string>

namespace editor {

void ToolbarPanel::Draw() {
    eng::Engine&    engine = eng::Engine::Get();
    eng::GameClock& clock  = engine.Clock();

    const bool playing = engine.IsInPlayMode();
    const bool paused  = clock.IsPaused();

    // ---- PLAY / STOP, Unity's semantics ----------------------------------
    //
    // Play SNAPSHOTS the scene and hands focus to the Game view. Stop RESTORES
    // the snapshot, so a play session that moved the player and destroyed half
    // the pickups leaves the authored scene untouched. That is what makes it
    // safe to press Play on something you have been editing for an hour, and
    // it is the single most important behaviour in this panel.
    ImGui::PushStyleColor(ImGuiCol_Button,
                          playing ? ImVec4(0.62f, 0.24f, 0.22f, 1.0f)
                                  : ImVec4(0.20f, 0.45f, 0.26f, 1.0f));
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(72, 0))) {
        if (playing) {
            engine.ExitPlayMode();
        } else {
            std::string error;
            if (engine.EnterPlayMode(error)) {
                // Focus follows Play, so the very next key press reaches the
                // game rather than the panel that happened to be focused.
                EditorState::Get().focusGameView = true;
            }
        }
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Pause only means anything while playing. Disabled rather than hidden, so
    // the toolbar does not change shape when Play is pressed.
    //
    // The label says "Resume" only when playing AND paused. In edit mode the
    // clock IS paused - that is what edit mode is - but a disabled button
    // reading "Resume" invites the question "resume what?", so it reads
    // "Pause" until there is something to resume.
    ImGui::BeginDisabled(!playing);
    if (ImGui::Button((playing && paused) ? "Resume" : "Pause", ImVec2(72, 0))) {
        clock.SetPaused(!paused);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!playing || !paused);
    if (ImGui::Button("Step", ImVec2(72, 0))) {
        // EXACTLY ONE TICK. Not approximately one - BeginFrame returns exactly
        // 1 and the accumulator is untouched, so ten clicks advance ten ticks.
        clock.RequestSingleStep();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    float scale = clock.TimeScale();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("Time scale", &scale, 0.0f, 4.0f, "%.2fx")) {
        clock.SetTimeScale(scale);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("1x")) {
        clock.SetTimeScale(1.0f);
    }

    ImGui::Separator();

    ImGui::Text("tick %llu   |   game %.2fs   |   real %.2fs   |   %d step(s) this frame",
                static_cast<unsigned long long>(clock.TickCount()), clock.GameSeconds(),
                clock.RealSeconds(), eng::Engine::Get().StepsThisFrame());
    ImGui::Text("fixed step %.4f s (%.1f Hz)   |   frame %.2f ms",
                static_cast<double>(clock.FixedStepSeconds()),
                static_cast<double>(1.0f / clock.FixedStepSeconds()),
                static_cast<double>(clock.RealDeltaSeconds() * 1000.0f));

    if (clock.ClampEventCount() > 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.30f, 1.0f),
                           "spiral-of-death clamp hit %llu time(s) - the simulation has "
                           "fallen behind real time",
                           static_cast<unsigned long long>(clock.ClampEventCount()));
    }

    ImGui::SeparatorText("Deferred operations");
    ImGui::Text("spawned %llu   destroyed %llu   pending %zu/%zu",
                static_cast<unsigned long long>(eng::DeferredOps::TotalSpawned()),
                static_cast<unsigned long long>(eng::DeferredOps::TotalDestroyed()),
                eng::DeferredOps::PendingSpawnCount(),
                eng::DeferredOps::PendingDestroyCount());
    ImGui::Text("messages dispatched %llu   queued %zu   subscriptions %zu",
                static_cast<unsigned long long>(eng::MessageBus::TotalDispatched()),
                eng::MessageBus::QueuedCount(), eng::MessageBus::SubscriptionCount());
}

} // namespace editor
