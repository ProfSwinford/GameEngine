// ============================================================================
//  Tests for eng::Project - the manifest, and the definition of "is this
//  folder a project".
//
//  WHY THIS IS WORTH TESTING
//  Most of what Project does is decide whether to accept something, and the
//  two answers fail in opposite directions. Accepting a folder that is not a
//  project points the whole file system - including ScriptBuild's remove_all
//  paths - at somewhere it should never write. REFUSING a folder that is one
//  makes somebody's existing project unopenable, which is worse.
//
//  The case that matters most is the one that looks like nothing: a folder
//  with an `assets` folder and NO manifest is a project. That is this
//  repository, it is every project made before manifests existed, and if it
//  ever stops being true then the player stops finding its own game, `editor
//  --build-scripts` stops finding anything to build, and these tests stop
//  finding the repository they run inside. There is a test for it below, and
//  it is the important one.
//
//  ==========================================================================
//  NO FILESYSTEM, NO ENGINE_ASSET_ROOT, NOTHING MOUNTED.
//
//  Unlike test_assets.cpp, these tests never touch eng::FileSystem. Project
//  deals in REAL paths precisely because it is what establishes the root -
//  see the long note in Project.h - so a test that went through the virtual
//  path system would be testing the opposite of the contract.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Json.h>
#include <engine/project/Project.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace eng;

namespace {

// ---------------------------------------------------------------------------
//  A temporary folder that deletes itself.
//
//  Named after the test so that a crashed run leaves something identifiable
//  behind, and cleared on the way IN as well as out for exactly that case.
// ---------------------------------------------------------------------------
struct TempDir {
    fs::path path;

    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / (std::string("engine-project-test-") + name);
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    std::string Str() const { return path.generic_string(); }

    // A folder inside this one, created.
    std::string Sub(const char* relative) const {
        std::error_code ec;
        fs::create_directories(path / relative, ec);
        return (path / relative).generic_string();
    }

    void WriteFile(const char* relative, std::string_view text) const {
        const fs::path  file = path / relative;
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    }

    std::string ReadFile(const char* relative) const {
        std::ifstream in(path / relative, std::ios::binary);
        std::ostringstream all;
        all << in.rdbuf();
        return all.str();
    }

    bool Has(const char* relative) const {
        std::error_code ec;
        return fs::exists(path / relative, ec);
    }
};

} // namespace

// ===========================================================================
//  Looks - which folders are projects
// ===========================================================================
TEST_CASE("a folder with a manifest is a project") {
    TempDir dir("looks-manifest");
    dir.WriteFile("project.oproj", "{}");

    CHECK(Project::Looks(dir.Str()));
}

TEST_CASE("a folder with only an assets folder is a project") {
    // THE BACKWARD-COMPATIBILITY CASE, and the most important test in this
    // file. This repository has no manifest; if this stops passing, every
    // program that finds a project by WALKING UP from its own folder stops
    // finding this one - the player, `editor --build-scripts`, and the test
    // sandboxes.
    //
    // It used to say "opening the editor with no arguments stops finding the
    // project". That is no longer one of the reasons: the editor opens a
    // project it was named and starts the launcher when it was given none.
    // The test still matters just as much, which is exactly why the reason
    // for it was worth correcting rather than leaving to rot.
    TempDir dir("looks-assets");
    dir.Sub("assets");

    CHECK(Project::Looks(dir.Str()));
}

TEST_CASE("a folder with neither a manifest nor assets is not a project") {
    TempDir dir("looks-neither");
    dir.Sub("something-else");

    CHECK_FALSE(Project::Looks(dir.Str()));
}

TEST_CASE("a path that does not exist is not a project") {
    TempDir dir("looks-missing");

    CHECK_FALSE(Project::Looks((dir.path / "no-such-folder").generic_string()));
    CHECK_FALSE(Project::Looks(""));
}

TEST_CASE("a FILE called project.oproj does not make its folder a project") {
    // is_regular_file, not exists: a DIRECTORY named project.oproj would
    // otherwise be accepted and then fail to parse on every open.
    TempDir dir("looks-directory-named-manifest");
    dir.Sub("project.oproj");

    CHECK_FALSE(Project::Looks(dir.Str()));
}

// ===========================================================================
//  Read
// ===========================================================================
TEST_CASE("reading a manifest gives the name it contains") {
    TempDir dir("read-name");
    dir.WriteFile("project.oproj",
                  R"({"formatVersion": 1, "name": "Orbit Test", "engineVersion": "0.1.0"})");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));

    CHECK(error.empty());
    CHECK(info.hasManifest);
    CHECK(info.name == "Orbit Test");
    CHECK(info.engineVersion == "0.1.0");
    CHECK(info.realRoot == dir.Str());
}

