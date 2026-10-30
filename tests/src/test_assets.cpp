// ============================================================================
//  Tests for FileSystem::Move and FileSystem::Import - the two operations the
//  Assets panel reorganises and imports with.
//
//  WHY THESE ARE WORTH TESTING AND THE OTHER FILE OPERATIONS ARE NOT
//  Read and Write either work or the file is not there. Move and Import are
//  different: most of what they do is REFUSE things, and a refusal that
//  quietly stops happening is invisible until somebody loses work. Moving a
//  folder into one of its own sub-folders leaves that branch of the project
//  unreachable; importing onto a name that is taken destroys whatever had it.
//  Neither would show up as a crash.
//
//  ==========================================================================
//  EVERY TEST RUNS IN A THROWAWAY PROJECT, not in this one.
//
//  FileSystem finds the project by walking up from the executable, which from
//  build/<preset>/bin lands on the real assets/ folder - so a test that
//  created files would be creating them in the actual game. ENGINE_ASSET_ROOT
//  overrides that search, which is what Sandbox below uses to point the whole
//  file system at a temporary folder for the duration of one test.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/fs/FileSystem.h>
#include <engine/project/Project.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace eng;

namespace {

void SetEnvironment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

// ---------------------------------------------------------------------------
//  A complete throwaway project: a temporary folder with an `assets` folder
//  inside it, with FileSystem pointed at it for as long as this object lives.
//
//  The destructor puts the environment variable back and deletes the lot, so
//  one test leaving a mess cannot make the next one fail for a reason that
//  has nothing to do with what it is testing.
// ---------------------------------------------------------------------------
struct Sandbox {
    fs::path    root;
    FileSystem  fileSystem;

    explicit Sandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-fs-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);           // left over from a crashed run
        fs::create_directories(root / "assets", ec);

        SetEnvironment("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
    }

    ~Sandbox() {
        fileSystem.Shutdown();
        SetEnvironment("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // Writes a file at a VIRTUAL path, creating the folders above it.
    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path real = fs::path(FileSystem::Resolve(virtualPath));
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }

    void MakeFolder(const std::string& virtualPath) const {
        std::error_code ec;
        fs::create_directories(fs::path(FileSystem::Resolve(virtualPath)), ec);
    }

    // A real path OUTSIDE the project, for the import tests. It lives in the
    // sandbox's root rather than inside assets/, which is the whole point -
    // importing is the act of bringing something in from elsewhere.
    fs::path Outside(const std::string& name) const { return root / name; }

    void WriteOutside(const std::string& name, const std::string& text) const {
        std::ofstream out(root / name, std::ios::binary);
        out << text;
    }

    std::string Read(const std::string& virtualPath) const {
        std::string text;
        std::string error;
        FileSystem::ReadTextFile(virtualPath, text, error);
        return text;
    }
};

} // namespace

// ===========================================================================
//  Move
// ===========================================================================

TEST_CASE("Move puts a file in another folder and takes it out of the old one") {
    const Sandbox box("move-file");
    box.Write("hero.png", "pixels");
    box.MakeFolder("textures");

    std::string error;
    REQUIRE(FileSystem::Move("hero.png", "textures", error));
    CHECK(error.empty());

    CHECK(FileSystem::Exists("textures/hero.png"));

    // BOTH HALVES. A "move" that copied would also pass the line above, and
    // would leave the project with two of everything.
    CHECK_FALSE(FileSystem::Exists("hero.png"));
    CHECK(box.Read("textures/hero.png") == "pixels");
}

TEST_CASE("Move takes a folder and everything inside it") {
    const Sandbox box("move-folder");
    box.Write("enemies/boss/Boss.cpp", "class Boss {};");
    box.MakeFolder("gameplay");

    std::string error;
    REQUIRE(FileSystem::Move("enemies/boss", "gameplay", error));

    CHECK(FileSystem::Exists("gameplay/boss/Boss.cpp"));
    CHECK_FALSE(FileSystem::Exists("enemies/boss"));
    CHECK(box.Read("gameplay/boss/Boss.cpp") == "class Boss {};");
}

