#pragma once

// ============================================================================
//  ProjectSettings.h - the editor reading and writing config/engine.json.
//
//  WHY THIS EXISTS. The Game view's aspect-and-resolution dropdown was a
//  PREVIEW and nothing else: it lived in .editor/session.json, which is one
//  person's view of one project, and a built game never saw it. So pinning
//  16:9 in the editor, exporting, and finding the game stretching to fill
//  whatever window it opened in was not a bug in the export - the setting had
//  simply never left the editor.
//
//  There is already a place for it. config/engine.json carries every setting
//  the engine reads at start-up, and the export copies that file into the
//  game. The only missing piece was the editor being able to write it.
//
//  ==========================================================================
//  WHICH SETTINGS BELONG HERE, AND WHICH DO NOT
//
//  THE PROJECT'S:  what shape the game renders at, how hard gravity pulls,
//                  how big one world unit is, how loud the log is. Statements
//                  about the GAME. They are the same for everybody working on
//                  it and they have to ship.
//
//  THE PERSON'S:   the Game view's scale slider, custom aspect ratios typed
//                  into the dropdown, which panels are open - and the
//                  editor's own colours and font, which are not in this file
//                  at all. See EditorPrefs.h.
//
//  Getting that split wrong in either direction is a real cost: a scale
//  slider in the project file would have teammates fighting over a value that
//  does nothing, and an aspect ratio in the session file is the bug this file
//  was written to fix. The test is simple - does it ship inside the game?
//
//  ==========================================================================
//  IT EDITS THE FILE IN PLACE. IT DOES NOT REGENERATE IT.
//
//  config/engine.json is edited by people and is full of "_note" keys that
//  explain what each setting is for - and the whole "input" section, which
//  InputMap reads and this file knows nothing about. So a writer that dumped
//  a BootConfig back out would silently delete the documentation and every
//  key binding in the project.
//
//  Every function here therefore READS THE FILE FROM DISK, splices the one
//  value asked for into the text, and leaves every other byte where it was.
//  From disk rather than from the engine's parsed copy, because that copy is
//  a BootConfig - the sections it does not model are not in it. And spliced
//  rather than parsed-and-dumped, because dumping it sorts the keys and
//  reflows every line: see the long comment at the top of the .cpp for what
//  that does to somebody's diff.
//
//  ==========================================================================
//  AND THE CHANGE TAKES EFFECT AT ONCE.
//
//  Every writer below ends by calling eng::Engine::ReloadSettings(), which
//  re-reads the file and applies whatever can be applied to the running
//  engine. So gravity changes the moment you let go of the slider, and
//  Config() never disagrees with what is on disk. There is deliberately no
//  per-key "and also tell the engine" step here - one function does it for
//  every key, including keys added later.
//
//  A setting that genuinely cannot be applied while running - the log FILE,
//  and the game's own window - is reported by that function rather than
//  silently ignored. See Engine::ReloadSettings.
// ============================================================================

#include <cstddef>
#include <string>
#include <string_view>

namespace editor {

// ---------------------------------------------------------------------------
//  Reading
//
//  The VALUES come from eng::Engine::Get().Config(), which is cheap, is
//  already parsed, and - because every writer below reloads it - is never out
//  of step with the file. A panel should read it fresh every frame rather
//  than caching, exactly as Panel.h says.
// ---------------------------------------------------------------------------

// What the project's settings say the game renders at: "free", a ratio like
// "16:9", or a size like "1920x1080".
std::string PresentationMode();

// And whether the game's window may be dragged to any size.
bool WindowResizable();

// Whether the file actually SAYS something about this key, as opposed to the
// engine having fallen back on a built-in default.
//
// Worth asking because the two are indistinguishable from BootConfig alone: a
// panel shows 1280 whether `window.width` is in the file or not, and a person
// who has never set it should be able to see that. Reads the file.
bool HasSetting(std::string_view section, std::string_view key);

// ---------------------------------------------------------------------------
//  Writing
//
//  Each one validates, splices the value into config/engine.json, and then
//  reloads the engine's settings so the change is live. Each returns false
//  with a readable reason - a read-only checkout, a file somebody is holding
//  open, a value the engine's own reader refuses - and writes nothing when it
//  does.
//
//  THE SECTION'S "_note" IS NOT A PARAMETER. It is looked up from the section
//  name and written once, when the section has to be created, so that every
//  section the editor adds explains itself the way the hand-written ones do -
//  and so that two callers cannot write two different explanations of the
//  same section.
//
//  CALL THESE ON AN EDGE, NOT EVERY FRAME. Each one is a read, a parse, a
//  splice and a write of the whole file. That is nothing on a checkbox and
//  far too much on a slider being dragged, so a continuous widget should
//  write when ImGui::IsItemDeactivatedAfterEdit() says the drag has finished.
// ---------------------------------------------------------------------------

bool SetString(std::string_view section, std::string_view key,
               std::string_view value, std::string& outError);
bool SetBool(std::string_view section, std::string_view key, bool value,
             std::string& outError);
bool SetInt(std::string_view section, std::string_view key, int value,
            std::string& outError);

// A float, written with the shortest text that reads back as the same float -
// so 32 arrives as "32.0" and 1/60 as "0.016666668", and neither picks up the
// trail of digits that `double` would add.
bool SetNumber(std::string_view section, std::string_view key, float value,
               std::string& outError);

// A list of floats, for a key like physics.gravity that is written [x, y].
bool SetNumbers(std::string_view section, std::string_view key,
                const float* values, std::size_t count, std::string& outError);

// The two the Game view already used, kept because each does something extra:
// the presentation is CHECKED by the engine's own parser before it is written,
// so a value the editor accepted can never be one the game refuses.
bool SetPresentationMode(const std::string& spec, std::string& outError);
bool SetWindowResizable(bool on, std::string& outError);

} // namespace editor
