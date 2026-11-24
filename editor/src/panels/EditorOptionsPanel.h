#pragma once

// ============================================================================
//  EditorOptionsPanel.h - how the EDITOR looks. Yours, not the project's.
//
//  The other settings panel in this editor writes config/engine.json, which
//  is the project's and ships inside the built game. This one writes a file
//  in your own settings folder that no project ever sees - see EditorPrefs.h
//  for why the line is drawn there and not somewhere more convenient.
//
//  ==========================================================================
//  WHAT IT COVERS, AND WHAT IT HONESTLY DOES NOT
//
//  COVERED: the theme, the font and its size, how large the interface is
//  drawn, and any of Dear ImGui's colours you want to override.
//
//  NOT COVERED: roughly a hundred colours written into the panels themselves
//  - the red of an error line, the amber of UNSAVED, the tint of a selected
//  asset. Those are not ImGui style colours and nothing here reaches them.
//  Said out loud in the panel rather than left to be discovered, because
//  "I set the theme to Light and half the editor ignored me" is otherwise a
//  bug report.
//
//  NOT COVERED EITHER: the panel layout. Window sizes are remembered in
//  .editor/imgui.ini in pixels, so raising the interface scale makes the
//  panels relatively smaller until they are dragged. There is no fixing that
//  from here; the panel says so.
// ============================================================================

#include "Panel.h"

#include <string>

namespace editor {

class EditorOptionsPanel : public Panel {
public:
    const char* Title() const override { return "Options"; }
    void        Draw() override;

private:
    // The last refusal from writing the preferences file. The change itself
    // has already been applied - you can see it - so this is about the file
    // not being saved, which is worth saying and is not an emergency.
    std::string m_error;

    // Filter for the colour list. Sixty-three colours is more than anybody
    // scrolls through to find "the one behind a menu".
    std::string m_colorFilter;

    // Whether the colour list is showing every colour or only the ones that
    // have been changed from the theme.
    bool m_onlyChanged = false;

    void DrawAppearance();
    void DrawFont();
    void DrawColors();

    // Applies and writes, recording a refusal. Every edit ends here.
    void Commit();
};

} // namespace editor
