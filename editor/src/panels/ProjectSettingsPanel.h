#pragma once

// ============================================================================
//  ProjectSettingsPanel.h - config/engine.json, as a form.
//
//  ==========================================================================
//  WHY A PANEL AND NOT A MODAL
//
//  Twenty modal popups in this editor and not one of them is a form: they are
//  confirmations and one-field prompts, which is what a modal is for. A
//  settings form is something you sit with, next to the thing it changes -
//  nudging gravity while watching the Scene view is the entire point - and a
//  modal that greys out the editor behind it makes that impossible.
//
//  ==========================================================================
//  EVERY CHANGE IS LIVE, AND THERE IS NO SAVE BUTTON
//
//  Each control writes config/engine.json when you let go of it, and
//  Engine::ReloadSettings then applies whatever can be applied to the running
//  engine. So gravity changes the moment the slider stops, and the file and
//  the editor are never out of step with each other.
//
//  That is deliberately NOT the Build panel's model, which has Save and
//  Revert. A build list is a thing you arrange and then commit; a setting is
//  a thing you tune and watch. The cost is that there is no undo here beyond
//  typing the old number back, which is the same bargain the Inspector makes.
//
//  WHAT IS NOT IN HERE, and both absences are on purpose:
//
//   * THE "input" SECTION. Key bindings are InputMap's, they are edited in
//     the Input Actions panel, and this panel does not touch that part of
//     the file. The panel says so rather than leaving you looking for it.
//
//   * THE EDITOR'S OWN APPEARANCE. Colours and fonts are the person's, not
//     the project's, and they are in Edit -> Options. See EditorPrefs.h.
// ============================================================================

#include "Panel.h"

#include <engine/core/Config.h>

#include <string>

namespace editor {

class ProjectSettingsPanel : public Panel {
public:
    const char* Title() const override { return "Project Settings"; }
    void        Draw() override;

private:
    // ======================================================================
    //  WHY THERE IS A DRAFT AT ALL, when Panel.h says panels hold no state.
    //
    //  A drag has to go somewhere while it is happening. Reading the engine's
    //  value fresh every frame - which is the house rule and the right rule -
    //  would reset a slider to its old position on the very next frame, so
    //  the number could never move. And writing the file on every frame of a
    //  drag would be a few hundred rewrites of a file holding every key
    //  binding in the project.
    //
    //  So: the draft holds what the widget currently says, each row copies
    //  the engine's value back into it as soon as that row is NOT being
    //  touched, and the file is written once, when the drag ends. Outside a
    //  gesture the draft is therefore always exactly the engine's value,
    //  which is what makes it safe.
    // ======================================================================
    eng::BootConfig m_draft;
    std::string     m_draftPresentation;

    // Whether the draft has been filled in from the project yet. Each row
    // refreshes its own after drawing, which cannot help the FIRST frame -
    // see Draw.
    bool m_seeded = false;

    // The last refusal, shown until it is dismissed. Settings are written to
    // a file that can be read-only, held open, or on a full disk, and a
    // control that silently did nothing is the worst of the three.
    std::string m_error;

    // ---- the row helpers, each of which does the three things above ------
    void FloatRow(const char* label, const char* tip, float live, float& draft,
                  float speed, float min, float max, const char* section,
                  const char* key);
    void IntRow(const char* label, const char* tip, int live, int& draft, float speed,
                int min, int max, const char* section, const char* key);
    void BoolRow(const char* label, const char* tip, bool live, const char* section,
                 const char* key);
    // Whether a text row's value has a grammar the engine checks. Only
    // presentation.mode does - see TextRow for why writing "banana" into it
    // would otherwise be accepted in silence.
    enum class Checked { No, PresentationMode };

    void TextRow(const char* label, const char* tip, const std::string& live,
                 std::string& draft, const char* section, const char* key,
                 Checked checked = Checked::No);

    // Records a refusal, or clears the last one. Every write goes through it
    // so there is one place the error surface is fed from.
    void Took(bool ok, const std::string& error);
};

} // namespace editor