TEST_CASE("the assets root is a legitimate destination") {
    // The root is the EMPTY virtual path, which looks like a missing argument
    // and is not one. Moving something back out to the top of the project has
    // to work, or a file can be dragged into a folder and never out again.
    const Sandbox box("move-to-root");
    box.Write("textures/hero.png", "pixels");

    std::string error;
    REQUIRE(FileSystem::Move("textures/hero.png", "", error));

    CHECK(FileSystem::Exists("hero.png"));
    CHECK_FALSE(FileSystem::Exists("textures/hero.png"));
}

TEST_CASE("a folder cannot be moved inside itself") {
    const Sandbox box("move-into-self");
    box.MakeFolder("art");

    std::string error;
    CHECK_FALSE(FileSystem::Move("art", "art", error));
    CHECK_FALSE(error.empty());
    CHECK(FileSystem::Exists("art"));
}

TEST_CASE("a folder cannot be moved into one of its own sub-folders") {
    // The one that would do real damage: std::filesystem::rename allows it
    // and the result is a branch of the project with no route to it.
    const Sandbox box("move-into-child");
    box.Write("art/ui/button.png", "pixels");

    std::string error;
    CHECK_FALSE(FileSystem::Move("art", "art/ui", error));
    CHECK(FileSystem::Exists("art/ui/button.png"));
}

TEST_CASE("a folder whose name merely starts the same is a different folder") {
    // The near miss that makes the check above worth writing carefully.
    // "art" is a prefix of "artwork" as text, and "artwork" is not inside
    // "art". A refusal based on the prefix alone would block a perfectly
    // ordinary move, and would look completely correct while doing it.
    const Sandbox box("move-prefix");
    box.MakeFolder("art");
    box.MakeFolder("artwork");

    std::string error;
    CHECK(FileSystem::Move("art", "artwork", error));
    CHECK(FileSystem::Exists("artwork/art"));
}

TEST_CASE("Move refuses a name that is already taken, and changes nothing") {
    const Sandbox box("move-collision");
    box.Write("hero.png", "the new one");
    box.Write("textures/hero.png", "the one already there");

    std::string error;
    CHECK_FALSE(FileSystem::Move("hero.png", "textures", error));
    CHECK_FALSE(error.empty());

    // BOTH FILES STILL WHERE THEY WERE, with their own contents. A refusal
    // that half-happened would be worse than either outcome.
    CHECK(box.Read("hero.png") == "the new one");
    CHECK(box.Read("textures/hero.png") == "the one already there");
}

TEST_CASE("Move refuses to move something into the folder it is already in") {
    const Sandbox box("move-nowhere");
    box.Write("textures/hero.png", "pixels");

    std::string error;
    CHECK_FALSE(FileSystem::Move("textures/hero.png", "textures", error));
    CHECK_FALSE(error.empty());
    CHECK(FileSystem::Exists("textures/hero.png"));
}

TEST_CASE("Move refuses the assets folder itself") {
    // An empty virtual path resolves to assets/, so a caller that passed one
    // through by mistake would move the whole project somewhere.
    const Sandbox box("move-root");
    box.MakeFolder("textures");

    std::string error;
    CHECK_FALSE(FileSystem::Move("", "textures", error));
    CHECK_FALSE(error.empty());
}

// ===========================================================================
//  Import
// ===========================================================================

TEST_CASE("Import copies a file in and leaves the original alone") {
    const Sandbox box("import-file");
    box.WriteOutside("hero.png", "pixels");
    box.MakeFolder("textures");

    std::string landed;
    std::string error;
    REQUIRE(FileSystem::Import(box.Outside("hero.png").string(), "textures", landed,
                               error));

    CHECK(landed == "textures/hero.png");
    CHECK(box.Read("textures/hero.png") == "pixels");

    // THE ORIGINAL IS STILL THERE. Importing is a copy, and a tool that
    // emptied a file out of somebody's Downloads folder because they dragged
    // it somewhere would be indefensible.
    CHECK(fs::exists(box.Outside("hero.png")));
}

