#pragma once

// ============================================================================
//  EditorAppearance.h - putting an EditorPrefs into Dear ImGui.
//
//  The half of the editor's appearance that has to know what ImGui is. The
//  other half - what the settings ARE and how they are stored - is in
//  EditorPrefs.h and knows nothing about it, which is what lets that half be
//  unit tested. See the note in tests/CMakeLists.txt.
//
//  ==========================================================================
//  WHY THE STYLE IS REBUILT FROM SCRATCH EVERY TIME
//
//  ImGuiStyle::ScaleAllSizes MULTIPLIES what is already there. Calling it
//  twice with 1.5 gives you 2.25, so a scale slider applied to the live style
//  would grow the interface every frame it was touched and never shrink it
//  back. So Apply starts from a default-constructed ImGuiStyle, lays the
//  theme on it, scales THAT, and assigns the result - which makes the scale
//  an absolute number rather than a nudge, and makes "back to 1.0" actually
//  work.
//
//  ==========================================================================
//  TWO THINGS CANNOT BE DONE IN THE MIDDLE OF A FRAME
//
//  Colours and spacing are plain numbers and change the moment they are
//  written. These two do not:
//
//   * THE FONT FILE, because loading a .ttf adds a source to the font atlas,
//     which is not a thing to do while a frame is being built.
//
//   * THE FONT SIZE, which is less obvious and was a real bug. In 1.92
//     ImGuiStyle::FontSizeBase is not a write-only input: the context writes
//     INTO it on every Begin and every End, from a copy it latched at
//     NewFrame. A size written to the style mid-frame is therefore overwritten
//     by the next window that opens, and the editor's font size silently
//     never changed - except after a restart, because start-up runs before
//     any frame.
//
//  So Apply RECORDS both and Pump() does the work. EditorGui::BeginFrame
//  calls Pump before ImGui::NewFrame.
// ============================================================================

#include "EditorPrefs.h"

#include <string>
#include <string_view>

namespace editor::appearance {

// ---------------------------------------------------------------------------
//  The colours ImGui has, by name
//
//  Names rather than numbers, because ImGuiCol_ is an enum that gains entries
//  between versions - a preferences file full of indices would quietly
//  recolour the wrong things after an upgrade. ImGui gives a name for an
//  index; the way back is built here.
// ---------------------------------------------------------------------------
int         ColorCount();
const char* ColorName(int index);
int         ColorIndex(std::string_view name);   // -1 when ImGui has no such colour

// What `theme` gives that colour before any of the person's overrides. This is
// what "reset this one" resets to, and what tells the panel whether a colour
// has been changed at all.
void ThemeColor(EditorTheme theme, int index, float outRgba[4]);

// ---------------------------------------------------------------------------
//  Applying
// ---------------------------------------------------------------------------

// Theme, colour overrides, font size and interface scale. Safe during a frame:
// the next widget drawn uses the new style.
void Apply(const EditorPrefs& prefs);

// Does whatever Apply deferred: the font size, and the font file if it has
// changed. Must be called BETWEEN frames. Doing nothing is the normal case.
void Pump();

// Empty when the font is fine. Otherwise why the last one did not load, ready
// to show in the panel - a font that silently did not apply is worse than one
// that says so.
const std::string& FontProblem();

// ---------------------------------------------------------------------------
//  The appearance the editor is running on
//
//  ONE COPY, and the panel edits it in place. There is no "pending" set and no
//  Apply button: an options panel whose changes you cannot see until you press
//  something is an options panel you tune by guessing.
// ---------------------------------------------------------------------------
EditorPrefs& Current();

// Reads the per-user file and applies it. Called once, from EditorGui::Init,
// before the first frame - so the editor never draws a frame in the wrong
// colours on the way to the right ones.
void LoadAndApply();

// Applies Current() and writes it to the per-user file. The panel calls this
// after every edit, which is what makes the file match what you can see.
// False with a reason when the file could not be written; the change is still
// applied, because refusing to show you a colour because a disk is full would
// be a strange way to help.
bool Commit(std::string& outError);

} // namespace editor::appearance
