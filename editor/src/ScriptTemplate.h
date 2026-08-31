#pragma once

// ============================================================================
//  ScriptTemplate.h - the starter file that "New Script" writes.
//
//  Unity hands you a script with Start and Update already written out, and
//  that template teaches more people what the lifecycle is than the manual
//  does. It works because the explanation is in the file you are already
//  looking at, at the moment you need it. This does the same job.
//
//  It has one extra thing to say that Unity's does not: because scripts here
//  are compiled C++, the template also has to tell you to rebuild - otherwise
//  the first experience of the feature is writing a script that never runs.
// ============================================================================

#include <string>
#include <string_view>

namespace editor {

// The full text of a new script called `scriptName`.
std::string DefaultScriptText(std::string_view scriptName);

// Checks that a name can legally become a C++ class name, BEFORE the file is
// written - rather than letting the editor produce a file that does not
// compile. An editor that emits broken code is worse than one that says no.
bool IsValidScriptName(std::string_view name, std::string& outError);

} // namespace editor
