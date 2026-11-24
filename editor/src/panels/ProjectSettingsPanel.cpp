// ============================================================================
//  ProjectSettingsPanel.cpp - see the header for why it is a panel, why every
//  change is live, and why there is a draft at all.
// ============================================================================

#include "ProjectSettingsPanel.h"

#include "../ProjectSettings.h"
#include "FieldRow.h"

#include <engine/Engine.h>
#include <engine/core/Log.h>
#include <engine/render/Presentation.h>

#include <imgui.h>

#include <array>
#include <cstring>
#include <string>

namespace editor {
namespace {

// The repo's error red - ConsolePanel paints LogLevel::Error with it, so a
// refusal here looks like a refusal everywhere else.
constexpr ImVec4 kErrorRed{0.95f, 0.38f, 0.32f, 1.0f};

// A small grey line under a row, for the things a number cannot say: that a
// setting is the GAME's and does nothing to the editor, or that it needs a
// restart, or what it will cost.
void Aside(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// ============================================================================
//  THE TIP HANGS ON THE ROW, NOT ON A "(?)" BESIDE IT.
//
//  A row:: widget is given SetNextItemWidth(-FLT_MIN), so it ends flush with
//  the window's work rect. Anything SameLine'd after it therefore starts at
//  the clip rect's right edge and is thrown away by ItemAdd - which is how
//  every one of these tooltips came to be unreachable while looking perfectly
//  reasonable in the source.
//
//  FieldRow.h's Row wraps the whole row in a group, so IsItemHovered() after
//  the call covers the name AND the value. Hovering anywhere on the row is a
//  better target than a six-pixel question mark anyway.
// ============================================================================
void Tip(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

const char* const kLogLevels[] = {"Info", "Warning", "Error"};

int IndexOfLevel(eng::LogLevel level) {
    switch (level) {
        case eng::LogLevel::Warning: return 1;
        case eng::LogLevel::Error:   return 2;
        default:                     return 0;
    }
}

} // namespace

void ProjectSettingsPanel::Took(bool ok, const std::string& error) {
    m_error = ok ? std::string() : error;
}

// ---------------------------------------------------------------------------
//  The row helpers
//
//  Each one does the same three things, which is exactly why they are
//  functions: draw against the draft, write the file when the gesture ENDS,
//  and put the engine's own value back into the draft the moment the row is
//  not being touched. Miss the third and a refused write leaves the widget
//  showing a number that is in no file anywhere.
// ---------------------------------------------------------------------------

void ProjectSettingsPanel::FloatRow(const char* label, const char* tip, float live,
                                    float& draft, float speed, float min, float max,
                                    const char* section, const char* key) {
    row::DragFloat(label, &draft, speed, min, max, "%.4g");
    Tip(tip);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string error;
        Took(SetNumber(section, key, draft, error), error);
    } else if (!ImGui::IsItemActive()) {
        draft = live;
    }
}

void ProjectSettingsPanel::IntRow(const char* label, const char* tip, int live,
                                  int& draft, float speed, int min, int max,
                                  const char* section, const char* key) {
    row::DragInt(label, &draft, speed, min, max);
    Tip(tip);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string error;
        Took(SetInt(section, key, draft, error), error);
    } else if (!ImGui::IsItemActive()) {
        draft = live;
    }
}

void ProjectSettingsPanel::BoolRow(const char* label, const char* tip, bool live,
                                   const char* section, const char* key) {
    // No draft: a checkbox has no gesture to be half-way through, so the
    // engine's value is always the right thing to show.
    bool value = live;
    const bool clicked = row::Checkbox(label, &value);
    Tip(tip);
    if (clicked) {
        std::string error;
        Took(SetBool(section, key, value, error), error);
    }
}

void ProjectSettingsPanel::TextRow(const char* label, const char* tip,
                                   const std::string& live, std::string& draft,
                                   const char* section, const char* key,
                                   Checked checked) {
    std::array<char, 512> buffer{};
#ifdef _MSC_VER
    strncpy_s(buffer.data(), buffer.size(), draft.c_str(), _TRUNCATE);
#else
    std::strncpy(buffer.data(), draft.c_str(), buffer.size() - 1);
#endif

    // ENTER COMMITS, and nothing else does. A path or a title written one
    // keystroke at a time would be a file rewrite per character, and would
    // save a dozen half-typed values on the way to the real one.
    const bool entered = row::InputText(label, buffer.data(), buffer.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    Tip(tip);
    if (entered) {
        draft = buffer.data();
        std::string error;

        // ==================================================================
        //  ONE KEY IS CHECKED BY THE ENGINE'S OWN PARSER FIRST.
        //
        //  presentation.mode has a grammar - "free", "16:9", "1920x1080" -
        //  and SetString would happily write "banana", which nothing
        //  downstream refuses: the engine keeps the text as it finds it and
        //  only warns when it comes to use it. The game would then silently
        //  fill the window, which is the exact bug this panel's file was
        //  written to close.
        // ==================================================================
        const bool ok = (checked == Checked::PresentationMode)
                            ? SetPresentationMode(draft, error)
                            : SetString(section, key, draft, error);
        Took(ok, error);
    } else if (ImGui::IsItemActive()) {
        draft = buffer.data();
    } else {
        // Not being typed in, so the engine is the authority - which is also
        // what puts a refused value back to what the file actually says.
        draft = live;
    }
}

// ---------------------------------------------------------------------------

void ProjectSettingsPanel::Draw() {
    const eng::BootConfig& live = eng::Engine::Get().Config();

    // ======================================================================
    //  SEEDED BEFORE THE FIRST ROW IS EVER DRAWN.
    //
    //  Each row copies the engine's value into its draft AFTER drawing, once
    //  it knows the row is not being touched - which is right for every frame
    //  but the first. On the first frame there is nothing to copy from yet,
    //  so the text fields drew empty and the numbers drew BootConfig's
    //  built-in defaults: a panel that opened showing 1280x720 and "free"
    //  whatever the project actually said.
    // ======================================================================
    if (!m_seeded) {
        m_draft             = live;
        m_draftPresentation = live.presentation;
        m_seeded            = true;
    }

    // ---- the refusal, if there was one -----------------------------------
    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorRed);
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Dismiss")) {
            m_error.clear();
        }
        ImGui::Separator();
    }

    Aside("These are the PROJECT's settings - they live in config/engine.json, "
          "they are the same for everybody working on the project, and the export "
          "packs the file into the built game. Changes take effect at once; the "
          "ones that cannot are marked.");
    ImGui::Spacing();

    // ======================================================================
    //  PRESENTATION
    // ======================================================================
    if (ImGui::CollapsingHeader("Presentation", ImGuiTreeNodeFlags_DefaultOpen)) {
        TextRow("Mode", "\"free\" fills the window - a wider monitor then sees more "
                        "of the level. A ratio like 16:9 pins the shape; a size like "
                        "1920x1080 pins the exact pixels. Whatever is left over is "
                        "black.\n\nPress Enter to apply.",
                live.presentation, m_draftPresentation, "presentation", "mode",
                Checked::PresentationMode);

        Aside("The Game view's dropdown writes this same setting, and shows it.");
        ImGui::Spacing();
    }

    // ======================================================================
    //  UNITS
    // ======================================================================
    if (ImGui::CollapsingHeader("Units", ImGuiTreeNodeFlags_DefaultOpen)) {
        FloatRow("Pixels per unit",
                 "How big ONE of something is: one tile, one standing character's "
                 "width, one grid square.\n\nCollider sizes are written as fractions "
                 "of this, so every collider in the open scene changes size when it "
                 "changes - which is the point of it, and worth knowing before you "
                 "drag.",
                 live.worldUnitPixels, m_draft.worldUnitPixels, 1.0f, 1.0f, 1024.0f,
                 "units", "pixelsPerUnit");
        ImGui::Spacing();
    }

    // ======================================================================
    //  PHYSICS
    // ======================================================================
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Written as one key, so both numbers go in together - a gravity with
        // a new y and an old x would be a frame of nonsense in the file.
        float gravity[2] = {m_draft.gravity.x, m_draft.gravity.y};
        row::DragFloat2("Gravity", gravity, 1.0f, -100000.0f, 100000.0f, "%.4g");
        Tip("In YOUR GAME'S units, and this engine works in pixels. Unity's -9.81 "
            "is metres per second squared; at roughly 100 pixels to the metre, "
            "-981 is the same real-world gravity. Use 0, 0 for a top-down game.");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            std::string error;
            Took(SetNumbers("physics", "gravity", gravity, 2, error), error);
            m_draft.gravity = eng::Vec2{gravity[0], gravity[1]};
        } else if (ImGui::IsItemActive()) {
            m_draft.gravity = eng::Vec2{gravity[0], gravity[1]};
        } else {
            m_draft.gravity = live.gravity;
        }
        IntRow("Solver iterations",
               "How many passes the contact solver makes over the collisions it "
               "found. Raise it if stacked objects sink into each other.",
               live.solverIterations, m_draft.solverIterations, 0.1f, 1, 64,
               "physics", "solverIterations");
        ImGui::Spacing();
    }

    // ======================================================================
    //  TUNABLES
    // ======================================================================
    if (ImGui::CollapsingHeader("Tunables")) {
        FloatRow("Fixed timestep",
                 "How much game time one simulation step represents. 0.0166667 is a "
                 "sixtieth of a second, so sixty steps a second.\n\nThis is NOT the "
                 "frame rate - drawing happens as often as it can, separately. Try "
                 "0.1 and watch movement go chunky while the drawing stays smooth.",
                 live.fixedTimestepSeconds, m_draft.fixedTimestepSeconds, 0.001f,
                 0.001f, 0.5f, "tunables", "fixedTimestepSeconds");

        IntRow("Max steps per frame",
               "The most simulation steps one frame may run, so a slow frame cannot "
               "ask for more and more steps until the game locks up.",
               live.maxStepsPerFrame, m_draft.maxStepsPerFrame, 0.1f, 1, 60,
               "tunables", "maxStepsPerFrame");

        IntRow("Gizmo circle segments",
               "How many short straight lines a drawn circle is made of. More looks "
               "rounder.",
               live.gizmoCircleSegments, m_draft.gizmoCircleSegments, 0.2f, 3, 256,
               "tunables", "gizmoCircleSegments");

        IntRow("Console buffer",
               "How many log messages the Console remembers.",
               live.logBufferCapacity, m_draft.logBufferCapacity, 16.0f, 16, 1000000,
               "tunables", "logBufferCapacity");
        Aside("Changing this EMPTIES the Console - the messages it is holding are "
              "in the buffer being replaced.");
        ImGui::Spacing();
    }

    // ======================================================================
    //  LOGGING
    // ======================================================================
    if (ImGui::CollapsingHeader("Logging")) {
        int        level   = IndexOfLevel(live.logThreshold);
        const bool changed = row::Combo("Threshold", &level, kLogLevels, 3);
        Tip("The least important level that still gets through. Set it to Warning "
            "while hunting a specific problem and the Console stops filling with "
            "routine progress.");
        if (changed) {
            std::string error;
            Took(SetString("logging", "threshold", kLogLevels[level], error), error);
        }

        TextRow("File",
                "Where a copy of everything is written, so it survives a crash. "
                "Relative to the project.\n\nPress Enter to apply.",
                live.logFile, m_draft.logFile, "logging", "file");
        Aside("The log file is opened once and held open, so this one takes effect "
              "the next time the editor starts.");
        ImGui::Spacing();
    }

    // ======================================================================
    //  THE GAME'S WINDOW
    // ======================================================================
    if (ImGui::CollapsingHeader("The game's window")) {
        Aside("These describe the window the BUILT GAME opens. The editor hosts the "
              "same engine and deliberately ignores them for its own window - "
              "otherwise a project that shipped full screen at a locked size would "
              "open the editor that way, with no way back.");

        TextRow("Title", "What appears in the game window's title bar.\n\nPress "
                         "Enter to apply.",
                live.windowTitle, m_draft.windowTitle, "window", "title");
        IntRow("Width", "", live.windowWidth, m_draft.windowWidth, 1.0f, 64, 16384,
               "window", "width");
        IntRow("Height", "", live.windowHeight, m_draft.windowHeight, 1.0f, 64, 16384,
               "window", "height");
        BoolRow("Resizable",
                "Off pins the game's window to the width and height above. "
                "Letterboxing keeps a pinned shape correct at any size, so this is "
                "about style rather than correctness - but a pixel game shown at "
                "exactly 3x has one right answer and a hundred wrong ones.",
                live.windowResizable, "window", "resizable");
        BoolRow("Full screen",
                "Borderless, keeping the desktop's own mode. Works with a pinned "
                "presentation, because whatever space the shape does not fill is "
                "black.",
                live.fullscreen, "window", "fullscreen");
        ImGui::Spacing();
    }

    // ======================================================================
    //  STARTUP
    // ======================================================================
    if (ImGui::CollapsingHeader("Startup")) {
        TextRow("Scene",
                "The scene the GAME opens with. Press Enter to apply.",
                live.startupScene, m_draft.startupScene, "startup", "scene");
        Aside("The build list wins over this when there is one - see the Build "
              "panel. This is the fallback.");
        ImGui::Spacing();
    }

    // ======================================================================
    //  AND THE TWO THINGS THAT ARE NOT HERE
    // ======================================================================
    ImGui::Separator();
    Aside("Key bindings are in the same file and are NOT edited here - they belong "
          "to the input system and nothing on this panel touches them. Use the Input "
          "Actions panel.");
    Aside("The editor's own colours and font are yours rather than the project's, "
          "and are in Edit -> Options.");
}

} // namespace editor
