#pragma once

// ============================================================================
//  Project.h - what makes a folder a project, and the file that names it.
//
//  ==========================================================================
//  WHY THIS EXISTS
//
//  The engine used to find its content by looking for an `assets` folder
//  beside the executable and then walking upwards until it found one. That
//  works perfectly for one project living in the source tree, and not at all
//  for somebody who keeps their games in `D:/games/` and the editor in
//  `C:/Program Files/`.
//
//  So the root became something the PROCESS IS TOLD rather than something it
//  guesses: `editor --project D:/games/orbit`. This file is the part that
//  answers "is that folder actually a project?" and "what is it called?".
//
//  ==========================================================================
//  EVERY PATH HERE IS A REAL PATH, AND THAT IS THE WHOLE POINT
//
//  Nothing in this file goes through FileSystem::Resolve, and nothing may be
//  changed to. Resolve turns a virtual path into a real one by prefixing the
//  project root - and this is the code that ESTABLISHES the root. Reading the
//  manifest through Resolve would mean needing the answer in order to find
//  the question: `Resolve("project.oproj")` names
//  `<root>/assets/project.oproj`, which is not where the file is.
//
//  It could have been bent into the virtual system. Resolve keeps a short
//  hand-written list of prefixes that live BESIDE assets/ rather than inside
//  it - `config/`, `logs/`, `.build/`, `.editor/` - and the manifest could
//  have become a fifth entry. That list exists twice, though (CouldBeArchived
//  in FileSystem.cpp keeps its own copy for a different reason), so every
//  addition is two edits in two files that must agree, and the reward would
//  be using a virtual path for exactly one file that is read once.
//
//  Staying out of it buys something better: THE HUB CAN USE THIS CODE. A
//  launcher showing a list of projects has no project open, therefore no
//  root, therefore no working Resolve - and it still has to read every
//  manifest to know what the projects are called.
//
//  ==========================================================================
//  A FOLDER IS A PROJECT IF IT HAS A MANIFEST **OR** AN assets/ FOLDER
//
//  The `or` is not laziness, it is the entire backward-compatibility story.
//  This repository has no `project.oproj` and never needs one: it has
//  `assets/`, so it is a project, and the player, `editor --build-scripts`
//  and the launcher's own "there is a project beside this engine" row all
//  find it by exactly the upward walk the engine always used.
//
//  FileSystem::LooksLikeRoot used to be its own one-line `is_directory(dir /
//  "assets")`. It now calls Looks() below, so there is ONE definition of "is
//  this a project" in the tree rather than two that can disagree.
//
//  Nothing is ever written into somebody's folder uninvited. A project with
//  no manifest opens with a single line in the Console saying one can be
//  written, and a menu item that writes it on request. A tool that creates
//  files in your tree without asking is how people stop trusting it.
//
//  ==========================================================================
//  WHAT IS DELIBERATELY NOT IN THE MANIFEST
//
//  No asset paths, no scene list, no window size, no key bindings. Those are
//  `config/engine.json` and `assets/scenes/build.json`, they already exist,
//  and they already travel with the root - duplicating any of them here would
//  create two answers to one question.
//
//  NO ABSOLUTE PATHS OF ANY KIND, so a project folder can be moved, copied,
//  zipped or committed and still opens. `realRoot` below is filled in from
//  where the file was FOUND; it is never read from the file.
//
//  And no recent-projects list and no window geometry: that is one person's
//  state rather than the project's, and it lives beside the user's other
//  settings. See UserPaths.h.
// ============================================================================

#include <engine/core/Json.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

// Everything known about one project. Filled in by Read(), and by Create()
// for a project that did not exist a moment ago.
struct ProjectInfo {
    // Where the project is, as an absolute real path with no trailing
    // separator. FILLED IN FROM WHERE THE MANIFEST WAS FOUND, never read out
    // of it - a path inside the file would be wrong the first time the folder
    // moved.
    std::string realRoot;

    // What to call it in a window title or a list of projects. NOT the folder
    // name: `.../work/game` and `.../archive/game` would both read as "game"
    // and a launcher showing two identical rows is no use to anybody.
    //
    // Falls back to the folder's own name when the manifest does not say, so
    // a hand-written one-line manifest still works.
    std::string name;

