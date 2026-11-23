// ============================================================================
//  test_projectsettings.cpp - the editor writing config/engine.json.
//
//  WHY THIS NEEDS TESTING AT ALL. This code rewrites a file the user edits by
//  hand: it holds every key binding in the project and a "_note" against most
//  sections explaining what the setting is for. A writer that regenerated the
//  file from a struct would silently delete both, and the symptom would be a
//  project whose controls had stopped working - blamed on anything but the
//  Game view's dropdown.
//
//  So almost every case here is about what SURVIVES, not about what changes.
// ============================================================================

#include <doctest/doctest.h>

#include <ProjectSettings.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/render/Presentation.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace eng;

namespace {

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

// A project with a settings file that looks like a real one: notes, an input
// section, and sections this writer knows nothing about.
struct SettingsSandbox {
    fs::path   root;
    FileSystem fileSystem;

    explicit SettingsSandbox(const char* name, bool writeConfig = true) {
        root = fs::temp_directory_path() / (std::string("engine-settings-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets", ec);
        fs::create_directories(root / "config", ec);

        if (writeConfig) {
            std::ofstream out(root / "config" / "engine.json", std::ios::binary);
            out << R"({
  "_comment": "the file a person edits",
  "window": {
    "_note": "what the window looks like",
    "title": "My Game",
    "width": 1280,
    "height": 720
  },
  "tunables": { "maxStepsPerFrame": 5 },
  "input": {
    "contexts": {
      "gameplay": { "Jump": ["Key.Space"], "Fire": ["Mouse.Left"] }
    }
  }
})";
        }

        SetEnv("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
    }

    ~SettingsSandbox() {
        fileSystem.Shutdown();
        SetEnv("ENGINE_ASSET_ROOT", "");
        Presentation::SetFree();
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    Json Read() const {
        std::ifstream     in(root / "config" / "engine.json", std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return Json::parse(buffer.str(), nullptr, false);
    }

    // What the file LOOKS like afterwards, for the layout cases below.
    //
    // LINE ENDINGS ARE TAKEN OUT, and that is not the thing being tested.
    // FileSystem reads and writes text files in text mode, so on Windows
    // every \n in the string handed to WriteTextFile is stored as \r\n and
    // turned back into \n on the way in. The conversion is symmetric and
    // applies to every file the engine writes; comparing it here would be
    // testing the C++ runtime, not this writer.
    std::string RawText() const {
        std::ifstream     in(root / "config" / "engine.json", std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();

        std::string text = buffer.str();
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }
};

} // namespace

TEST_CASE("setting the presentation writes the mode and keeps everything else") {
    SettingsSandbox box("merge");

    std::string error;
    REQUIRE_MESSAGE(editor::SetPresentationMode("16:9", error), error);

    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());

    CHECK(after["presentation"]["mode"].get<std::string>() == "16:9");

    // ======================================================================
    //  AND NOTHING ELSE MOVED. This is the point of the case: the input
    //  bindings are the whole of a project's controls and this writer knows
    //  nothing about them, so a regenerate-from-struct approach would delete
    //  them and the symptom would be a game whose keys had stopped working.
    // ======================================================================
    REQUIRE(after.contains("input"));
    CHECK(after["input"]["contexts"]["gameplay"]["Jump"][0].get<std::string>() ==
          "Key.Space");
    CHECK(after["window"]["title"].get<std::string>() == "My Game");
    CHECK(after["window"]["width"].get<int>() == 1280);
    CHECK(after["tunables"]["maxStepsPerFrame"].get<int>() == 5);

    // THE NOTES TOO, which are documentation for whoever opens the file.
    CHECK(after.contains("_comment"));
    CHECK(after["window"].contains("_note"));
}

TEST_CASE("a new presentation section arrives with its own note") {
    SettingsSandbox box("note");

    std::string error;
    REQUIRE(editor::SetPresentationMode("1920x1080", error));

    const Json after = box.Read();
    REQUIRE(after.contains("presentation"));

    // Every other section in that file explains itself to whoever opens it;
    // the one the editor adds should not be the bare exception.
    CHECK(after["presentation"].contains("_note"));
    CHECK(after["presentation"]["mode"].get<std::string>() == "1920x1080");
}

TEST_CASE("the mode is checked by the engine's own reader before it is written") {
    SettingsSandbox box("validate");

    // ======================================================================
    //  ONE PARSER DECIDES WHAT "16:9" MEANS. A value this accepted but the
    //  engine then refused at start-up would be a setting that looked applied
    //  in the editor and did nothing in the game - the exact class of bug
    //  this whole file exists to close.
    // ======================================================================
    std::string error;
    CHECK_FALSE(editor::SetPresentationMode("sideways", error));
    CHECK_FALSE(error.empty());

    // AND THE FILE WAS NOT TOUCHED. A refused value must not leave a
    // half-written settings file behind.
    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());
    CHECK_FALSE(after.contains("presentation"));
}

TEST_CASE("setting the window resizable writes it and keeps the rest") {
    SettingsSandbox box("resizable");

    std::string error;
    REQUIRE(editor::SetWindowResizable(false, error));

    const Json after = box.Read();
    CHECK(after["window"]["resizable"].get<bool>() == false);

    // The window section it joined is intact.
    CHECK(after["window"]["title"].get<std::string>() == "My Game");
    CHECK(after["window"]["height"].get<int>() == 720);
    CHECK(after["window"].contains("_note"));
    CHECK(after.contains("input"));

    REQUIRE(editor::SetWindowResizable(true, error));
    CHECK(box.Read()["window"]["resizable"].get<bool>() == true);
}

TEST_CASE("a project with no settings file yet gets one") {
    // Refusing to write a setting because the file is absent would mean a
    // project with no settings could never be given any.
    SettingsSandbox box("nofile", /*writeConfig=*/false);

    std::string error;
    REQUIRE_MESSAGE(editor::SetPresentationMode("4:3", error), error);

    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());
    CHECK(after["presentation"]["mode"].get<std::string>() == "4:3");
}

