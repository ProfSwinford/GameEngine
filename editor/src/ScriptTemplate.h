#pragma once
// =============================================================================
//  THE DEFAULT SCRIPT - what "New Script" writes.
//
//  Unity hands you a MonoBehaviour with Start and Update already stubbed, and
//  that template teaches more people what the lifecycle is than the manual
//  does. It works because the explanation is in the file you are already
//  looking at, at the moment you need it.
//
//  This one does the same job with one extra obligation: because scripts here
//  are compiled C++, the template also has to say what to do after saving -
//  rebuild - or the first experience of the feature is writing a script that
//  never runs.
// =============================================================================

#include <string>

namespace editor {

// Returns the full text of a new script named `scriptName`. The name is
// assumed to have been validated by IsValidScriptName first.
std::string DefaultScriptText(std::string_view scriptName);

// A script name has to be a legal C++ identifier, because it becomes a class
// name and is pasted into a macro. Checked BEFORE the file is written rather
// than discovered as a compile error in a file the editor generated - an
// editor that emits code which does not compile is worse than one that refuses
// the name.
bool IsValidScriptName(std::string_view name, std::string& outError);

} // namespace editor
