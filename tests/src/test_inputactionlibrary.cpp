// ============================================================================
//  Tests for eng::InputActionLibrary - the .oinput cache.
//
//  SEPARATE FROM test_inputactionasset.cpp ON PURPOSE. Everything about the
//  FORMAT is pure - Deserialize takes a parsed Json - and is tested there
//  with no filesystem at all. Everything here needs files on disk, so it runs
//  in a throwaway project folder with ENGINE_ASSET_ROOT pointed at it, the
//  same trick test_tilemap.cpp and test_assets.cpp use. Keeping the two apart
//  means the format's hundred cases do not each pay for a temp directory.
//
//  WHAT IS WORTH TESTING HERE IS POLICY, not parsing: which file states get
//  cached, what Reload does to a live asset somebody is pointing at, and
//  whether fixing a broken file is enough on its own. Each of those could
//  reasonably have gone the other way, and each is the kind of decision that
//  is otherwise only noticed months later from the wrong end.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputActionAsset.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

void SetEnvironmentVariable(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

struct InputSandbox {
    fs::path        root;
    eng::FileSystem fileSystem;

    explicit InputSandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-input-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "input", ec);

        SetEnvironmentVariable("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(eng::BootConfig{});
        eng::InputActionLibrary::Clear();
    }

    ~InputSandbox() {
        eng::InputActionLibrary::Clear();
        fileSystem.Shutdown();
        SetEnvironmentVariable("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path  real{eng::FileSystem::Resolve(virtualPath)};
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }
};

constexpr const char* kOneMap =
    R"({ "maps": [ { "name": "Gameplay",
                     "actions": [ { "name": "Jump", "bindings": ["Key.Space"] } ] } ] })";

} // namespace

TEST_CASE("an asset loads once and is handed back by pointer") {
    InputSandbox box("cache");
    box.Write("input/player.oinput", kOneMap);

    std::string                  error;
    const eng::InputActionAsset* first =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    REQUIRE(first != nullptr);
    CHECK(error.empty());
    CHECK(eng::InputActionLibrary::Count() == 1);

    // THE SAME POINTER, which is what lets a component hold the asset and
    // compare identity by it rather than by re-reading a path.
    const eng::InputActionAsset* second =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    CHECK(second == first);
    CHECK(eng::InputActionLibrary::Count() == 1);

    CHECK(eng::InputActionLibrary::Find("input/player.oinput") == first);
    CHECK(eng::InputActionLibrary::Find("input/nothing.oinput") == nullptr);

    // The library stamps the path on, so an asset knows where it came from
    // without every holder having to carry that separately.
    CHECK(first->Path() == "input/player.oinput");
}

TEST_CASE("a broken file is not cached, so fixing it is enough") {
    InputSandbox box("notcached");
    box.Write("input/player.oinput", R"({ "maps": [] })");

    std::string error;
    CHECK(eng::InputActionLibrary::Load("input/player.oinput", error) == nullptr);
    CHECK_FALSE(error.empty());

    // The message names the file. The Inspector and the Asset Browser both
    // put this string on screen beside the field that is wrong, and have
    // nothing else to go on.
    CHECK(error.find("player.oinput") != std::string::npos);
    CHECK(eng::InputActionLibrary::Count() == 0);

    // NOTHING IS RESTARTED. A file that was broken and has been fixed has to
    // start working on the next look - which is the whole of how editing one
    // of these by hand is meant to feel.
    box.Write("input/player.oinput", kOneMap);
    error.clear();
    const eng::InputActionAsset* asset =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    REQUIRE(asset != nullptr);
    CHECK(asset->FindMap("Gameplay") != nullptr);
}

TEST_CASE("a file that is not there is reported rather than crashed on") {
    InputSandbox box("missing");

    std::string error;
    CHECK(eng::InputActionLibrary::Load("input/ghost.oinput", error) == nullptr);
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK(eng::InputActionLibrary::Load("", error) == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("Reload changes the asset in place, so holders see it") {
    InputSandbox box("reload");
    box.Write("input/player.oinput", kOneMap);

    std::string                  error;
    const eng::InputActionAsset* asset =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    REQUIRE(asset != nullptr);
    CHECK(asset->Maps().size() == 1);

    box.Write("input/player.oinput", R"({ "maps": [
        { "name": "Gameplay", "actions": [ { "name": "Jump", "bindings": ["Key.Space"] } ] },
        { "name": "UI",       "actions": [ { "name": "Back", "bindings": ["Key.Escape"] } ] } ] })");

    REQUIRE(eng::InputActionLibrary::Reload("input/player.oinput", error));

    // THE SAME OBJECT, WITH NEW CONTENTS. Nothing has to be re-dropped in the
    // Inspector and no component has to be told. This is also exactly why a
    // caller must not keep a pointer INSIDE the asset across a reload - the
    // map vector was re-filled underneath it.
    CHECK(eng::InputActionLibrary::Find("input/player.oinput") == asset);
    CHECK(asset->Maps().size() == 2);
    CHECK(asset->FindMap("UI") != nullptr);
}