TEST_CASE("a settings file that will not parse is left alone and reported") {
    SettingsSandbox box("broken", /*writeConfig=*/false);

    {
        std::ofstream out(box.root / "config" / "engine.json", std::ios::binary);
        out << R"({"window": {"title": "half written",)";
    }

    // ======================================================================
    //  OVERWRITING IT WOULD THROW AWAY WHATEVER IS IN THERE - which, for
    //  this file, is every key binding in the project. A half-typed file is
    //  somebody's work in progress, not an invitation.
    // ======================================================================
    std::string error;
    CHECK_FALSE(editor::SetPresentationMode("16:9", error));
    CHECK_FALSE(error.empty());

    std::ifstream     in(box.root / "config" / "engine.json", std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    CHECK(buffer.str() == R"({"window": {"title": "half written",)");
}

TEST_CASE("writing a mode applies it to the engine running right now") {
    SettingsSandbox box("live");

    // The settings file is read once at start-up, so without this the Game
    // view would go on showing the old shape until the editor was restarted -
    // and a setting you cannot see take effect is a setting you do not trust.
    std::string error;
    REQUIRE(editor::SetPresentationMode("21:9", error));
    CHECK(Presentation::CurrentMode() == Presentation::Mode::Aspect);
    CHECK(Presentation::WantedX() == 21);
    CHECK(Presentation::WantedY() == 9);

    REQUIRE(editor::SetPresentationMode("free", error));
    CHECK(Presentation::IsFree());

    REQUIRE(editor::SetPresentationMode("800x600", error));
    CHECK(Presentation::CurrentMode() == Presentation::Mode::Resolution);
    CHECK(Presentation::WantedX() == 800);
}

TEST_CASE("the accessors report what the project asks for") {
    SettingsSandbox box("read");

    std::string error;
    REQUIRE(editor::SetPresentationMode("16:10", error));
    CHECK(editor::PresentationMode() == "16:10");

    REQUIRE(editor::SetWindowResizable(false, error));
    CHECK_FALSE(editor::WindowResizable());

    REQUIRE(editor::SetWindowResizable(true, error));
    CHECK(editor::WindowResizable());
}

// ============================================================================
//  WHAT THE FILE LOOKS LIKE AFTERWARDS.
//
//  These are the cases that stop this writer being rewritten as a
//  parse-change-dump, which is what it was first written as. `eng::Json` is
//  nlohmann::json, whose objects are std::map-backed and therefore SORTED, so
//  a dump reorders every section and every key binding in the project and
//  reflows every line. The diff is then four hundred moved lines with the one
//  changed value hidden inside it, and the person who opens it concludes the
//  editor corrupted their settings.
// ============================================================================

TEST_CASE("adding a key leaves every other byte exactly where it was") {
    SettingsSandbox box("bytes");

    const std::string before = box.RawText();

    std::string error;
    REQUIRE_MESSAGE(editor::SetWindowResizable(false, error), error);

    // The ONE change: a new line in the window section, indented to match the
    // members already there, after the last of them.
    const std::string expected = R"({
  "_comment": "the file a person edits",
  "window": {
    "_note": "what the window looks like",
    "title": "My Game",
    "width": 1280,
    "height": 720,
    "resizable": false
  },
  "tunables": { "maxStepsPerFrame": 5 },
  "input": {
    "contexts": {
      "gameplay": { "Jump": ["Key.Space"], "Fire": ["Mouse.Left"] }
    }
  }
})";

    CHECK(box.RawText() == expected);

    // And said the other way round, because the above is easy to "fix" by
    // pasting in whatever the code happens to produce: the file grew by one
    // line and nothing else moved.
    CHECK(box.RawText().size() > before.size());
    CHECK(box.RawText().find(R"(  "tunables": { "maxStepsPerFrame": 5 },)") !=
          std::string::npos);
}

