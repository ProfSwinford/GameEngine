// ============================================================================
//  ToolbarPanel.cpp - Play, Pause and Step. Declared in InspectorPanel.h.
//
//  This is the strip across the top of the editor, and it is the same set of
//  buttons Unity puts there.
// ============================================================================

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

    // ---- PLAY / STOP ------------------------------------------------------
    //
    // Play takes a SNAPSHOT of the scene and hands the keyboard to the Game
    // view. Stop puts the snapshot back, so a play session that moved the
    // player and collected half the pickups leaves the level exactly as you
    // authored it.
    //
    // That is what makes it safe to press Play on something you have been
    // editing for an hour, and it is the most important behaviour here.
    ImGui::PushStyleColor(ImGuiCol_Button,
                          playing ? ImVec4(0.62f, 0.24f, 0.22f, 1.0f)    // red for Stop
                                  : ImVec4(0.20f, 0.45f, 0.26f, 1.0f));  // green for Play
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(72, 0))) {
        if (playing) {
            engine.ExitPlayMode();
        } else {
            std::string error;
            if (engine.EnterPlayMode(error)) {
                // Focus follows Play, so the very next key press reaches the
                // game rather than whichever panel happened to be focused.
                EditorState::Get().focusGameView = true;
            }
        }
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Pause only means anything while playing. Greyed out rather than hidden,
    // so the toolbar does not change shape when Play is pressed and the
    // buttons stay where your hand expects them.
    //
    // The label reads "Resume" only when playing AND paused. In edit mode the
    // clock IS paused - that is what edit mode is - but a greyed-out button
    // saying "Resume" invites the question "resume what?".
    ImGui::BeginDisabled(!playing);
    if (ImGui::Button((playing && paused) ? "Resume" : "Pause", ImVec2(72, 0))) {
        clock.SetPaused(!paused);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!playing || !paused);
    if (ImGui::Button("Step", ImVec2(72, 0))) {
        // Exactly ONE simulation step. Not roughly one - ten clicks advance
        // exactly ten steps. Pair this with the Inspector to watch a single
        // value change one tick at a time.
        clock.RequestSingleStep();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    float scale = clock.TimeScale();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("Speed", &scale, 0.0f, 4.0f, "%.2fx")) {
        clock.SetTimeScale(scale);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("1x")) {
        clock.SetTimeScale(1.0f);
    }

    ImGui::Separator();

    ImGui::Text("step %llu   |   game time %.2fs   |   real time %.2fs   |   %d step(s) "
                "this frame",
                clock.TickCount(), clock.GameSeconds(), clock.RealSeconds(),
                engine.StepsThisFrame());
    ImGui::Text("simulating %.1f times a second   |   this frame took %.2f ms",
                static_cast<double>(1.0f / clock.FixedStepSeconds()),
                static_cast<double>(clock.RealDeltaSeconds() * 1000.0f));

    ImGui::SameLine();
    ImGui::TextDisabled("|   %zu entities, %zu colliders touching",
                        engine.GetScene().EntityCount(),
                        eng::CollisionSystem::ActivePairCount());
}

} // namespace editor
