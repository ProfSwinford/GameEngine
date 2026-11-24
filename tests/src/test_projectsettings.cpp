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

#include <engine/Engine.h>
#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/core/WorldUnit.h>
#include <engine/fs/FileSystem.h>
#include <engine/physics/Physics2D.h>
#include <engine/render/Presentation.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
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

    // ======================================================================
    //  AND IT PUTS THE ENGINE BACK.
    //
    //  Writing a setting now APPLIES it - that is the whole point of the
    //  feature - and the things it applies to are globals: the world unit
    //  every collider is measured against, the physics gravity, the log
    //  threshold. A case that left one of those changed would be a case that
    //  broke a test file it has never heard of, and the failure would look
    //  like a bug in that file.
    // ======================================================================
    ~SettingsSandbox() {
        fileSystem.Shutdown();
        SetEnv("ENGINE_ASSET_ROOT", "");
        Presentation::SetFree();
        WorldUnit::SetPixels(worldUnitWas);
        Physics2D::SetGravity(gravityWas);
        Log::SetThreshold(thresholdWas);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    const float    worldUnitWas = WorldUnit::Pixels();
    const Vec2     gravityWas   = Physics2D::Gravity();
    const LogLevel thresholdWas = Log::GetThreshold();

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

// ============================================================================
//  THE TYPED WRITERS, AND WHAT THEY PUT IN THE FILE.
//
//  A settings panel writes numbers, and a number is where this gets
//  interesting: nlohmann holds every number as a double, so a float arrives
//  widened and -9.81f would be written as -9.8100004196166992. A settings
//  file full of those is one nobody wants to open, and a gravity that reads
//  back differently from what was typed is a setting that looks broken.
// ============================================================================

TEST_CASE("a float is written as the shortest text that reads back the same") {
    SettingsSandbox box("numbers");

    std::string error;
    REQUIRE_MESSAGE(editor::SetNumber("physics", "drag", -9.81f, error), error);

    const std::string after = box.RawText();
    CHECK(after.find("\"drag\": -9.81") != std::string::npos);

    // Said the other way round, because that is the failure this guards: not
    // one digit of double's widening may appear.
    CHECK(after.find("-9.8100004") == std::string::npos);

    // And it still IS the float that was asked for.
    CHECK(box.Read()["physics"]["drag"].get<float>() == doctest::Approx(-9.81f));
}

TEST_CASE("a whole number keeps its decimal point, so the file still reads as a measurement") {
    SettingsSandbox box("whole");

    std::string error;
    REQUIRE(editor::SetNumber("units", "pixelsPerUnit", 32.0f, error));
    CHECK(box.RawText().find("\"pixelsPerUnit\": 32.0") != std::string::npos);

    // An INT is a different request and keeps no point - window.width is a
    // count of pixels, not a measurement of one.
    REQUIRE(editor::SetInt("window", "width", 1920, error));
    CHECK(box.RawText().find("\"width\": 1920,") != std::string::npos);
}

TEST_CASE("a pair of numbers is written on one line, the way a person writes one") {
    SettingsSandbox box("pair");

    const float gravity[2] = {0.0f, -981.0f};
    std::string error;
    REQUIRE_MESSAGE(editor::SetNumbers("physics", "gravity", gravity, 2, error), error);

    // nlohmann's own dump would put each number on a line of its own, which
    // is not how any hand-written config in this repo spells a coordinate.
    CHECK(box.RawText().find("\"gravity\": [0.0, -981.0]") != std::string::npos);

    const Json after = box.Read();
    REQUIRE(after["physics"]["gravity"].is_array());
    CHECK(after["physics"]["gravity"][1].get<float>() == doctest::Approx(-981.0f));
}

TEST_CASE("a new section arrives with the note that belongs to it") {
    // The note is looked up from the section's NAME rather than passed in by
    // the caller, so that two places setting two keys of one section cannot
    // write two different explanations of it.
    SettingsSandbox box("notes");

    std::string error;
    REQUIRE(editor::SetNumber("units", "pixelsPerUnit", 16.0f, error));
    REQUIRE(editor::SetInt("physics", "solverIterations", 7, error));

    const Json after = box.Read();
    REQUIRE(after["units"].contains("_note"));
    REQUIRE(after["physics"].contains("_note"));
    CHECK(after["units"]["_note"].get<std::string>() !=
          after["physics"]["_note"].get<std::string>());

    // AND A SECTION THAT WAS ALREADY THERE IS NOT GIVEN ONE. The fixture's
    // "tunables" has no note and did not ask for one; a writer that added
    // explanations to sections somebody else wrote would be editing their
    // file rather than setting a value in it.
    REQUIRE(editor::SetInt("tunables", "maxStepsPerFrame", 7, error));
    CHECK_FALSE(box.Read()["tunables"].contains("_note"));

    // A SECOND KEY IN A SECTION THAT ALREADY EXISTS does not get a second
    // note, and does not disturb the first.
    const std::string noteBefore = after["units"]["_note"].get<std::string>();
    REQUIRE(editor::SetNumber("units", "somethingElse", 2.5f, error));
    CHECK(box.Read()["units"]["_note"].get<std::string>() == noteBefore);
}

TEST_CASE("a string is escaped by the same parser that will read it back") {
    SettingsSandbox box("escapes");

    std::string error;
    REQUIRE(editor::SetString("window", "title", "He said \"hi\"\\there", error));

    // The point is the round trip, not the spelling: whatever it wrote, the
    // file still parses and still says what was asked for.
    const Json after = box.Read();
    REQUIRE_FALSE(after.is_discarded());
    CHECK(after["window"]["title"].get<std::string>() == "He said \"hi\"\\there");
    CHECK(after.contains("input"));
}

TEST_CASE("asking whether a setting was actually written down") {
    // ======================================================================
    //  ABSENT AND DEFAULT ARE INDISTINGUISHABLE FROM BootConfig ALONE: a
    //  panel shows 1280 whether window.width is in the file or not. This is
    //  the only way to tell, and it reads the file rather than the engine
    //  because the engine is exactly the thing that cannot tell.
    // ======================================================================
    SettingsSandbox box("presence");

    CHECK(editor::HasSetting("window", "width"));        // the sandbox file has it
    CHECK_FALSE(editor::HasSetting("window", "resizable"));
    CHECK_FALSE(editor::HasSetting("physics", "gravity"));
    CHECK_FALSE(editor::HasSetting("nosuchsection", "nosuchkey"));

    std::string error;
    REQUIRE(editor::SetBool("window", "resizable", false, error));
    CHECK(editor::HasSetting("window", "resizable"));
}

TEST_CASE("a value the file cannot hold is refused before anything is written") {
    SettingsSandbox box("badvalue");

    const std::string before = box.RawText();

    // Presentation is the one key checked by the ENGINE's own parser, so a
    // value the editor accepted can never be one the game refuses.
    std::string error;
    CHECK_FALSE(editor::SetPresentationMode("sideways", error));
    CHECK_FALSE(error.empty());
    CHECK(box.RawText() == before);
}

// ============================================================================
//  AND THE CHANGE IS LIVE.
//
//  The point of editing settings in an editor rather than in a text editor is
//  that you see what you changed. Every writer above ends by asking the
//  engine to re-read the file, which applies whatever can be applied - so
//  these cases are about the SUBSYSTEM, not about the file.
// ============================================================================

TEST_CASE("writing a setting changes the running engine, not just the file") {
    SettingsSandbox box("live-apply");

    std::string error;

    // ---- the world unit, which every collider is measured against --------
    REQUIRE_MESSAGE(editor::SetNumber("units", "pixelsPerUnit", 64.0f, error), error);
    CHECK(WorldUnit::Pixels() == doctest::Approx(64.0f));

    // ---- gravity ---------------------------------------------------------
    const float gravity[2] = {0.0f, -500.0f};
    REQUIRE(editor::SetNumbers("physics", "gravity", gravity, 2, error));
    CHECK(Physics2D::Gravity().y == doctest::Approx(-500.0f));

    // ---- how loud the log is ---------------------------------------------
    REQUIRE(editor::SetString("logging", "threshold", "Error", error));
    CHECK(Log::GetThreshold() == LogLevel::Error);

    // ---- and the engine's own copy agrees with all of it -----------------
    //
    // Config() is what every panel reads. A setting applied to a subsystem
    // but not to Config() would show the old number in the editor while the
    // new one was in force, which is worse than not applying it at all.
    const BootConfig& config = eng::Engine::Get().Config();
    CHECK(config.worldUnitPixels == doctest::Approx(64.0f));
    CHECK(config.gravity.y == doctest::Approx(-500.0f));
    CHECK(config.logThreshold == LogLevel::Error);
}

TEST_CASE("re-reading the settings leaves untouched values alone") {
    // ======================================================================
    //  THIS IS NOT AN OPTIMISATION, IT IS THE BEHAVIOUR.
    //
    //  LogBuffer::SetCapacity EMPTIES THE CONSOLE. If re-reading applied
    //  every key rather than the changed ones, nudging the gravity would wipe
    //  the log - and that would be blamed on the Console, not on the gravity.
    //
    //  Checked here through the world unit, which is observable without
    //  filling a buffer: it is set to something unusual, a DIFFERENT setting
    //  is then written, and it has to still be unusual afterwards.
    // ======================================================================
    SettingsSandbox box("untouched");

    std::string error;
    REQUIRE(editor::SetNumber("units", "pixelsPerUnit", 48.0f, error));
    REQUIRE(WorldUnit::Pixels() == doctest::Approx(48.0f));

    // Something the file has never mentioned, set directly - the way another
    // part of the engine might have set it.
    WorldUnit::SetPixels(7.0f);

    REQUIRE(editor::SetInt("tunables", "maxStepsPerFrame", 9, error));

    // Still 7: the file still says 48, the engine's copy still says 48, so
    // nothing about the world unit changed and nothing was re-applied.
    CHECK(WorldUnit::Pixels() == doctest::Approx(7.0f));
    CHECK(eng::Engine::Get().Config().maxStepsPerFrame == 9);
}

TEST_CASE("a settings file that will not parse leaves the engine as it was") {
    SettingsSandbox box("reload-broken", /*writeConfig=*/false);

    // A known-good state first.
    std::string error;
    REQUIRE(editor::SetNumber("units", "pixelsPerUnit", 24.0f, error));
    REQUIRE(WorldUnit::Pixels() == doctest::Approx(24.0f));

    {
        std::ofstream out(box.root / "config" / "engine.json", std::ios::binary);
        out << R"({"units": {"pixelsPerUnit": 9)";
    }

    // Half-replaced settings would be worse than stale ones: the engine runs
    // on what it has rather than on half of what it was given.
    std::string reloadError;
    CHECK_FALSE(eng::Engine::Get().ReloadSettings(reloadError));
    CHECK_FALSE(reloadError.empty());
    CHECK(WorldUnit::Pixels() == doctest::Approx(24.0f));
    CHECK(eng::Engine::Get().Config().worldUnitPixels == doctest::Approx(24.0f));
}

TEST_CASE("the editor's window is never touched by a project's window settings") {
    // ======================================================================
    //  A project that ships full screen at a locked size must not open the
    //  EDITOR that way - there would be no way back except hand-editing the
    //  file. Engine::Options::gameOwnsTheWindow is the gate, and this engine
    //  was never told it owns one, so the keys are recorded and not applied.
    // ======================================================================
    SettingsSandbox box("window-gate");

    std::string error;
    REQUIRE(editor::SetBool("window", "fullscreen", true, error));
    REQUIRE(editor::SetBool("window", "resizable", false, error));

    // Written, and remembered, so the export and the Game view both see it...
    CHECK(eng::Engine::Get().Config().fullscreen);
    CHECK_FALSE(eng::Engine::Get().Config().windowResizable);

    // ...and that is the whole of what happened. There is no window in a test
    // run to inspect; what matters is that ReloadSettings reported success
    // rather than trying to act on it.
    std::string reloadError;
    CHECK(eng::Engine::Get().ReloadSettings(reloadError));
}

TEST_CASE("a number with no JSON spelling is refused rather than replaced") {
    // ======================================================================
    //  WHY THIS IS NOT PEDANTRY. An infinity has no spelling in JSON, and
    //  the first version of this quietly wrote 0.0 instead and returned
    //  success. For units.pixelsPerUnit that is the difference between a
    //  32-pixel world unit and a 1-pixel one - every collider in the project
    //  resized by a factor of 32, reported to the editor as "done".
    // ======================================================================
    SettingsSandbox box("nonfinite");

    const std::string before = box.RawText();
    const float       infinity = std::numeric_limits<float>::infinity();

    std::string error;
    CHECK_FALSE(editor::SetNumber("units", "pixelsPerUnit", infinity, error));
    CHECK_FALSE(error.empty());

    const float pair[2] = {0.0f, std::numeric_limits<float>::quiet_NaN()};
    CHECK_FALSE(editor::SetNumbers("physics", "gravity", pair, 2, error));

    // AND NOTHING WAS WRITTEN. A refusal that had already half-written the
    // file would be worse than the value it refused.
    CHECK(box.RawText() == before);
    CHECK(WorldUnit::Pixels() == doctest::Approx(box.worldUnitWas));
}

TEST_CASE("a settings file that is there but cannot be read is left alone") {
    // ======================================================================
    //  THERE-AND-UNREADABLE IS NOT ABSENT, and the engine's own reader
    //  cannot tell the difference - it answers "no settings file; using
    //  built-in defaults" to both. Taking that at face value here would
    //  replace a project's whole settings file, every key binding included,
    //  with a file holding one key.
    //
    //  A FOLDER WHERE THE FILE SHOULD BE is the portable way to produce a
    //  path that exists and will not open as a file.
    // ======================================================================
    SettingsSandbox box("unreadable", /*writeConfig=*/false);

    std::error_code ec;
    fs::create_directories(box.root / "config" / "engine.json", ec);
    REQUIRE(fs::is_directory(box.root / "config" / "engine.json"));

    std::string error;
    CHECK_FALSE(editor::SetNumber("units", "pixelsPerUnit", 64.0f, error));
    CHECK(error.find("could not be read") != std::string::npos);

    // Still a folder: nothing tried to write a file over it, and nothing
    // reported success.
    CHECK(fs::is_directory(box.root / "config" / "engine.json"));

    // And the engine was not quietly reset to built-in defaults either.
    CHECK(WorldUnit::Pixels() == doctest::Approx(box.worldUnitWas));
}