TEST_CASE("changing a value that is already there rewrites only that value") {
    SettingsSandbox box("inplace");

    std::string error;
    REQUIRE(editor::SetWindowResizable(false, error));
    const std::string locked = box.RawText();

    REQUIRE(editor::SetWindowResizable(true, error));

    // Same file, one word different - not the same file re-dumped.
    std::string expected = locked;
    const size_t at      = expected.find("\"resizable\": false");
    REQUIRE(at != std::string::npos);
    expected.replace(at, std::string("\"resizable\": false").size(),
                     "\"resizable\": true");
    CHECK(box.RawText() == expected);
}

TEST_CASE("the sections and the keys keep the order the person put them in") {
    SettingsSandbox box("order");

    std::string error;
    REQUIRE(editor::SetPresentationMode("16:9", error));

    const std::string after = box.RawText();

    // ----------------------------------------------------------------------
    //  THE ALPHABETISATION GUARD. Sorted, this file's top level would read
    //  _comment, input, presentation, tunables, window - and the window
    //  section would read height, title, width. None of that may happen.
    // ----------------------------------------------------------------------
    const size_t comment      = after.find("\"_comment\"");
    const size_t window       = after.find("\"window\"");
    const size_t tunables     = after.find("\"tunables\"");
    const size_t input        = after.find("\"input\"");
    const size_t presentation = after.find("\"presentation\"");

    REQUIRE(presentation != std::string::npos);
    CHECK(comment < window);
    CHECK(window < tunables);
    CHECK(tunables < input);
    CHECK(input < presentation);   // the new section is appended, not sorted in

    CHECK(after.find("\"title\"") < after.find("\"width\""));
    CHECK(after.find("\"width\"") < after.find("\"height\""));

    // The hand-written one-liners are still one-liners, rather than exploded
    // one element per line the way dump(2) would leave them.
    CHECK(after.find(R"({ "Jump": ["Key.Space"], "Fire": ["Mouse.Left"] })") !=
          std::string::npos);
    CHECK(after.find(R"(  "tunables": { "maxStepsPerFrame": 5 },)") !=
          std::string::npos);

    // And the new section explains itself, like the ones around it.
    CHECK(after.find("\"_note\"") != std::string::npos);
    const Json parsed = box.Read();
    REQUIRE_FALSE(parsed.is_discarded());
    CHECK(parsed["presentation"]["mode"].get<std::string>() == "16:9");
    CHECK(parsed["presentation"].contains("_note"));
}

