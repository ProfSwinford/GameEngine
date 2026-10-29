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