TEST_CASE("a project with no manifest reads as the folder's own name") {
    TempDir dir("read-no-manifest");
    dir.Sub("assets");

    ProjectInfo info;
    std::string error;

    // TRUE, not false. A missing manifest is not a failure - see Project.h.
    REQUIRE(Project::Read(dir.Str(), info, error));

    CHECK(error.empty());
    CHECK_FALSE(info.hasManifest);
    CHECK(info.name == "engine-project-test-read-no-manifest");
}

TEST_CASE("an empty or missing name falls back to the folder") {
    // A hand-written manifest containing nothing but braces has to work -
    // otherwise the simplest thing somebody might type is the one thing that
    // breaks.
    TempDir dir("read-empty-name");

    SUBCASE("no name key") { dir.WriteFile("project.oproj", "{}"); }
    SUBCASE("empty name") { dir.WriteFile("project.oproj", R"({"name": ""})"); }

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));

    CHECK(info.hasManifest);
    CHECK(info.name == "engine-project-test-read-empty-name");
}

TEST_CASE("a manifest that is not valid JSON still opens the project") {
    TempDir dir("read-broken-json");
    dir.WriteFile("project.oproj", "{ this is not json");

    ProjectInfo info;
    std::string error;

    // Opens, under the folder name, with a warning. A syntax error in a file
    // whose only job is to hold a display name must not stand between
    // somebody and their work.
    REQUIRE(Project::Read(dir.Str(), info, error));

    CHECK_FALSE(info.hasManifest);
    CHECK(info.name == "engine-project-test-read-broken-json");
}

TEST_CASE("a manifest from a newer editor still opens") {
    TempDir dir("read-newer-format");
    dir.WriteFile("project.oproj",
                  R"({"formatVersion": 99, "name": "From The Future"})");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));

    CHECK(info.hasManifest);
    CHECK(info.name == "From The Future");
}

TEST_CASE("reading a folder that is not a project fails and says why") {
    TempDir dir("read-not-a-project");

    ProjectInfo info;
    std::string error;
    CHECK_FALSE(Project::Read(dir.Str(), info, error));
    CHECK_FALSE(error.empty());
    CHECK(error.find("project.oproj") != std::string::npos);
    CHECK(error.find("assets") != std::string::npos);
}

TEST_CASE("addons are read as names, and non-strings are ignored") {
    TempDir dir("read-addons");
    dir.WriteFile("project.oproj",
                  R"({"name": "A", "addons": ["tilemap-extras", 7, "dialogue"]})");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));

    REQUIRE(info.addons.size() == 2);
    CHECK(info.addons[0] == "tilemap-extras");
    CHECK(info.addons[1] == "dialogue");
}

// ===========================================================================
//  Write, and keeping what we did not understand
// ===========================================================================
TEST_CASE("an unknown key survives a read and a write") {
    // Silently dropping a field written by a newer editor is how a shared
    // file becomes one nobody trusts.
    TempDir dir("write-unknown-keys");
    dir.WriteFile("project.oproj",
                  R"({"name": "Keeps Things", "somethingNew": {"nested": [1, 2]}})");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));
    REQUIRE(Project::Write(info, error));

    std::string parseError;
    const Json  written = ParseJson(dir.ReadFile("project.oproj"), parseError);
    REQUIRE(parseError.empty());

    CHECK(written["name"] == "Keeps Things");
    CHECK(written["somethingNew"]["nested"][1] == 2);
    CHECK(written["formatVersion"] == Project::kFormatVersion);
}

TEST_CASE("a known key is not overwritten by a stale unknown copy") {
    // Write merges the unknown keys in FIRST so that the real name wins. If
    // the order were the other way round, a manifest could end up renaming
    // itself back to an old value.
    TempDir dir("write-known-wins");
    dir.WriteFile("project.oproj", R"({"name": "Original"})");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Read(dir.Str(), info, error));

    info.unknown["name"] = "Stale";   // as if it had been carried by mistake
    info.name            = "Current";
    REQUIRE(Project::Write(info, error));

    std::string parseError;
    const Json  written = ParseJson(dir.ReadFile("project.oproj"), parseError);
    REQUIRE(parseError.empty());
    CHECK(written["name"] == "Current");
}

TEST_CASE("writing a manifest makes the folder a project") {
    TempDir dir("write-makes-project");
    REQUIRE_FALSE(Project::Looks(dir.Str()));

    ProjectInfo info;
    info.realRoot = dir.Str();
    info.name     = "Written";

    std::string error;
    REQUIRE(Project::Write(info, error));

    CHECK(info.hasManifest);
    CHECK(Project::Looks(dir.Str()));
}

TEST_CASE("writing with no folder fails rather than guessing one") {
    ProjectInfo info;
    info.name = "Nowhere";

    std::string error;
    CHECK_FALSE(Project::Write(info, error));
    CHECK_FALSE(error.empty());
}

// ===========================================================================
//  FindAbove
// ===========================================================================
TEST_CASE("the project is found from inside it") {
    TempDir dir("find-above");
    dir.Sub("assets/scenes");

    SUBCASE("from the root itself") {
        CHECK(Project::FindAbove(dir.Str()) == dir.Str());
    }
    SUBCASE("from a folder inside it") {
        CHECK(Project::FindAbove(dir.Sub("assets/scenes")) == dir.Str());
    }
}

