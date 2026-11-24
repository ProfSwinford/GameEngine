// ============================================================================
//  test_editorprefs.cpp - the editor's own appearance, as a file.
//
//  WHY THIS IS WORTH TESTING. It is a file people edit by hand to get a
//  colour exactly right, and it is read at start-up before anything is drawn.
//  A value that makes no sense has to cost the one value and nothing else: a
//  preferences file that could stop the editor opening would be a far worse
//  bargain than no preferences file at all.
//
//  So almost every case here is about what a WRONG file does, not a right one.
//
//  The file-system half - PrefsFilePath, LoadPrefs, SavePrefs, ForgetPrefs -
//  is deliberately NOT exercised: it writes to the real user settings folder,
//  and a test that left a file in somebody's AppData would be a rude test.
//  Everything that decides anything lives in PrefsFromJson/PrefsToJson, which
//  take and return values.
// ============================================================================

#include <doctest/doctest.h>

#include <EditorPrefs.h>

#include <engine/core/Json.h>

#include <string>

using namespace editor;

TEST_CASE("a fresh install has no file, and the defaults are what it gets") {
    const EditorPrefs prefs = PrefsFromJson(eng::Json::object());

    CHECK(prefs.theme == EditorTheme::Dark);
    CHECK(prefs.fontSize == doctest::Approx(13.0f));
    CHECK(prefs.uiScale == doctest::Approx(1.0f));
    CHECK(prefs.fontPath.empty());
    CHECK(prefs.colors.empty());
}

TEST_CASE("everything survives a round trip") {
    EditorPrefs before;
    before.theme    = EditorTheme::Light;
    before.fontSize = 17.0f;
    before.uiScale  = 1.25f;
    before.fontPath = "C:/Windows/Fonts/consola.ttf";
    before.colors["WindowBg"] = {0.1f, 0.2f, 0.3f, 1.0f};
    before.colors["Button"]   = {0.0f, 0.5f, 0.0f, 0.75f};

    const EditorPrefs after = PrefsFromJson(PrefsToJson(before));

    CHECK(after.theme == EditorTheme::Light);
    CHECK(after.fontSize == doctest::Approx(17.0f));
    CHECK(after.uiScale == doctest::Approx(1.25f));
    CHECK(after.fontPath == "C:/Windows/Fonts/consola.ttf");

    REQUIRE(after.colors.size() == 2);
    CHECK(after.colors.at("WindowBg")[2] == doctest::Approx(0.3f));
    CHECK(after.colors.at("Button")[3] == doctest::Approx(0.75f));
}

TEST_CASE("the theme is stored by NAME, not by number") {
    // ======================================================================
    //  A FILE FULL OF NUMBERS WOULD SILENTLY RECOLOUR THE EDITOR the first
    //  time somebody reordered the enum. Naming it costs a string compare
    //  once per launch and makes the file readable into the bargain.
    // ======================================================================
    EditorPrefs prefs;
    prefs.theme = EditorTheme::Classic;
    CHECK(PrefsToJson(prefs)["theme"].get<std::string>() == "classic");

    prefs.theme = EditorTheme::Light;
    CHECK(PrefsToJson(prefs)["theme"].get<std::string>() == "light");

    prefs.theme = EditorTheme::Dark;
    CHECK(PrefsToJson(prefs)["theme"].get<std::string>() == "dark");
}

TEST_CASE("a theme this version has never heard of costs the theme, not the editor") {
    eng::Json document   = eng::Json::object();
    document["theme"]    = "solarized-midnight-plus";
    document["fontSize"] = 15.0f;

    const EditorPrefs prefs = PrefsFromJson(document);

    CHECK(prefs.theme == EditorTheme::Dark);          // the default, silently
    CHECK(prefs.fontSize == doctest::Approx(15.0f));  // and the rest still read
}

TEST_CASE("a size nobody could use is clamped rather than obeyed") {
    // A 2px font is not small text, it is an editor nobody can read and no
    // menu to fix it from - which is a one-way trip for anyone who does not
    // know the file exists.
    eng::Json document   = eng::Json::object();
    document["fontSize"] = 0.5f;
    document["uiScale"]  = 40.0f;

    const EditorPrefs prefs = PrefsFromJson(document);

    CHECK(prefs.fontSize >= 7.0f);
    CHECK(prefs.uiScale <= 3.0f);
}

