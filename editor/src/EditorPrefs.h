#pragma once

// ============================================================================
//  EditorPrefs.h - how the EDITOR looks, remembered per person.
//
//  ==========================================================================
//  WHY THIS IS NOT config/engine.json, AND NOT .editor/session.json
//
//  The project already has two places state can go, and this is neither.
//
//  config/engine.json is THE PROJECT'S, and the export packs it into the
//  built game. A font size in there would ship inside somebody's game, and
//  would mean two people on one project arguing about a setting that does not
//  affect the game at all. See ProjectSettings.h for that half of the rule.
//
//  .editor/session.json is ONE PERSON'S STATE ABOUT ONE PROJECT - which scene
//  was open, where the Scene view was pointing. The qualifier is the point:
//  "I like a 15px font" is one person's state about NO project, and putting it
//  there would mean setting it again for every project you ever open.
//
//  So it goes where the operating system keeps that sort of thing, through
//  eng::UserPaths - the same place the recent-projects list already lives.
//  See fs/UserPaths.h, which states this rule in its own words.
//
//  ==========================================================================
//  THIS FILE KNOWS NOTHING ABOUT DEAR IMGUI, ON PURPOSE.
//
//  Colours are four floats and a name; a theme is one of three words. Nothing
//  here includes imgui.h, which is what lets it be compiled into the unit test
//  program - see the comment in tests/CMakeLists.txt about why an editor file
//  that reaches for ImGui cannot be. EditorAppearance.h is the half that does know,
//  and it is the only half that cannot be tested.
//
//  ==========================================================================
//  THE ABSENCE OF THE FILE IS THE DEFAULTS.
//
//  There is no written-out table of default values anywhere: a fresh install
//  has no file, Load leaves the struct at its member initialisers, and
//  "Reset everything" deletes the file rather than writing the defaults into
//  it. So the defaults cannot drift from what a new user gets, and a
//  half-understood file can always be fixed by throwing it away.
// ============================================================================

#include <engine/core/Json.h>

#include <array>
#include <map>
#include <string>

namespace editor {

// One of Dear ImGui's three built-in themes, which everything else is laid on
// top of. Named rather than numbered in the file, so a reordering here cannot
// silently change somebody's colours.
enum class EditorTheme { Dark, Light, Classic };

struct EditorPrefs {
    EditorTheme theme = EditorTheme::Dark;

    // ======================================================================
    //  THE FONT SIZE IS ALWAYS A REAL NUMBER, never "0 meaning the default".
    //
    //  Dear ImGui 1.92 fills an unset size in on the first frame, and what it
    //  fills in depends on which font happens to be loaded: 13 for the
    //  built-in bitmap font, 20 for anything else. A person who loaded a .ttf
    //  would have the whole editor jump a third larger without having asked
    //  for anything of the sort. Saying the number out loud removes the
    //  question.
    // ======================================================================
    float fontSize = 13.0f;

    // Everything else in the interface - padding, spacing, the width of a
    // scrollbar, the rounding of a corner - multiplied by this. Separate from
    // the font size because wanting bigger TEXT and wanting a bigger
    // INTERFACE are different wants, and on a high-resolution display they
    // usually point in different directions.
    float uiScale = 1.0f;

    // A .ttf or .otf to use instead of the built-in font. Empty is the
    // built-in one, which is what a fresh install has.
    //
    // AN ABSOLUTE PATH, deliberately: it is one machine's font, not the
    // project's, and nothing here resolves it against a project root.
    std::string fontPath;

    // ======================================================================
    //  ONLY THE COLOURS THAT DIFFER FROM THE THEME.
    //
    //  Keyed by Dear ImGui's own name for the colour - "WindowBg", "Button",
    //  "TextSelectedBg" - because the numbers behind those names are an enum
    //  that is added to between versions, and a file full of indices would
    //  silently recolour the wrong things after an upgrade.
    //
    //  Storing only the differences is what makes switching theme still work:
    //  a file holding all 63 colours would be a theme of its own and would
    //  make the Dark/Light choice do nothing.
    // ======================================================================
    std::map<std::string, std::array<float, 4>> colors;
};

// ---------------------------------------------------------------------------
//  Reading and writing them as JSON.
//
//  Separate from the file so that both halves can be tested without touching
//  the real user settings folder - and so that an unreadable value costs the
//  one setting rather than the whole file. Nothing here throws, nothing here
//  reports an error: a value that makes no sense is replaced by the default
//  it would have had, which is the only behaviour that leaves somebody able
//  to open their editor.
// ---------------------------------------------------------------------------
EditorPrefs PrefsFromJson(const eng::Json& document);
eng::Json   PrefsToJson(const EditorPrefs& prefs);

// Clamped to what the interface can actually draw with. Called by
// PrefsFromJson, and worth calling again after anything hand-edits a struct.
void ClampPrefs(EditorPrefs& prefs);

// ---------------------------------------------------------------------------
//  The file itself
// ---------------------------------------------------------------------------

// Where it lives: the user settings folder plus "editor-prefs.json". Empty
// when the operating system would not give us a settings folder, which is
// unusual rather than impossible - a read-only or sandboxed home - and which
// every function here copes with by doing nothing.
std::string PrefsFilePath();

// Reads it. A missing or unreadable file is NOT an error: it means the
// defaults, which is what a fresh install has. Returns whether a file was
// actually read, which is only worth knowing in order to say so.
bool LoadPrefs(EditorPrefs& outPrefs);

// Writes it, whole. Unlike config/engine.json this file is written by the
// editor and read by the editor, so there is no hand-written layout to
// preserve and a plain dump is honest.
bool SavePrefs(const EditorPrefs& prefs, std::string& outError);

// Deletes it, which is how "back to the defaults" is spelled - see the note
// at the top. Succeeds when there was nothing there.
bool ForgetPrefs(std::string& outError);

} // namespace editor
