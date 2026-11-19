// ============================================================================
//  test_assetrefs.cpp - following an asset when it is renamed or moved.
//
//  THIS CODE REWRITES THE USER'S PROJECT. Every scene, prefab and tile file
//  under the assets folder is opened and, if it mentions the asset that moved,
//  written back. The failure modes are therefore not "a wrong number on
//  screen" but "a file was corrupted" and "a reference was silently missed" -
//  the second of which looks exactly like the bug this feature exists to fix,
//  which is why the cases below check what is NOT rewritten as carefully as
//  what is.
// ============================================================================

#include <doctest/doctest.h>

#include <AssetRefs.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/scene/Scene.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace eng;

namespace {

// Windows' putenv-equivalent, matching what the other suites use.
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value == nullptr || value[0] == '\0') {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

struct RefSandbox {
    fs::path   root;
    FileSystem fileSystem;

    explicit RefSandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-refs-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets", ec);

        SetEnv("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
    }

    ~RefSandbox() {
        fileSystem.Shutdown();
        SetEnv("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path  real = fs::path(FileSystem::Resolve(virtualPath));
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }

    std::string Read(const std::string& virtualPath) const {
        std::ifstream     in(fs::path(FileSystem::Resolve(virtualPath)), std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
};

} // namespace

TEST_CASE("a renamed tile is followed into every palette that named it") {
    RefSandbox box("palette");

    // THE REPORTED CASE. A tilemap's palette is a list of .otile paths, and a
    // palette entry that will not load does not fail loudly: the cells are
    // numbers, so the map keeps them and simply draws nothing where that tile
    // was. The rename is what broke it, and nothing says so.
    box.Write("tiles/overworld_tree.otile",
              R"({"tileSize":32,"collision":"Grid"})");
    box.Write("scenes/level.oscene",
              R"({"entities":[{"name":"Level","components":[)"
              R"({"type":"TilemapComponent","palette":["tiles/overworld_tree.otile",)"
              R"("tiles/grass.otile"]}]}]})");

    const editor::RefRewrite result = editor::RewriteAssetReferences(
        "tiles/overworld_tree.otile", "tiles/tree.otile");

    CHECK(result.files == 1);
    CHECK(result.references == 1);

    const std::string after = box.Read("scenes/level.oscene");
    CHECK(after.find("tiles/tree.otile") != std::string::npos);
    CHECK(after.find("overworld_tree") == std::string::npos);

    // The entry beside it is untouched - a rewrite that took the whole list
    // with it would be worse than the bug.
    CHECK(after.find("tiles/grass.otile") != std::string::npos);
}

TEST_CASE("a path that merely starts the same way is left alone") {
    RefSandbox box("prefix");

    // "tiles2/..." is NOT inside "tiles", and "grass_dark.otile" is not
    // "grass.otile". A prefix test without this is the classic way for a
    // rename to quietly damage files it was never asked about.
    box.Write("scenes/a.oscene",
              R"({"paths":["tiles/grass.otile","tiles/grass_dark.otile",)"
              R"("tiles2/grass.otile","tilesmore/x.otile"]})");

    const editor::RefRewrite result =
        editor::RewriteAssetReferences("tiles/grass.otile", "tiles/meadow.otile");

    CHECK(result.references == 1);

    const std::string after = box.Read("scenes/a.oscene");
    CHECK(after.find("tiles/meadow.otile") != std::string::npos);
    CHECK(after.find("tiles/grass_dark.otile") != std::string::npos);
    CHECK(after.find("tiles2/grass.otile") != std::string::npos);
    CHECK(after.find("tilesmore/x.otile") != std::string::npos);
}

TEST_CASE("a moved folder takes everything inside it along") {
    RefSandbox box("folder");

    box.Write("scenes/a.oscene",
              R"({"paths":["tiles/grass.otile","tiles/deep/stone.otile",)"
              R"("tiles","other/x.otile"]})");

    const editor::RefRewrite result = editor::RewriteAssetReferences("tiles", "world/tiles");

    // Three: the folder itself, and the two files under it.
    CHECK(result.references == 3);

    const std::string after = box.Read("scenes/a.oscene");
    CHECK(after.find("world/tiles/grass.otile") != std::string::npos);
    CHECK(after.find("world/tiles/deep/stone.otile") != std::string::npos);
    CHECK(after.find("other/x.otile") != std::string::npos);
}

TEST_CASE("a frame of a sliced sheet follows its sheet") {
    RefSandbox box("subasset");

    // A frame is written "sheet.png#name" and is not a file of its own, so
    // renaming the sheet has to carry every frame reference with it.
    box.Write("scenes/a.oscene",
              R"({"paths":["textures/hero.png","textures/hero.png#walk_1",)"
              R"("textures/hero.png#idle"]})");

    const editor::RefRewrite result =
        editor::RewriteAssetReferences("textures/hero.png", "art/player.png");

    CHECK(result.references == 3);

    const std::string after = box.Read("scenes/a.oscene");
    CHECK(after.find("art/player.png#walk_1") != std::string::npos);
    CHECK(after.find("art/player.png#idle") != std::string::npos);
    CHECK(after.find("textures/hero") == std::string::npos);
}

TEST_CASE("every kind of asset file is searched, and nothing else is") {
    RefSandbox box("kinds");

    const std::string body = R"({"art":"textures/hero.png"})";
    box.Write("scenes/a.oscene", body);
    box.Write("prefabs/a.oprefab", body);
    box.Write("tiles/a.otile", body);
    box.Write("input/a.oinput", body);
    box.Write("scenes/build.json", body);

    // NOT searched: a script's source is not JSON, and parsing a .png as JSON
    // once per rename would make renaming feel broken.
    box.Write("scripts/Thing.cpp", "// textures/hero.png\n");
    box.Write("textures/hero.png", "textures/hero.png not really a png");

    const editor::RefRewrite result =
        editor::RewriteAssetReferences("textures/hero.png", "art/hero.png");

    CHECK(result.files == 5);
    CHECK(result.references == 5);

    CHECK(box.Read("scripts/Thing.cpp").find("textures/hero.png") !=
          std::string::npos);
    CHECK(box.Read("textures/hero.png").find("textures/hero.png") !=
          std::string::npos);
}

TEST_CASE("a file that is not valid JSON is skipped, not destroyed") {
    RefSandbox box("broken");

    // Half-written, or hand-edited badly. A rename is not the moment to start
    // refusing to proceed over somebody else's broken file - but it must not
    // be the moment that file gets truncated either.
    const std::string broken = R"({"art":"textures/hero.png",)";
    box.Write("scenes/broken.oscene", broken);
    box.Write("scenes/good.oscene", R"({"art":"textures/hero.png"})");

    const editor::RefRewrite result =
        editor::RewriteAssetReferences("textures/hero.png", "art/hero.png");

    CHECK(result.files == 1);
    CHECK(box.Read("scenes/broken.oscene") == broken);
}

TEST_CASE("a file that does not mention the asset is not rewritten at all") {
    RefSandbox box("untouched");

    // Not merely "left correct" - left BYTE FOR BYTE. Every file in the
    // project is opened by this, and re-dumping them all through a JSON
    // printer would reformat the lot and turn one rename into a diff across
    // the whole repository.
    const std::string original =
        "{\n    \"art\":   \"textures/other.png\",\n\n    \"note\": \"spaced out\"\n}";
    box.Write("scenes/a.oscene", original);

    const editor::RefRewrite result =
        editor::RewriteAssetReferences("textures/hero.png", "art/hero.png");

    CHECK(result.files == 0);
    CHECK(result.references == 0);
    CHECK(box.Read("scenes/a.oscene") == original);
}

TEST_CASE("renaming to the same name, or from nothing, does nothing") {
    RefSandbox box("noop");
    box.Write("scenes/a.oscene", R"({"art":"textures/hero.png"})");

    CHECK(editor::RewriteAssetReferences("textures/hero.png", "textures/hero.png")
              .references == 0);
    CHECK(editor::RewriteAssetReferences("", "art/hero.png").references == 0);
    CHECK(editor::RewriteAssetReferences("textures/hero.png", "").references == 0);
}

TEST_CASE("the result describes itself in words") {
    editor::RefRewrite none;
    CHECK(editor::DescribeRewrite(none) == "nothing referred to it");

    editor::RefRewrite one;
    one.files      = 1;
    one.references = 1;
    CHECK(editor::DescribeRewrite(one) == "1 reference in 1 file updated");

    editor::RefRewrite many;
    many.files      = 3;
    many.references = 12;
    many.openScene  = true;
    CHECK(editor::DescribeRewrite(many) ==
          "12 references in 3 files, and the open scene updated");
}

TEST_CASE("renaming the open scene re-points the scene at its new file") {
    RefSandbox box("ownfile");
    box.Write("scenes/Town.oscene", R"({"entities":[]})");

    Scene scene;
    Scene::SetActive(&scene);

    std::string error;
    REQUIRE(scene.Load("scenes/Town.oscene", error));
    REQUIRE(scene.SourcePath() == "scenes/Town.oscene");

    // WITHOUT THIS, the next Ctrl+S writes the level straight back under the
    // old name: two copies of the scene, and a rename that appears to have
    // undone itself a minute later.
    editor::RewriteAssetReferences("scenes/Town.oscene", "scenes/Village.oscene");
    CHECK(scene.SourcePath() == "scenes/Village.oscene");

    Scene::SetActive(nullptr);
}