TEST_CASE("the font size is a real number and never a 'use the default' zero") {
    // ======================================================================
    //  DEAR IMGUI FILLS AN UNSET SIZE IN ON THE FIRST FRAME, and what it
    //  fills in depends on which font is loaded - 13 for the built-in one, 20
    //  for a .ttf. A zero here would mean that choosing a font also changed
    //  the size by a third, which is nobody's idea of choosing a font.
    // ======================================================================
    eng::Json document   = eng::Json::object();
    document["fontSize"] = 0.0f;

    CHECK(PrefsFromJson(document).fontSize > 0.0f);
}

TEST_CASE("a half-typed colour is skipped and the rest of the file still reads") {
    eng::Json colors               = eng::Json::object();
    colors["WindowBg"]             = eng::Json::array({0.1f, 0.2f, 0.3f});  // no alpha
    colors["Button"]               = "not a colour at all";
    colors["Text"]                 = eng::Json::array({1.0f, 1.0f, 1.0f, 1.0f});
    colors["TitleBg"]              = eng::Json::array({0.0f, 0.0f, "x", 1.0f});

    eng::Json document  = eng::Json::object();
    document["colors"]  = colors;

    const EditorPrefs prefs = PrefsFromJson(document);

    // THREE OF THE FOUR ARE WRONG AND THE FOURTH STILL ARRIVES. Guessing at
    // the missing alpha is how a half-typed line becomes an invisible window.
    REQUIRE(prefs.colors.size() == 1);
    CHECK(prefs.colors.count("Text") == 1);
}

TEST_CASE("a colour outside 0..1 is pulled back into range") {
    eng::Json colors   = eng::Json::object();
    colors["WindowBg"] = eng::Json::array({-4.0f, 0.5f, 9.0f, 1.0f});

    eng::Json document = eng::Json::object();
    document["colors"] = colors;

    // THE PREFS ARE HELD, not reached into. Binding a const reference to a
    // member of a temporary does not extend the temporary, and the release
    // build happened to leave the memory readable while the debug build
    // poisoned it - which is exactly the kind of difference worth never
    // writing in the first place.
    const EditorPrefs prefs    = PrefsFromJson(document);
    const auto&       channels = prefs.colors.at("WindowBg");
    CHECK(channels[0] == doctest::Approx(0.0f));
    CHECK(channels[1] == doctest::Approx(0.5f));
    CHECK(channels[2] == doctest::Approx(1.0f));
}

TEST_CASE("something that is not a set of settings at all gives the defaults") {
    CHECK(PrefsFromJson(eng::Json("just a string")).fontSize == doctest::Approx(13.0f));
    CHECK(PrefsFromJson(eng::Json::array({1, 2, 3})).theme == EditorTheme::Dark);
    CHECK(PrefsFromJson(eng::Json()).colors.empty());
}

TEST_CASE("the written file explains itself and keeps its shape") {
    const eng::Json document = PrefsToJson(EditorPrefs{});

    // Somebody opening this file should be able to tell what it is and that
    // deleting it is safe - it is in their AppData, next to files that are
    // not safe to delete.
    REQUIRE(document.contains("_comment"));
    CHECK(document["_comment"].get<std::string>().find("Delete this file") !=
          std::string::npos);

    // "colors" is written even when empty, so a person editing one by hand
    // can see where they go rather than having to guess the key.
    REQUIRE(document.contains("colors"));
    CHECK(document["colors"].is_object());
    CHECK(document.contains("fontPath"));
}

TEST_CASE("clamping an already-sane set of preferences changes nothing") {
    EditorPrefs prefs;
    prefs.fontSize            = 16.0f;
    prefs.uiScale             = 1.5f;
    prefs.colors["WindowBg"]  = {0.25f, 0.5f, 0.75f, 1.0f};

    ClampPrefs(prefs);

    CHECK(prefs.fontSize == doctest::Approx(16.0f));
    CHECK(prefs.uiScale == doctest::Approx(1.5f));
    CHECK(prefs.colors.at("WindowBg")[1] == doctest::Approx(0.5f));
}