TEST_CASE("Reload refuses a broken file and leaves the live asset working") {
    InputSandbox box("probe");
    box.Write("input/player.oinput", kOneMap);

    std::string                  error;
    const eng::InputActionAsset* asset =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    REQUIRE(asset != nullptr);

    // WHILE ONE OF THESE IS BEING TYPED INTO, "has an error in it" is its
    // usual state - and the live asset is what the running scene points at.
    // Deserialize gives up part-way through, so applying a half-written file
    // would leave the game holding half a control scheme. It is parsed into a
    // scratch asset first for exactly this.
    box.Write("input/player.oinput", R"({ "maps": "not an array" })");
    error.clear();
    CHECK_FALSE(eng::InputActionLibrary::Reload("input/player.oinput", error));
    CHECK_FALSE(error.empty());

    // Untouched.
    CHECK(asset->Maps().size() == 1);
    REQUIRE(asset->FindMap("Gameplay") != nullptr);
    CHECK(asset->FindMap("Gameplay")->Find("Jump") != nullptr);

    // And reloading something never loaded says so rather than quietly
    // loading it, because the caller asked a different question.
    error.clear();
    CHECK_FALSE(eng::InputActionLibrary::Reload("input/other.oinput", error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Forget drops one and Clear drops all") {
    InputSandbox box("forget");
    box.Write("input/a.oinput", kOneMap);
    box.Write("input/b.oinput", kOneMap);

    std::string error;
    REQUIRE(eng::InputActionLibrary::Load("input/a.oinput", error) != nullptr);
    REQUIRE(eng::InputActionLibrary::Load("input/b.oinput", error) != nullptr);
    CHECK(eng::InputActionLibrary::Count() == 2);

    // A stable order, so a list of these in the editor does not reshuffle
    // itself between runs.
    const std::vector<const eng::InputActionAsset*> all = eng::InputActionLibrary::All();
    REQUIRE(all.size() == 2);
    CHECK(all[0]->Path() == "input/a.oinput");
    CHECK(all[1]->Path() == "input/b.oinput");

    eng::InputActionLibrary::Forget("input/a.oinput");
    CHECK(eng::InputActionLibrary::Count() == 1);
    CHECK(eng::InputActionLibrary::Find("input/a.oinput") == nullptr);
    CHECK(eng::InputActionLibrary::Find("input/b.oinput") != nullptr);

    eng::InputActionLibrary::Forget("input/nothing.oinput");   // not an error
    CHECK(eng::InputActionLibrary::Count() == 1);

    eng::InputActionLibrary::Clear();
    CHECK(eng::InputActionLibrary::Count() == 0);
}

TEST_CASE("the sample asset this repository ships is correct") {
    // NO SANDBOX, DELIBERATELY. With ENGINE_ASSET_ROOT unset the file system
    // searches UPWARD from the executable for an assets folder, which is how
    // the editor and the player find a project - and from build/<cfg>/bin
    // that search lands on this repository. So this is the shipped file,
    // found the shipped way. test_project.cpp leans on the same thing.
    //
    // AND IT CHECKS FOR WARNINGS, which is the whole point. A misspelled key
    // in assets/input/player.oinput costs that binding and nothing else - the
    // file still loads, the editor still runs, and the only trace is one line
    // in a console nobody is reading when it scrolls past. That is exactly
    // the failure a shipped sample must not be allowed to have.
    SetEnvironmentVariable("ENGINE_ASSET_ROOT", "");
    eng::FileSystem fileSystem;
    fileSystem.Init(eng::BootConfig{});

    std::string text, readError;
    REQUIRE(eng::FileSystem::ReadTextFile("input/player.oinput", text, readError));

    std::string     parseError;
    const eng::Json node = eng::ParseJson(text, parseError);
    REQUIRE(parseError.empty());

    eng::InputActionAsset asset;
    std::string           error, warnings;
    REQUIRE(asset.Deserialize(node, error, warnings));
    CHECK(error.empty());
    CHECK(warnings == "");          // spelled out, so a failure prints the text

    // The two maps it documents, and the overlap that is the reason it holds
    // both: Escape means Pause in one and Cancel in the other.
    REQUIRE(asset.Maps().size() == 2);
    REQUIRE(asset.DefaultMap() != nullptr);
    CHECK(asset.DefaultMap()->Name() == "Gameplay");

    const eng::InputAction* pause  = asset.FindMap("Gameplay")->Find("Pause");
    const eng::InputAction* cancel = asset.FindMap("Menu")->Find("Cancel");
    REQUIRE(pause != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK(pause->bindings[0].control == cancel->bindings[0].control);

    // Move really is the composite it claims to be, with both key sets bound.
    const eng::InputAction* move = asset.FindMap("Gameplay")->Find("Move");
    REQUIRE(move != nullptr);
    CHECK(move->controlType == eng::ControlType::Vector2);
    REQUIRE(move->bindings.size() == 2);
    for (const eng::InputBinding& binding : move->bindings) {
        CHECK(binding.shape == eng::InputBinding::Shape::Vector2Composite);
        CHECK(binding.up);
        CHECK(binding.down);
        CHECK(binding.left);
        CHECK(binding.right);
    }

    fileSystem.Shutdown();
}

TEST_CASE("warnings do not stop a file loading") {
    InputSandbox box("warn");

    // One good binding, one typo. The file is usable and the typo is said out
    // loud - the two-tier rule, seen from the library's side.
    box.Write("input/player.oinput", R"({ "maps": [ { "name": "Gameplay", "actions": [
        { "name": "Jump", "bindings": ["Key.Space", "Key.Spacebar"] } ] } ] })");

    std::string                  error;
    const eng::InputActionAsset* asset =
        eng::InputActionLibrary::Load("input/player.oinput", error);
    REQUIRE(asset != nullptr);
    CHECK(error.empty());
    CHECK(asset->FindMap("Gameplay")->Find("Jump")->bindings.size() == 1);
}