    // The engine install this project was last opened with, rewritten on
    // every successful open.
    //
    // A RECORD, NEVER A REQUIREMENT. Nothing refuses to open a project
    // because of what is in here; a launcher may warn before opening a
    // project with an older engine, and that is all. Refusing is how a tool
    // makes somebody's work unopenable, which is a far worse failure than
    // whatever the mismatch would have caused.
    std::string engineVersion;

    // Addons this project uses, BY NAME. Reserved - nothing reads it yet.
    //
    // It is in the format now rather than later because adding a field is a
    // format version bump and this one is already predictable. Names, not
    // paths: an addon is resolved against `<root>/addons/<name>/`, so a
    // project and its addons move together and can be committed together,
    // and no entry here can point at a folder that has since been deleted.
    std::vector<std::string> addons;

    // Keys this version of the engine did not recognise, kept so that Write()
    // can put them back exactly as they were.
    //
    // Silently dropping a field you did not understand is how a file that two
    // versions of a tool share becomes a file neither can be trusted with.
    Json unknown;

    // False when the folder is a project only because it has an assets/
    // folder. The editor says so once, and offers to write one.
    bool hasManifest = false;
};

class Project {
public:
    // The manifest's name, fixed. It sits at the top of the project folder,
    // beside assets/, config/, logs/, .build/ and .editor/.
    static constexpr const char* kFileName = "project.oproj";

    // Bumped only when the format changes in a way an older editor would get
    // wrong. A file claiming a HIGHER version still opens, with a warning
    // naming both numbers - the alternative is refusing to open a project
    // because it was last touched by a newer build, which is exactly the
    // behaviour this file is written to avoid.
    static constexpr int kFormatVersion = 1;

    // Is this folder a project? True when it holds a manifest OR an assets/
    // folder. See the header note - the `or` is the backward-compatibility
    // story, not a convenience.
    static bool Looks(std::string_view realDirectory);

    // Reads the manifest in `realDirectory`, or describes the folder as an
    // unnamed project when there is none.
    //
    // RETURNS TRUE FOR A FOLDER WITH NO MANIFEST, as long as Looks() accepts
    // it: out.hasManifest is false, out.name is the folder's own name, and
    // there is nothing wrong. It returns false only when the folder is not a
    // project at all, or cannot be read.
    //
    // Unparseable JSON is a WARNING and not a failure: the project still
    // opens under its folder name. A syntax error in a file whose only job is
    // to hold a display name should not stand between somebody and their
    // work.
    static bool Read(std::string_view realDirectory, ProjectInfo& out,
                     std::string& outError);

    // Writes `info` to `<info.realRoot>/project.oproj`, including whatever
    // unknown keys came back with it. Sets info.hasManifest on success.
    static bool Write(ProjectInfo& info, std::string& outError);

    // Walks up from `realStartDirectory` looking for a project, and returns
    // its root - or an empty string if there is none above it.
    //
    // This is what lets `--project <some folder inside a project>` do the
    // obvious thing instead of refusing. It is the same search the engine
    // does from the executable's folder when nobody has named a project,
    // which is still how the player and `editor --build-scripts` find theirs
    // - and how the launcher works out which project to offer on a first run.
    static std::string FindAbove(std::string_view realStartDirectory);

    // ---- making a new one -------------------------------------------------
    //
    // Creates `<realParentDirectory>/<name>` and the skeleton inside it: the
    // manifest, `assets/scenes`, `assets/scripts`, `assets/textures`, a
    // `config/engine.json` with the window titled after the project, and a
    // `.gitignore` naming the three folders that must never be committed.
    //
    // THE NEW PROJECT HAS NO SCENE, and that is deliberate rather than
    // unfinished. Shipping a starter scene would mean this file knowing the
    // scene format - a second place that has to be updated whenever the
    // format changes, and the first place to be forgotten. "No scene open" is
    // already a supported, tested state, and Scene -> New Scene is one menu
    // item away.
    //
    // Refuses, with a reason worth reading, when the name is unusable, the
    // destination already exists and is not empty, or the destination is
    // inside another project - that last one because a project inside a
    // project means two manifests above one assets/ folder and no way to say
    // which won.
    static bool Create(std::string_view realParentDirectory, std::string_view name,
                       ProjectInfo& out, std::string& outError);

    // Is `name` usable as a new project's folder name? Separated out so the
    // dialog can grey its own button out instead of letting somebody type a
    // name, press Create, and only then be told.
    static bool NameIsUsable(std::string_view name, std::string& outError);
};

} // namespace eng
