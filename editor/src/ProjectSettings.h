#pragma once

// ============================================================================
//  ProjectSettings.h - the editor writing config/engine.json.
//
//  WHY THIS EXISTS. The Game view's aspect-and-resolution dropdown was a
//  PREVIEW and nothing else: it lived in .editor/session.json, which is one
//  person's view of one project, and a built game never saw it. So pinning
//  16:9 in the editor, exporting, and finding the game stretching to fill
//  whatever window it opened in was not a bug in the export - the setting had
//  simply never left the editor.
//
//  There is already a place for it. config/engine.json carries a
//  "presentation" section that the engine reads at start-up, and the export
//  copies that file into the game. The only missing piece was the editor
//  being able to write it.
//
//  ==========================================================================
//  WHICH SETTINGS BELONG HERE, AND WHICH STAY IN THE SESSION
//
//  THE PROJECT'S:  what shape the game renders at, and whether its window may
//                  be resized. Both are statements about the GAME. They are
//                  the same for everybody working on it and they have to ship.
//
//  THE PERSON'S:   the Game view's scale slider, and any custom aspect ratios
//                  typed into the dropdown. How large the preview is drawn on
//                  one monitor is nobody else's business, and it must not
//                  change what ships.
//
//  Getting that split wrong in either direction is a real cost: a scale
//  slider in the project file would have teammates fighting over a value that
//  does nothing, and an aspect ratio in the session file is the bug this file
//  was written to fix.
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
// ============================================================================

#include <string>

namespace editor {

// What the project's settings currently say the game renders at: "free", a
// ratio like "16:9", or a size like "1920x1080". Read from the engine's
// already-parsed settings, so this is cheap and matches what is running.
std::string PresentationMode();

// And whether the game's window may be dragged to any size.
bool WindowResizable();

// ---------------------------------------------------------------------------
//  CHANGING THEM, which writes config/engine.json.
//
//  Both apply the change to the RUNNING engine as well as to the file, so the
//  Game view reflects it at once rather than on the next launch - the engine
//  reads this file only at start-up, and a setting that needed a restart to
//  be seen is a setting nobody trusts.
//
//  Return false with a reason when the file could not be written, which is a
//  real case: a read-only checkout, or somebody with the file open.
// ---------------------------------------------------------------------------
bool SetPresentationMode(const std::string& spec, std::string& outError);
bool SetWindowResizable(bool on, std::string& outError);

} // namespace editor
