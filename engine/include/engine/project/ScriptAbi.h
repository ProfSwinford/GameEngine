#pragma once

// ============================================================================
//  ScriptAbi.h - were these compiled scripts built for THIS engine?
//
//  ==========================================================================
//  THE FAILURE THIS EXISTS TO PREVENT
//
//  A project's scripts compile into DLLs that are loaded into the running
//  program, and they pass std::string and std::vector across that boundary.
//  Both sides must therefore agree about how those are laid out and which
//  heap they came from, which means the scripts must be built against the
//  same headers and the same C runtime as the engine loading them.
//
//  WHEN THEY DISAGREE, NOTHING SAYS SO. The library loads - it is a valid
//  DLL. Every symbol resolves. The log reports success. The program then dies
//  on the first allocation that crosses the heap boundary, somewhere with no
//  visible connection to the cause. Reproduced in this project: a player
//  built Debug, run against scripts the Release editor had compiled, exits
//  with an access violation and NOT ONE LINE of output. Worse, some scenes
//  survive and others do not, so it reads as a bad scene rather than a bad
//  build.
//
//  ==========================================================================
//  WHY THE ENGINE OWNS THIS AND NOT THE EDITOR
//
//  The editor already checked it, in ScriptBuild's CurrentBuildStamp, and
//  acts on it correctly: a changed stamp recompiles every script. That is the
//  right answer for the editor, and it is not available to anybody else -
//  THE PLAYER HAS NO COMPILER. It cannot rebuild, so its only options are to
//  load and die, or to refuse and say why.
//
//  So the knowledge moved here, where both programs can reach it, and the
//  editor's stamp is now this one plus a line of its own. One definition: two
//  copies of "what makes a script loadable" that drift apart would mean the
//  editor happily rebuilding for a difference the player ignores, or the
//  reverse.
//
//  ==========================================================================
//  WHAT IS COMPARED, AND WHAT IS DELIBERATELY NOT
//
//  Compared: the C runtime flavour, the engine build's identity, and the
//  script ABI number. All three are things this engine can work out about
//  ITSELF, which is the test for whether a line belongs here.
//
//  Not compared: the path of the engine library the editor linked against.
//  The engine cannot know what spelling of its own location a build used, and
//  guessing would produce mismatches that are not real. The editor still
//  records it and still cares, which is why Check IGNORES KEYS IT DOES NOT
//  KNOW rather than treating them as a disagreement - that is what lets the
//  two layers record different things in one file.
//
//  ==========================================================================
//  THREE ANSWERS, NOT TWO
//
//  Match and Mismatch are obvious. Unverifiable is the one that matters:
//  nothing was recorded, or what was recorded predates this check.
//
//  AN EXPORTED GAME IS ALWAYS UNVERIFIABLE, and that is correct rather than a
//  gap. It ships one userContent.dll built by the editor that exported it,
//  beside the engine.dll that went with it, so they match by construction and
//  there is no build folder to record anything in. Treating "nothing
//  recorded" as a problem would print a warning in every shipped game, which
//  is exactly the kind of noise that teaches people to ignore warnings.
//
//  So: a disagreement is refused, and silence is trusted. Only a stamp that
//  EXISTS and is missing the keys gets a warning, because that is somebody
//  mid-upgrade rather than somebody shipping.
// ============================================================================

#include <string>
#include <string_view>

namespace eng {

struct ScriptAbiCheck {
    enum class Verdict {
        Match,          // built for this engine
        Unverifiable,   // nothing recorded, or recorded before this existed
        Mismatch,       // definitely built for a different one
    };

    Verdict verdict = Verdict::Unverifiable;

    // What differs and what to do about it, ready to log. Empty for Match.
    //
    // ONE STRING RATHER THAN THE TWO VALUES, because the useful message is
    // not "a != b" - it is which build made these scripts, which build is
    // trying to load them, and the command that fixes it.
    std::string explanation;
};

class ScriptAbi {
public:
    // Where the stamp lives, as a virtual path. Named here rather than in the
    // editor so that the writer and the reader cannot disagree about it.
    static std::string StampVirtualPath();

    // What a script library must have been built against to be loadable by
    // this engine, as `key=value` lines. This is the authority; everything
    // else in this file compares against it.
    static std::string Stamp();

    // Compares a recorded stamp against Stamp(). Keys this engine does not
    // know about are ignored - see the header.
    static ScriptAbiCheck Check(std::string_view recorded);

    // Reads the stamp from the project and checks it. The one call
    // ScriptLibrary makes at start-up.
    //
    // Needs the file system to be up, which it is: FileSystem is the second
    // subsystem to start and scripts are the ninth.
    static ScriptAbiCheck CheckRecorded();
};

} // namespace eng