TEST_CASE("a folder outside any project finds nothing") {
    TempDir dir("find-above-nothing");

    // The temp folder itself has no assets/ and no manifest. Its PARENTS are
    // the system temp folder and the drive root, which have neither either -
    // unless somebody has made the drive root a project, which would be a
    // stranger problem than this test.
    CHECK(Project::FindAbove(dir.Str()).empty());
    CHECK(Project::FindAbove("").empty());
}

// ===========================================================================
//  NameIsUsable
// ===========================================================================
TEST_CASE("a project name that would not survive becoming a folder is refused") {
    std::string error;

    CHECK(Project::NameIsUsable("Orbit Test", error));
    CHECK(Project::NameIsUsable("my-game_2", error));

    // Each of these is refused WITH A REASON - a dialog that greys out its
    // button and says nothing is its own kind of bug.
    for (const char* bad : {"", "a/b", "a\\b", ".", "..", "what?", "a:b", "ends.",
                            "ends "}) {
        error.clear();
        CHECK_FALSE(Project::NameIsUsable(bad, error));
        CHECK_FALSE(error.empty());
    }
}

// ===========================================================================
//  Create
// ===========================================================================
TEST_CASE("a created project is a project, with the skeleton inside it") {
    TempDir dir("create-skeleton");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Create(dir.Str(), "New Game", info, error));

    CHECK(error.empty());
    CHECK(info.name == "New Game");
    CHECK(info.hasManifest);
    CHECK(Project::Looks(info.realRoot));

    CHECK(dir.Has("New Game/project.oproj"));
    CHECK(dir.Has("New Game/assets/scenes"));
    CHECK(dir.Has("New Game/assets/scripts"));
    CHECK(dir.Has("New Game/assets/textures"));
    CHECK(dir.Has("New Game/config/engine.json"));
    CHECK(dir.Has("New Game/.gitignore"));
}

TEST_CASE("a created project's config is valid JSON titled after the project") {
    // The generated config is written as TEXT so its comments survive, which
    // means nothing checks its syntax on the way out. This is that check.
    TempDir dir("create-config");

    ProjectInfo info;
    std::string error;
    REQUIRE(Project::Create(dir.Str(), "Titled", info, error));

    std::string parseError;
    const Json  config = ParseJson(dir.ReadFile("Titled/config/engine.json"), parseError);
    REQUIRE(parseError.empty());

    CHECK(config["window"]["title"] == "Titled");
    CHECK(config["startup"]["scene"] == "");   // no starter scene, deliberately
}

TEST_CASE("creating into an existing empty folder is allowed") {
    // Picking the folder you just made in a file dialog is the obvious way to
    // do this, so it must not be refused.
    TempDir dir("create-into-empty");
    dir.Sub("Already There");

    ProjectInfo info;
    std::string error;
    CHECK(Project::Create(dir.Str(), "Already There", info, error));
}

TEST_CASE("creating is refused for a reason that is worth reading") {
    TempDir dir("create-refusals");

    ProjectInfo info;
    std::string error;

    SUBCASE("a name that cannot be a folder") {
        CHECK_FALSE(Project::Create(dir.Str(), "a/b", info, error));
    }
    SUBCASE("a destination that is not empty") {
        dir.WriteFile("Taken/something.txt", "hello");
        CHECK_FALSE(Project::Create(dir.Str(), "Taken", info, error));
    }
    SUBCASE("a destination that is a file") {
        dir.WriteFile("AFile", "hello");
        CHECK_FALSE(Project::Create(dir.Str(), "AFile", info, error));
    }
    SUBCASE("a parent that does not exist") {
        CHECK_FALSE(Project::Create((dir.path / "nope").generic_string(), "X", info,
                                    error));
    }
    SUBCASE("inside another project") {
        // Two manifests above one assets/ folder gives the upward walk two
        // answers and nothing to choose between them with.
        dir.Sub("Outer/assets");
        CHECK_FALSE(Project::Create((dir.path / "Outer").generic_string(), "Inner", info,
                                    error));
    }

    CHECK_FALSE(error.empty());
}

TEST_CASE("a half-made project does not look like a finished one") {
    // The manifest is written LAST, so a Create that failed on the way does
    // not leave something Looks() accepts... except through assets/, which it
    // creates first and which is the one thing that legitimately makes a
    // folder a project. This test pins what Create guarantees: either it
    // succeeded, or there is no manifest claiming it did.
    TempDir dir("create-half-made");
    dir.Sub("Outer/assets");

    ProjectInfo info;
    std::string error;
    REQUIRE_FALSE(Project::Create((dir.path / "Outer").generic_string(), "Inner", info,
                                  error));

    CHECK_FALSE(dir.Has("Outer/Inner/project.oproj"));
}
