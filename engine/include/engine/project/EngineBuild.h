#pragma once

// ============================================================================
//  EngineBuild.h - which engine build is this, and does it match what the
//  project's scripts were compiled against?
//
//  ==========================================================================
//  WHY AN ENGINE NEEDS TO KNOW ITS OWN NAME
//
//  Once a launcher can install more than one engine build and point a project
//  at whichever one it likes, two questions appear that did not exist while
//  there was only ever one:
//
//    "Which engine am I about to open this project with?" - asked by the
//    launcher, which has a folder full of executables and has to show the
//    user something more useful than a path.
//
//    "Were these scripts built against THIS engine?" - asked by the editor,
//    and it is the dangerous one.
//
//  ==========================================================================
//  THE DANGEROUS ONE, SPELLED OUT
//
//  A project's scripts compile into DLLs that are loaded into the editor and
//  pass std::string and std::vector across the boundary. They must be built
//  against the same headers and the same C runtime as the engine that loads
//  them. ScriptBuild already records the C runtime flavour and the path of
//  the engine library it linked, and rebuilds everything when either changes.
//
//  That catches two engine builds living in DIFFERENT folders. It does not
//  catch an engine REPLACED IN PLACE - which is exactly what installing a new
//  version over an old one is, and exactly what a launcher that manages
//  engine versions makes easy. The scripts look up to date because nothing
//  about their own sources changed and the library path is the same string.
//
//  What happens then is documented at length above CurrentBuildStamp in
//  editor/src/ScriptBuild.cpp, and the short version is that it does not
//  crash where you could see it: the library loads, the Console reports
//  success, and the program dies on the first allocation that crosses the
//  heap boundary. Two lines of stamp prevent the entire class of it.
//
//  ==========================================================================
//  WRITTEN BY THE BUILD, NOT COMPILED IN
//
//  engine-version.json is produced by a POST_BUILD step beside engine.dll,
//  and read from there at run time. It is a FILE rather than a baked-in
//  constant for one reason: the launcher has to read it too, and the
//  launcher cannot run an executable to ask it what it is - the whole point
//  is to decide which one to run.
//
//  A missing file is not an error. A hand-built engine, or one from before
//  this existed, reports the id "unknown", and everything carries on with
//  exactly the staleness checking it had before.
//
//  ==========================================================================
//  TWO NUMBERS, BECAUSE THEY CHANGE AT DIFFERENT RATES
//
//  `version` is for people - it goes in a window title and a launcher's list,
//  and it is bumped by hand in the top-level CMakeLists.
//
//  `scriptAbi` is for the build stamp, and it is bumped when something a
//  script can SEE changes shape: a field added to a struct a script holds by
//  value, a virtual function added to ScriptBehaviour, an enum reordered.
//  Keeping them apart means releasing a version with no ABI change does not
//  force every project in the world to recompile its scripts, and a change
//  that does need it cannot be forgotten because the version was already
//  bumped for something else.
// ============================================================================

#include <string>

namespace eng {

struct EngineBuildInfo {
    // What to show a person: "0.1.0 Debug", or "unknown" when there is no
    // engine-version.json to read. Also what gets recorded in a project's
    // manifest as the build it was last opened with.
    std::string id;

    std::string version;         // "0.1.0"
    std::string configuration;   // "Debug" / "Release"

    // The number the script build stamp cares about. Zero means "no file was
    // found", which is distinguishable from any real ABI.
    int scriptAbi = 0;

    // False when engine-version.json was missing or unreadable. Everything
    // still works; the id is just "unknown".
    bool known = false;
};

class EngineBuild {
public:
    static constexpr const char* kFileName = "engine-version.json";

    // The build that is running, read once from beside this executable and
    // remembered. Safe to call before any subsystem is up - it uses
    // std::filesystem directly and never goes through FileSystem, because it
    // is about the ENGINE's location rather than the project's.
    static const EngineBuildInfo& Running();

    // The build installed in some other folder - what a launcher calls for
    // each engine it knows about. `known` is false when that folder holds no
    // engine-version.json.
    static EngineBuildInfo ReadFrom(std::string_view realDirectory);
};

} // namespace eng
