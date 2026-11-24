// ============================================================================
//  EditorOptionsPanel.cpp - see the header for what this covers and what it
//  honestly does not.
// ============================================================================

#include "EditorOptionsPanel.h"

#include "../EditorAppearance.h"
#include "../EditorPrefs.h"
#include "FieldRow.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace editor {
namespace {

constexpr ImVec4 kErrorRed{0.95f, 0.38f, 0.32f, 1.0f};

void Aside(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

std::string Lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

const char* const kThemeNames[] = {"Dark", "Light", "Classic"};

// ============================================================================
//  A ROW WITH A BUTTON AFTER IT HAS TO LEAVE ROOM FOR THE BUTTON.
//
//  FieldRow's rows are given SetNextItemWidth(-FLT_MIN), so they end flush
//  with the window's work rect. Anything SameLine'd after one therefore
//  starts at the clip rect's right edge and is silently thrown away by
//  ItemAdd - which is how a "Clear" and a per-colour "Reset" came to be
//  invisible while reading perfectly sensibly in the source.
//
//  Called immediately BEFORE the row:: call, because Row's constructor
//  overwrites a pending width only when the caller has not set one.
void MakeRoomFor(const char* buttonLabel) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float       button =
        ImGui::CalcTextSize(buttonLabel).x + style.FramePadding.x * 2.0f +
        style.ItemSpacing.x;
    ImGui::SetNextItemWidth(ImMax(1.0f, row::ValueWidth() - button));
}

bool SameColor(const float a[4], const float b[4]) {
    // Exact, not approximate: these numbers are only ever copied from a theme
    // or typed into a picker, never computed, so "has this been changed?" has
    // an exact answer and a tolerance would only hide a real override.
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

} // namespace

void EditorOptionsPanel::Commit() {
    std::string error;
    m_error = appearance::Commit(error) ? std::string() : error;
}

void EditorOptionsPanel::DrawAppearance() {
    EditorPrefs& prefs = appearance::Current();

    int theme = static_cast<int>(prefs.theme);
    if (row::Combo("Theme", &theme, kThemeNames, 3)) {
        prefs.theme = static_cast<EditorTheme>(theme);
        Commit();
    }

    // ----------------------------------------------------------------------
    //  A SLIDER WRITES WHEN IT IS LET GO, NOT WHILE IT MOVES.
    //
    //  The appearance itself follows the drag - the whole point is to see the
    //  size you are choosing - but the FILE is written once, at the end.
    //  Without the split, dragging across twenty pixel sizes would be twenty
    //  writes of the preferences file.
    // ----------------------------------------------------------------------
    float fontSize = prefs.fontSize;
    if (row::DragFloat("Font size", &fontSize, 0.25f, 7.0f, 48.0f, "%.0f px")) {
        prefs.fontSize = fontSize;
        ClampPrefs(prefs);
        appearance::Apply(prefs);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Commit();
    }

    float scale = prefs.uiScale;
    if (row::DragFloat("Interface scale", &scale, 0.01f, 0.5f, 3.0f, "%.2fx")) {
        prefs.uiScale = scale;
        ClampPrefs(prefs);
        appearance::Apply(prefs);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Commit();
    }

    Aside("Interface scale sizes the padding, spacing and borders - not the text, "
          "which is the font size above. Panels you have already arranged keep "
          "their old pixel sizes, because the layout is remembered in pixels; drag "
          "an edge once and it settles.");
}

void EditorOptionsPanel::DrawFont() {
    EditorPrefs& prefs = appearance::Current();

    std::array<char, 512> buffer{};
#ifdef _MSC_VER
    strncpy_s(buffer.data(), buffer.size(), prefs.fontPath.c_str(), _TRUNCATE);
#else
    std::strncpy(buffer.data(), prefs.fontPath.c_str(), buffer.size() - 1);
#endif

    MakeRoomFor("Clear");
    if (row::InputTextWithHint("Font file", "(the built-in font)", buffer.data(),
                               buffer.size(),
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
        prefs.fontPath = buffer.data();
        Commit();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        prefs.fontPath.clear();
        Commit();
    }

    Aside("A .ttf or .otf, by full path - press Enter to apply. Empty is the font "
          "built into the editor. On Windows the system fonts are in "
          "C:\\\\Windows\\\\Fonts.");

    // A font that was moved or uninstalled since it was chosen is an ordinary
    // Tuesday, so it is reported here rather than being a crash or a silence.
    if (!appearance::FontProblem().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorRed);
        ImGui::TextWrapped("%s - using the built-in font",
                           appearance::FontProblem().c_str());
        ImGui::PopStyleColor();
    }
}

void EditorOptionsPanel::DrawColors() {
    EditorPrefs& prefs = appearance::Current();

    std::array<char, 128> filter{};
#ifdef _MSC_VER
    strncpy_s(filter.data(), filter.size(), m_colorFilter.c_str(), _TRUNCATE);
#else
    std::strncpy(filter.data(), m_colorFilter.c_str(), filter.size() - 1);
#endif
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    if (ImGui::InputTextWithHint("##colourfilter", "Filter, e.g. \"button\"",
                                 filter.data(), filter.size())) {
        m_colorFilter = filter.data();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Only changed", &m_onlyChanged);

    const std::string needle  = Lowered(m_colorFilter);
    int               changed = 0;

    for (int i = 0; i < appearance::ColorCount(); ++i) {
        const char* name = appearance::ColorName(i);

        float base[4] = {};
        appearance::ThemeColor(prefs.theme, i, base);

        const auto  override_  = prefs.colors.find(name);
        const bool  isOverride = override_ != prefs.colors.end();
        if (isOverride) {
            ++changed;
        }

        if (m_onlyChanged && !isOverride) {
            continue;
        }
        if (!needle.empty() && Lowered(name).find(needle) == std::string::npos) {
            continue;
        }

        float value[4] = {base[0], base[1], base[2], base[3]};
        if (isOverride) {
            value[0] = override_->second[0];
            value[1] = override_->second[1];
            value[2] = override_->second[2];
            value[3] = override_->second[3];
        }

        ImGui::PushID(name);

        // Live while dragging inside the picker, written when it closes - the
        // same bargain as the sliders above, and for the same reason.
        MakeRoomFor("Reset");
        if (row::ColorEdit4(name, value, ImGuiColorEditFlags_AlphaBar)) {
            prefs.colors[name] = {value[0], value[1], value[2], value[3]};
            appearance::Apply(prefs);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            // A colour dragged back to exactly the theme's is not an override
            // any more. Removing it rather than storing a copy is what keeps
            // switching theme meaningful: an entry here would survive the
            // switch and quietly pin one colour to the old theme's.
            if (SameColor(value, base)) {
                prefs.colors.erase(name);
                appearance::Apply(prefs);
            }
            Commit();
        }

        // ASKED AGAIN, because the edit above may just have made this colour
        // an override - and a Reset that only appeared on the following frame
        // would make the panel's own "change one and a Reset appears beside
        // it" a frame late.
        if (prefs.colors.find(name) != prefs.colors.end()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                prefs.colors.erase(name);
                Commit();
            }
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (changed == 0) {
        Aside("No colours changed - every one of them is the theme's. Change one "
              "and a Reset appears beside it.");
    } else {
        ImGui::Text("%d colour%s changed from the theme.", changed,
                    changed == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all colours")) {
            prefs.colors.clear();
            Commit();
        }
    }
}

void EditorOptionsPanel::Draw() {
    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorRed);
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
        Aside("The change is applied - it just could not be remembered for next "
              "time.");
        if (ImGui::SmallButton("Dismiss")) {
            m_error.clear();
        }
        ImGui::Separator();
    }

    Aside("How the EDITOR looks. Yours, not the project's - nothing here is part of "
          "a project or ends up in a built game. The project's own settings are in "
          "the Project Settings panel.");
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawAppearance();
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Font", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawFont();
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Colours")) {
        DrawColors();
        ImGui::Spacing();
    }

    ImGui::Separator();

    // ======================================================================
    //  WHERE IT IS, AND HOW TO THROW IT AWAY.
    //
    //  Both halves taken from the Inspector's import-settings footer, which
    //  settled this already: say where the file is so the settings are never
    //  a mystery, and spell "back to the defaults" as deleting it rather than
    //  as writing a table of default values that can drift from the real ones.
    // ======================================================================
    const std::string path = PrefsFilePath();
    if (path.empty()) {
        Aside("This machine would not give the editor a settings folder, so nothing "
              "here can be remembered for next time.");
    } else {
        ImGui::TextDisabled("Saved in %s", path.c_str());
    }

    if (ImGui::Button("Reset everything to defaults")) {
        std::string error;
        if (ForgetPrefs(error)) {
            appearance::Current() = EditorPrefs{};
            appearance::Apply(appearance::Current());
            m_error.clear();
        } else {
            m_error = error;
        }
    }

    ImGui::Spacing();
    Aside("Around a hundred colours are written into the panels themselves - the "
          "red of an error, the amber of UNSAVED, the tint of a selected asset - "
          "and are not Dear ImGui style colours, so nothing on this panel reaches "
          "them. They stay as they are whichever theme you choose.");
}

} // namespace editor