TEST_CASE("a file the editor cannot follow is still given the setting") {
    SettingsSandbox box("fallback", /*writeConfig=*/false);

    {
        // Valid JSON, but the name the setting needs is taken by something
        // that is not a section. There is nothing to splice into.
        std::ofstream out(box.root / "config" / "engine.json", std::ios::binary);
        out << R"({"presentation": 5, "input": {"contexts": {}}})";
    }

    // The setting still lands - the fallback rewrites the document rather
    // than refusing - and nothing is lost, only moved.
    std::string error;
    REQUIRE_MESSAGE(editor::SetPresentationMode("4:3", error), error);

    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());
    CHECK(after["presentation"]["mode"].get<std::string>() == "4:3");
    CHECK(after.contains("input"));
}

TEST_CASE("the real file's awkward parts do not throw the scanner off") {
    // ======================================================================
    //  THE SHAPES THIS HAS TO SURVIVE, all taken from the engine's own
    //  config/engine.json rather than invented:
    //
    //    - a "_note" whose text contains ESCAPED QUOTES - the real
    //      presentation note says \"free\" and \"1920x1080\" - and braces and
    //      colons, any of which a scanner that was really a search would trip
    //      over;
    //    - an inline array of numbers, "gravity": [0.0, -981.0];
    //    - a key whose NAME starts with the name of the key being set, so
    //      that matching a prefix instead of the whole name writes the wrong
    //      value into the wrong place;
    //    - blank lines between the sections, which is the thing that makes
    //      the file readable and the first thing a dump destroys.
    // ======================================================================
    SettingsSandbox box("awkward", /*writeConfig=*/false);

    const std::string original = R"({
  "window": {
    "_resizable": "false locks the game's window. Not the editor's: see {Game view}.",
    "title": "My Game",
    "width": 1280,
    "height": 720,
    "resizable": true
  },

  "presentation": {
    "_note": "\"free\" fills the window. A ratio like \"16:9\" pins the shape.",
    "_modeExamples": "free, 16:9, 1920x1080",
    "mode": "free"
  },

  "physics": {
    "gravity": [0.0, -981.0],
    "solverIterations": 4
  }
})";

    {
        std::ofstream out(box.root / "config" / "engine.json", std::ios::binary);
        out << original;
    }

    std::string error;
    REQUIRE_MESSAGE(editor::SetPresentationMode("1920x1080", error), error);
    REQUIRE_MESSAGE(editor::SetWindowResizable(false, error), error);

    std::string expected = original;
    expected.replace(expected.find("\"mode\": \"free\""),
                     std::string("\"mode\": \"free\"").size(),
                     "\"mode\": \"1920x1080\"");
    expected.replace(expected.find("\"resizable\": true"),
                     std::string("\"resizable\": true").size(),
                     "\"resizable\": false");

    // Two values changed, in place, and not one other byte - the blank lines
    // between the sections included.
    CHECK(box.RawText() == expected);

    // Said again through the parser, because the check above would also pass
    // if both replacements had somehow landed in the notes.
    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());
    CHECK(after["presentation"]["mode"].get<std::string>() == "1920x1080");
    CHECK(after["window"]["resizable"].get<bool>() == false);

    // THE PREFIX TRAP: "_modeExamples" begins with "mode" and must not have
    // been touched, and "_resizable" begins with nothing but must not have
    // been mistaken for "resizable".
    CHECK(after["presentation"]["_modeExamples"].get<std::string>() ==
          "free, 16:9, 1920x1080");
    CHECK(after["window"]["_resizable"].get<std::string>().find("{Game view}") !=
          std::string::npos);
}
