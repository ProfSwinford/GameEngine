#pragma once

// ============================================================================
//  ScriptIde.h - telling an IDE what a script is compiled with.
//
//  ==========================================================================
//  THE PROBLEM, WHICH IS NOT A SMALL ONE
//
//  Open a script from the Assets panel and the operating system hands the .cpp
//  to whatever opens C++ files. In Visual Studio that is a file with no
//  project behind it, so there are no include directories, so the first line
//  of every script - #include <engine/Engine.h> - cannot be found.
//
//  Engine.h is the umbrella over every public header. So ONE failed include
//  leaves every eng:: name in the file unknown: no completion, no parameter
//  hints, no go-to-definition, and a red squiggle under most of the file.
//  Not degraded assistance. None.
//
//  What it takes to fix that is small, and the editor already knows all of
//  it: two include directories, a standard, three switches and one forced
//  include - see CompileSettings in ScriptBuild.h. The only thing missing was
//  somewhere to write it down that an IDE actually reads.
//
//  ==========================================================================
//  TWO FILES, BECAUSE NOTHING READS BOTH
//
//  CppProperties.json is what Visual Studio reads, in the mode it calls Open
//  Folder. It is the only documented way to configure IntelliSense for a
//  folder that is not a CMake or MSBuild project, and Visual Studio does not
//  read a compile database in any mode - so for the IDE this file is not one
//  option among several, it is the mechanism.
//
//  compile_commands.json is what everything else reads: clangd, and through
//  clangd VS Code, CLion, Qt Creator, Neovim and Emacs. It is a list of
//  commands rather than a description of a folder, which is why it has to
//  name every script one at a time.
//
//  ==========================================================================
//  WRITTEN WHEN ASKED, AND NEVER OTHERWISE
//
//  Both land in the project folder, beside assets/, because that is where an
//  IDE looks for them. Neither is written because a project was opened.
//  ProjectMenu.cpp already states that rule, about the project file: writing
//  into somebody's folder because they opened it is how a tool stops being
//  trusted. Two more generated files in the root of a stranger's repository
//  would be exactly that.
//
//  THEY ARE ALSO NOT WORTH COMMITTING, and that is not an oversight. The
//  include directories are absolute, and the set of them is different in a
//  source tree and beside a released editor, so a copy from another machine
//  is not stale - it is wrong, and wrong in a way that reads as broken engine
//  headers. Regenerating is one menu item or one command, which is cheaper
//  than diagnosing that once.
//
//  ==========================================================================
//  WHERE THIS IS DELIBERATELY STRICTER THAN THE BUILD
//
//  The compiler never sees a project's .cpp. It sees a generated unit that
//  includes <engine/Engine.h> and then includes the script, and for a script
//  with exposed variables that unit also includes <cstddef>, <iterator>,
//  <string> and <type_traits> ahead of it.
//
//  The forced include below reproduces the first of those and not the other
//  four. So a script that compiles only because the exposer unit happened to
//  include <string> for its own reasons will squiggle in the IDE.
//
//  THAT IS THE DIRECTION TO ERR IN. The IDE complaining about something the
//  build accepts costs one include; the IDE staying quiet about something the
//  build rejects costs a build cycle and reads as a broken toolchain. And the
//  fix - naming <string> yourself - is what the script should have done
//  anyway, for the same reason the generated unit names its own.
// ============================================================================

#include <string>

namespace editor {

// Writes CppProperties.json and compile_commands.json into the project folder.
//
// True on success, with a one-line summary naming what was written, for the
// status bar. False with a reason, which is an ordinary outcome rather than a
// fault: there may be no project open, no scripts to describe, no compiler to
// describe them with, or a folder that cannot be written to.
bool WriteIdeFiles(std::string& outSummary, std::string& outError);

// Opens the project FOLDER in Visual Studio. The folder, not a file, and that
// is the entire point: Open Folder mode is what reads CppProperties.json, and
// handing devenv.exe a lone .cpp is what already happens when you open a
// script from the Assets panel - which is the case with no IntelliSense at
// all.
//
// False with a reason when Visual Studio is not installed, when no project is
// open, or anywhere that is not Windows.
bool OpenInVisualStudio(std::string& outError);

} // namespace editor