TEST_CASE("Import makes the name unique rather than overwriting") {
    const Sandbox box("import-twice");
    box.WriteOutside("tiles.png", "the second one");
    box.Write("tiles.png", "the one already there");

    std::string landed;
    std::string error;
    REQUIRE(FileSystem::Import(box.Outside("tiles.png").string(), "", landed, error));

    CHECK(landed == "tiles (2).png");
    CHECK(box.Read("tiles (2).png") == "the second one");

    // Untouched, which is the point of renaming instead of overwriting.
    CHECK(box.Read("tiles.png") == "the one already there");
}

TEST_CASE("Import brings a folder's contents with it") {
    const Sandbox box("import-folder");
    std::error_code ec;
    fs::create_directories(box.Outside("pack") / "ui", ec);
    {
        std::ofstream out(box.Outside("pack") / "ui" / "button.png", std::ios::binary);
        out << "pixels";
    }

    std::string landed;
    std::string error;
    REQUIRE(FileSystem::Import(box.Outside("pack").string(), "", landed, error));

    CHECK(landed == "pack");
    CHECK(FileSystem::Exists("pack/ui/button.png"));
    CHECK(box.Read("pack/ui/button.png") == "pixels");
}

TEST_CASE("Import refuses something that is already in the project") {
    // Opening assets/ in a file explorer and dragging a file out of it back
    // onto the panel asks for a file to be copied on top of itself. What the
    // person meant was a move, which the panel can do directly - so this says
    // so rather than silently making a duplicate.
    const Sandbox box("import-self");
    box.Write("textures/hero.png", "pixels");
    box.MakeFolder("sprites");

    const std::string inside = FileSystem::Resolve("textures/hero.png");

    std::string landed;
    std::string error;
    CHECK_FALSE(FileSystem::Import(inside, "sprites", landed, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(FileSystem::Exists("sprites/hero.png"));
}

TEST_CASE("Import reports a source that is not there rather than inventing one") {
    const Sandbox box("import-missing");

    std::string landed;
    std::string error;
    CHECK_FALSE(FileSystem::Import(box.Outside("nothing.png").string(), "", landed,
                                   error));
    CHECK_FALSE(error.empty());
    CHECK(landed.empty());
}

// ===========================================================================
//  STAYING INSIDE THE PROJECT
//
//  Resolve finishes with lexically_normal(), which collapses ".." textually
//  and does NOT clamp at the root - so a virtual path containing ".." named a
//  real place outside the project, and the three operations that write would
//  carry it out.
//
//  This mattered less while the root was always guessed from the executable's
//  own location, somewhere inside this source tree. Now that a project can
//  live anywhere, the blast radius is wherever somebody keeps their work and
//  its parents. These tests are the fence.
// ===========================================================================
TEST_CASE("the operations that write refuse to leave the project") {
    const Sandbox box("escape-refused");
    box.Write("keep.txt", "please");

    // ---- WHERE THE FENCE ACTUALLY IS -------------------------------------
    //
    // The check is against the project ROOT, not against assets/, so ONE
    // ".." from a virtual path lands beside assets/ - in config/, .build/,
    // .editor/ - and is perfectly legitimate. Escaping takes a second one.
    //
    // Getting this wrong in a test is easy and quiet: `../x` resolves to
    // `<root>/x`, which is inside the project, so a test aiming there would
    // be asserting that a legal operation is refused.
    const fs::path victim = fs::temp_directory_path() / "engine-fs-escape-victim.txt";
    {
        std::ofstream out(victim);
        out << "do not delete me";
    }
    REQUIRE(fs::exists(victim));

    std::string error;

    SUBCASE("Remove") {
        std::size_t deleted = 99;
        CHECK_FALSE(
            FileSystem::Remove("../../engine-fs-escape-victim.txt", deleted, error));
        CHECK(deleted == 0);
    }
    SUBCASE("Rename") {
        CHECK_FALSE(FileSystem::Rename("../../engine-fs-escape-victim.txt", "taken.txt",
                                       error));
    }
    SUBCASE("Move, escaping by the source") {
        CHECK_FALSE(FileSystem::Move("../../engine-fs-escape-victim.txt", "", error));
    }
    SUBCASE("Move, escaping by the destination") {
        CHECK_FALSE(FileSystem::Move("keep.txt", "../..", error));
    }

    CHECK_FALSE(error.empty());

    // THE POINT OF THE WHOLE TEST: the file above the project is still there.
    CHECK(fs::exists(victim));

    std::error_code ec;
    fs::remove(victim, ec);
}

TEST_CASE("the folders beside assets are still inside the project") {
    // The containment check compares against the ROOT, not against assets/,
    // because config/, logs/, .build/ and .editor/ live beside it - and the
    // Assets panel legitimately deletes things in .build/. Checking against
    // assets/ would have refused all of these, which would look like the
    // feature working while quietly breaking the editor.
    const Sandbox box("beside-assets-allowed");
    box.Write("config/engine.json", "{}");
    box.Write(".build/sources.txt", "scripts/A.cpp");

    REQUIRE(FileSystem::Exists("config/engine.json"));
    REQUIRE(FileSystem::Exists(".build/sources.txt"));

    std::size_t deleted = 0;
    std::string error;

    CHECK(FileSystem::Remove("config/engine.json", deleted, error));
    CHECK(error.empty());
    CHECK(FileSystem::Remove(".build/sources.txt", deleted, error));
    CHECK(error.empty());
}

// ===========================================================================
//  NO PROJECT OPEN
//
//  Init used to leave the root pointing at the executable's own folder when
//  it found nothing, and the caller discarded the `false`. The result was a
//  file system that looked like it worked and was aimed at wherever the
//  editor happens to be installed - which is where ScriptBuild derives
//  .build/ from and deletes things inside.
// ===========================================================================
// ---------------------------------------------------------------------------
//  Puts UseRoot back on the way out.
//
//  UseRoot DELIBERATELY SURVIVES Shutdown - it is the process's intent rather
//  than the file system's state, which is what keeps the engine's double Init
//  consistent. That makes it the one piece of state in this file a test can
//  leak into every test that runs after it, including the ones in other
//  files, so it is restored by a destructor rather than by remembering.
// ---------------------------------------------------------------------------
struct NamedRoot {
    explicit NamedRoot(const std::string& realDirectory) {
        FileSystem::UseRoot(realDirectory);
    }
    ~NamedRoot() { FileSystem::UseRoot(""); }
};

TEST_CASE("a NAMED root that is not a project fails instead of searching") {
    // THE ASYMMETRY THAT MATTERS. A stale ENGINE_ASSET_ROOT in somebody's
    // shell warns and falls back to the upward search, which is kind. A
    // --project that names one particular folder must NOT quietly open a
    // different one - somebody who asked for D:/games/orbit and silently got
    // the engine's own sample project would start editing the wrong files.
    const fs::path empty = fs::temp_directory_path() / "engine-fs-test-no-project";
    std::error_code ec;
    fs::remove_all(empty, ec);
    fs::create_directories(empty, ec);

    const NamedRoot named(empty.string());
    FileSystem       fileSystem;

    CHECK_FALSE(fileSystem.Init(BootConfig{}));
    CHECK_FALSE(FileSystem::HasRoot());

    // Resolve says NOTHING rather than handing back a relative path, which
    // would resolve against the working directory - so every write and
    // delete would land there instead of failing.
    CHECK(FileSystem::Resolve("textures/x.png").empty());

    std::string error;
    CHECK_FALSE(FileSystem::WriteTextFile("x.txt", "no", error));
    CHECK_FALSE(error.empty());

    std::size_t deleted = 0;
    CHECK_FALSE(FileSystem::Remove("x.txt", deleted, error));

    fileSystem.Shutdown();
    fs::remove_all(empty, ec);
}

TEST_CASE("a stale ENGINE_ASSET_ROOT still falls back to searching") {
    // The other half of the asymmetry above, pinned so that making one of
    // them stricter does not quietly make the other one stricter too. This
    // one has to keep working: it is how the test sandboxes and anybody with
    // an old variable exported in their shell carry on.
    const fs::path empty = fs::temp_directory_path() / "engine-fs-test-stale-env";
    std::error_code ec;
    fs::remove_all(empty, ec);
    fs::create_directories(empty, ec);

    SetEnvironment("ENGINE_ASSET_ROOT", empty.string().c_str());
    FileSystem fileSystem;

    // Finds a project by walking up from the executable - which, running from
    // build/<preset>/bin, is this repository.
    CHECK(fileSystem.Init(BootConfig{}));
    CHECK(FileSystem::HasRoot());

    fileSystem.Shutdown();
    SetEnvironment("ENGINE_ASSET_ROOT", "");
    fs::remove_all(empty, ec);
}

TEST_CASE("the environment can name a project, and reports it without judging it") {
    // ======================================================================
    //  HALF OF WHAT THE EDITOR ASKS BEFORE OPENING THE LAUNCHER.
    //
    //  The editor opens a project it was NAMED and nothing else; given none
    //  it starts the launcher rather than searching upward for one. Setting
    //  this variable IS naming one, so it has to count - see
    //  editor/src/main.cpp.
    //
    //  REPORTED, NOT JUDGED, and the division of labour is the point. This
    //  hands back whatever the variable says and leaves the caller to decide
    //  whether it likes it, because the two callers want opposite things:
    //  Init is lenient and falls back to the upward search (the test above
    //  pins that, and the player and these sandboxes rely on it), while the
    //  editor checks the folder itself and opens the launcher when it is not
    //  a project. Teaching THIS function to validate would silently make
    //  Init strict, which would break the player.
    //
    //  ASKED WITH NO FILE SYSTEM RUNNING, deliberately. The editor asks
    //  before Init, which is why this is a plain question about the
    //  environment rather than a property of an open project.
    // ======================================================================
    SetEnvironment("ENGINE_ASSET_ROOT", "");
    CHECK(FileSystem::RootFromEnvironment().empty());

    // A FOLDER THAT DOES NOT EVEN EXIST comes back verbatim. If this ever
    // starts returning empty for a folder it dislikes, the editor stops being
    // able to tell "nobody named a project" from "somebody named a bad one" -
    // and those get different messages on purpose.
    SetEnvironment("ENGINE_ASSET_ROOT", "Z:/no/such/folder");
    CHECK(FileSystem::RootFromEnvironment() == "Z:/no/such/folder");
    CHECK_FALSE(Project::Looks(FileSystem::RootFromEnvironment()));

    SetEnvironment("ENGINE_ASSET_ROOT", "");
    CHECK(FileSystem::RootFromEnvironment().empty());
}

TEST_CASE("a folder is a project when it has a manifest but no assets folder") {
    // The other half of Project::Looks, reached through the file system: a
    // manifest alone is enough. Resolve still puts content under assets/,
    // which simply does not exist yet - that is a project somebody has just
    // created and not put anything in.
    const fs::path root = fs::temp_directory_path() / "engine-fs-test-manifest-only";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    {
        std::ofstream out(root / "project.oproj");
        out << R"({"name": "Manifest Only"})";
    }

    SetEnvironment("ENGINE_ASSET_ROOT", root.string().c_str());
    FileSystem fileSystem;

    CHECK(fileSystem.Init(BootConfig{}));
    CHECK(FileSystem::HasRoot());

    std::string error;
    CHECK(FileSystem::WriteTextFile("notes.txt", "hello", error));
    CHECK(FileSystem::Exists("notes.txt"));

    fileSystem.Shutdown();
    SetEnvironment("ENGINE_ASSET_ROOT", "");
    fs::remove_all(root, ec);
}
