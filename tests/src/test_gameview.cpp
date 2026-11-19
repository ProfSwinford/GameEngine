// ============================================================================
//  test_gameview.cpp - what shape the Game view renders at, and how big it is
//  shown.
//
//  WHY THIS IS WORTH A SUITE. The whole feature rests on TWO SIZES being kept
//  apart: what the camera renders at, and how big that picture is drawn. A
//  bigger render viewport shows MORE WORLD at the same zoom - see
//  render/Camera.h - so confusing the two does not produce a stretched
//  picture, it produces a DIFFERENT AMOUNT OF LEVEL, which is exactly the
//  thing a fixed resolution exists to pin down.
//
//  And the pointer translation divides by one and multiplies by the other.
//  Get that backwards and the symptom is UI buttons that only respond near
//  one corner, which gets blamed on the layout code.
// ============================================================================

#include <doctest/doctest.h>

#include <GameViewLayout.h>

#include <engine/render/Camera.h>
#include <engine/scene/CameraComponent.h>
#include <engine/scene/Scene.h>
#include <engine/render/Presentation.h>

#include <string>

using namespace editor;

namespace {

const GameViewMode& Built(GameViewMode::Kind kind, int x, int y) {
    for (const GameViewMode& mode : BuiltInGameViewModes()) {
        if (mode.kind == kind && mode.x == x && mode.y == y) {
            return mode;
        }
    }
    FAIL("no such built-in mode");
    return BuiltInGameViewModes().front();
}

} // namespace

TEST_CASE("Free Aspect is first, so a project with no choice opens as it always did") {
    const auto& modes = BuiltInGameViewModes();
    REQUIRE_FALSE(modes.empty());

    // LOAD-BEARING rather than tidy: index 0 is the default, so the editor
    // behaves exactly as it did before this feature until somebody asks for
    // something else.
    CHECK(modes.front().kind == GameViewMode::Kind::Free);
    CHECK(modes.front().label == "Free Aspect");
}

TEST_CASE("Free Aspect renders at the panel's size") {
    const GameViewLayout layout =
        LayoutGameView(BuiltInGameViewModes().front(), 800.0f, 600.0f, kFitScale);

    CHECK(layout.targetWidth == 800);
    CHECK(layout.targetHeight == 600);

    // And drawn 1:1, because it already fits by construction.
    CHECK(layout.drawWidth == doctest::Approx(800.0f));
    CHECK(layout.drawHeight == doctest::Approx(600.0f));
}

TEST_CASE("an aspect pins the shape and fills the panel as far as it can") {
    const GameViewMode& wide = Built(GameViewMode::Kind::Aspect, 16, 9);

    SUBCASE("a panel wider than the shape is limited by its height") {
        const GameViewLayout layout = LayoutGameView(wide, 1000.0f, 400.0f, kFitScale);
        CHECK(layout.targetHeight == 400);
        CHECK(layout.targetWidth == 711);   // 400 * 16/9
    }

    SUBCASE("a panel taller than the shape is limited by its width") {
        const GameViewLayout layout = LayoutGameView(wide, 640.0f, 1000.0f, kFitScale);
        CHECK(layout.targetWidth == 640);
        CHECK(layout.targetHeight == 360);   // 640 * 9/16
    }

    SUBCASE("the shape is exact, whatever the panel") {
        const GameViewLayout layout = LayoutGameView(wide, 1234.0f, 567.0f, kFitScale);
        const float ratio = static_cast<float>(layout.targetWidth) /
                            static_cast<float>(layout.targetHeight);
        CHECK(ratio == doctest::Approx(16.0f / 9.0f).epsilon(0.01));
    }
}

TEST_CASE("a resolution is exactly what was asked for, whatever the panel is") {
    const GameViewMode& hd = Built(GameViewMode::Kind::Resolution, 1920, 1080);

    // THE DISTINCTION THAT MATTERS. An aspect follows the panel in size; a
    // resolution does not follow it at all. This is the only mode that
    // answers "what will a player actually see".
    for (const float room : {200.0f, 600.0f, 4000.0f}) {
        const GameViewLayout layout = LayoutGameView(hd, room, room, kFitScale);
        CHECK(layout.targetWidth == 1920);
        CHECK(layout.targetHeight == 1080);
    }
}

TEST_CASE("Fit scales a big render down to the panel, and never blows a small one up") {
    const GameViewMode& hd    = Built(GameViewMode::Kind::Resolution, 1920, 1080);
    const GameViewMode& small = Built(GameViewMode::Kind::Resolution, 480, 270);

    SUBCASE("down, keeping its shape") {
        const GameViewLayout layout = LayoutGameView(hd, 960.0f, 1000.0f, kFitScale);
        CHECK(layout.drawWidth == doctest::Approx(960.0f));
        CHECK(layout.drawHeight == doctest::Approx(540.0f));

        // Still rendering at full size - only the drawing shrank.
        CHECK(layout.targetWidth == 1920);
    }

    SUBCASE("but NOT up") {
        // ==============================================================
        //  A 480x270 target stretched to fill a wide panel makes every
        //  judgement about pixel art a lie. What is wanted there is a scale
        //  of 2 or 3, chosen deliberately - so Fit stops at 1:1.
        // ==============================================================
        const GameViewLayout layout = LayoutGameView(small, 1920.0f, 1080.0f, kFitScale);
        CHECK(layout.drawWidth == doctest::Approx(480.0f));
        CHECK(layout.drawHeight == doctest::Approx(270.0f));
    }
}

TEST_CASE("the scale changes how big it is DRAWN and never what it renders") {
    const GameViewMode& hd = Built(GameViewMode::Kind::Resolution, 1920, 1080);

    // THE WHOLE POINT OF KEEPING THEM APART. Scaling the render would change
    // what the camera sees, which is the problem a fixed resolution solves.
    const GameViewLayout half = LayoutGameView(hd, 600.0f, 400.0f, 0.5f);
    CHECK(half.targetWidth == 1920);
    CHECK(half.targetHeight == 1080);
    CHECK(half.drawWidth == doctest::Approx(960.0f));
    CHECK(half.drawHeight == doctest::Approx(540.0f));

    // A scale of 1 is one render pixel per screen pixel, even though that
    // leaves most of the picture outside a 600-pixel panel. Deliberately: it
    // is the only honest way to look at pixel art.
    const GameViewLayout oneToOne = LayoutGameView(hd, 600.0f, 400.0f, 1.0f);
    CHECK(oneToOne.drawWidth == doctest::Approx(1920.0f));
    CHECK(oneToOne.drawHeight == doctest::Approx(1080.0f));
}

TEST_CASE("the pointer conversion is the ratio between the two sizes") {
    const GameViewMode& hd = Built(GameViewMode::Kind::Resolution, 1920, 1080);
    const GameViewLayout layout = LayoutGameView(hd, 960.0f, 1000.0f, kFitScale);

    // Drawn at half size, so one drawn pixel is two render pixels - and a
    // click 300 pixels into the picture is 600 pixels into the game.
    CHECK(layout.TargetPerDrawX() == doctest::Approx(2.0f));
    CHECK(layout.TargetPerDrawY() == doctest::Approx(2.0f));

    SUBCASE("and it is 1 when nothing is being scaled") {
        const GameViewLayout free =
            LayoutGameView(BuiltInGameViewModes().front(), 800.0f, 600.0f, kFitScale);
        CHECK(free.TargetPerDrawX() == doctest::Approx(1.0f));
        CHECK(free.TargetPerDrawY() == doctest::Approx(1.0f));
    }
}

TEST_CASE("a panel dragged shut still produces a layout that can be created") {
    // A render target of zero cannot be made, and the panel would report
    // "this view could not be created" every time somebody collapsed it.
    for (const float room : {0.0f, -50.0f, 0.4f}) {
        const GameViewLayout layout =
            LayoutGameView(BuiltInGameViewModes().front(), room, room, kFitScale);
        CHECK(layout.targetWidth >= 1);
        CHECK(layout.targetHeight >= 1);
        CHECK(layout.drawWidth >= 1.0f);
        CHECK(layout.drawHeight >= 1.0f);
    }
}

// ---------------------------------------------------------------------------
//  Typing a custom entry
// ---------------------------------------------------------------------------

TEST_CASE("the separator says whether a custom entry is a ratio or a size") {
    GameViewMode mode;
    std::string  error;

    // ONE BOX FOR BOTH, because which one somebody meant is already written
    // in the separator they typed.
    REQUIRE(ParseGameViewMode("16:9", mode, error));
    CHECK(mode.kind == GameViewMode::Kind::Aspect);
    CHECK(mode.x == 16);
    CHECK(mode.y == 9);
    CHECK(mode.label == "16:9");

    REQUIRE(ParseGameViewMode("1920x1080", mode, error));
    CHECK(mode.kind == GameViewMode::Kind::Resolution);
    CHECK(mode.x == 1920);
    CHECK(mode.y == 1080);
    CHECK(mode.label == "1920 x 1080");

    SUBCASE("spelled a few ways people actually type it") {
        CHECK(ParseGameViewMode("1366X768", mode, error));
        CHECK(mode.kind == GameViewMode::Kind::Resolution);

        CHECK(ParseGameViewMode(" 21 : 9 ", mode, error));
        CHECK(mode.kind == GameViewMode::Kind::Aspect);
        CHECK(mode.x == 21);
    }
}

TEST_CASE("a custom entry that is not a ratio or a size is refused with a reason") {
    GameViewMode mode;
    std::string  error;

    for (const char* nonsense : {"", "16", "big", "16:", ":9", "16:9:4", "-16x9",
                                 "0x100", "16:0", "abcxdef"}) {
        error.clear();
        CAPTURE(nonsense);
        CHECK_FALSE(ParseGameViewMode(nonsense, mode, error));

        // A REASON, not a refusal. Somebody typed this into a box and has to
        // be able to work out what to type instead.
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("a zero or negative custom size is refused rather than clamped") {
    // Clamping 0x100 to 1x100 would accept a typo and render a one-pixel-wide
    // game, which looks like a broken engine rather than a bad entry.
    GameViewMode mode;
    std::string  error;
    CHECK_FALSE(ParseGameViewMode("0x0", mode, error));
    CHECK_FALSE(ParseGameViewMode("0:0", mode, error));
}

// ===========================================================================
//  AND THE SHIPPED GAME: eng::Presentation.
//
//  The same question as above asked of a window instead of a panel, and with
//  a DIFFERENT POLICY: a player wants the game as large as the screen allows,
//  so this scales up to fill; the editor stops at 1:1 so that one render
//  pixel can be seen as one screen pixel. Same arithmetic, two policies, and
//  these cases are what keep them from being quietly unified.
// ===========================================================================

TEST_CASE("Free presentation fills the window and letterboxes nothing") {
    eng::Presentation::SetFree();
    REQUIRE(eng::Presentation::IsFree());

    const auto plan = eng::Presentation::PlanFor(1280, 800);
    CHECK(plan.renderWidth == 1280);
    CHECK(plan.renderHeight == 800);
    CHECK(plan.width == doctest::Approx(1280.0f));
    CHECK(plan.height == doctest::Approx(800.0f));
    CHECK_FALSE(plan.letterboxed);
    CHECK(plan.x == doctest::Approx(0.0f));
}

TEST_CASE("a pinned aspect letterboxes a window of the wrong shape") {
    eng::Presentation::SetAspect(16, 9);

    SUBCASE("a taller window gets bars top and bottom") {
        const auto plan = eng::Presentation::PlanFor(1280, 800);
        CHECK(plan.letterboxed);
        CHECK(plan.width == doctest::Approx(1280.0f));
        CHECK(plan.height == doctest::Approx(720.0f));
        CHECK(plan.x == doctest::Approx(0.0f));
        CHECK(plan.y == doctest::Approx(40.0f));   // (800 - 720) / 2
    }

    SUBCASE("a wider window gets bars at the sides") {
        const auto plan = eng::Presentation::PlanFor(2560, 1080);
        CHECK(plan.letterboxed);
        CHECK(plan.height == doctest::Approx(1080.0f));
        CHECK(plan.width == doctest::Approx(1920.0f));
        CHECK(plan.x == doctest::Approx(320.0f));
        CHECK(plan.y == doctest::Approx(0.0f));
    }

    SUBCASE("a window already that shape gets no bars") {
        // AND NOT BY COMPARING FLOATS FOR EQUALITY. A window one rounding
        // error off the shape would otherwise be letterboxed, and the engine
        // would clear the backbuffer every frame to draw nothing.
        const auto plan = eng::Presentation::PlanFor(1920, 1080);
        CHECK_FALSE(plan.letterboxed);
    }

    eng::Presentation::SetFree();
}

TEST_CASE("a pinned aspect scales UP to fill the window, unlike the editor panel") {
    // THE POLICY DIFFERENCE, asserted rather than left in a comment. A player
    // wants the game as big as the screen allows; the editor stops at 1:1 so
    // pixel art can be judged. Same ratio, opposite answers.
    eng::Presentation::SetAspect(16, 9);
    const auto plan = eng::Presentation::PlanFor(3840, 2160);
    CHECK(plan.width == doctest::Approx(3840.0f));
    CHECK(plan.renderWidth == 3840);
    eng::Presentation::SetFree();
}

TEST_CASE("a pinned resolution renders at that size whatever the window is") {
    eng::Presentation::SetResolution(640, 360);

    const auto small = eng::Presentation::PlanFor(800, 600);
    CHECK(small.renderWidth == 640);
    CHECK(small.renderHeight == 360);

    const auto big = eng::Presentation::PlanFor(3840, 2160);
    CHECK(big.renderWidth == 640);
    CHECK(big.renderHeight == 360);

    // ...and is DRAWN as large as the window allows at that shape, so a
    // 640x360 game fills a 4K screen without asking the monitor to change
    // mode. That is the whole reason borderless full screen is the one worth
    // having - see platform/Window.h.
    CHECK(big.width == doctest::Approx(3840.0f));
    CHECK(big.height == doctest::Approx(2160.0f));

    eng::Presentation::SetFree();
}

TEST_CASE("a nonsense shape is refused and leaves the mode alone") {
    eng::Presentation::SetAspect(16, 9);
    eng::Presentation::SetAspect(0, 9);
    eng::Presentation::SetResolution(-1, 100);

    // Still 16:9. Clamping a typo to 1x9 would render a sliver and look like
    // a broken engine rather than a bad setting.
    CHECK(eng::Presentation::CurrentMode() == eng::Presentation::Mode::Aspect);
    CHECK(eng::Presentation::WantedX() == 16);
    eng::Presentation::SetFree();
}

TEST_CASE("one parser reads the settings file and the editor's box alike") {
    eng::Presentation::Mode mode = eng::Presentation::Mode::Resolution;
    int                     x    = 0;
    int                     y    = 0;
    std::string             error;

    REQUIRE(eng::Presentation::ParseSpec("free", mode, x, y, error));
    CHECK(mode == eng::Presentation::Mode::Free);

    // An empty setting is free, not an error: a project that has never
    // mentioned presentation should start.
    REQUIRE(eng::Presentation::ParseSpec("", mode, x, y, error));
    CHECK(mode == eng::Presentation::Mode::Free);

    REQUIRE(eng::Presentation::ParseSpec("16:9", mode, x, y, error));
    CHECK(mode == eng::Presentation::Mode::Aspect);
    CHECK(x == 16);
    CHECK(y == 9);

    REQUIRE(eng::Presentation::ParseSpec("640x360", mode, x, y, error));
    CHECK(mode == eng::Presentation::Mode::Resolution);
    CHECK(x == 640);

    // AND IT DOES NOT CHANGE THE CURRENT MODE. The settings loader warns and
    // carries on; a text box keeps what it had. Both decide for themselves.
    const auto before = eng::Presentation::CurrentMode();
    CHECK_FALSE(eng::Presentation::ParseSpec("sideways", mode, x, y, error));
    CHECK_FALSE(error.empty());
    CHECK(eng::Presentation::CurrentMode() == before);
}

TEST_CASE("the editor's parser and the engine's agree about the same text") {
    // ONE READER, TWO CALLERS - the editor adds a label and nothing else.
    // Two copies of "what does 16:9 mean" is the drift worth preventing.
    GameViewMode            editorMode;
    std::string             editorError;
    eng::Presentation::Mode engineMode = eng::Presentation::Mode::Free;
    int                     ex = 0;
    int                     ey = 0;
    std::string             engineError;

    for (const char* text : {"16:9", "1920x1080", "21:9", "640x360"}) {
        CAPTURE(text);
        REQUIRE(ParseGameViewMode(text, editorMode, editorError));
        REQUIRE(eng::Presentation::ParseSpec(text, engineMode, ex, ey, engineError));

        CHECK(editorMode.x == ex);
        CHECK(editorMode.y == ey);

        const bool bothAspect = editorMode.kind == GameViewMode::Kind::Aspect &&
                                engineMode == eng::Presentation::Mode::Aspect;
        const bool bothSize = editorMode.kind == GameViewMode::Kind::Resolution &&
                              engineMode == eng::Presentation::Mode::Resolution;
        CHECK((bothAspect || bothSize));
    }
}

// ===========================================================================
//  HOW THE CAMERA DECIDES WHAT IS IN SHOT.
//
//  Two framings, and the second was added because the first does not survive
//  a change of resolution: with a zoom, the viewport decides how much world
//  fits, so a player at 2560x1440 saw further than one at 1280x720.
//
//  THE FIRST FOUR CASES ARE THE BACKWARDS COMPATIBILITY GUARANTEE. Every
//  camera in every scene already written is framed by zoom, and the whole
//  point of the way this was built is that those take a branch that does
//  nothing. If one of these fails, an existing project has been broken.
// ===========================================================================

TEST_CASE("a camera is framed by zoom until something says otherwise") {
    eng::Camera camera;
    CHECK(camera.CurrentFraming() == eng::Camera::Framing::Zoom);
    CHECK(camera.Zoom() == doctest::Approx(1.0f));
}

TEST_CASE("framing by zoom still lets a bigger viewport show more world") {
    // THE OLD BEHAVIOUR, PINNED. It is right for the Scene view - dragging a
    // panel wider should reveal more of the level - and this case exists so
    // that adding the other framing cannot quietly change it.
    eng::Camera camera;
    camera.SetZoom(1.0f);

    camera.SetViewportSize(eng::Vec2{1280.0f, 720.0f});
    CHECK(camera.Zoom() == doctest::Approx(1.0f));
    CHECK(camera.FrameHeight() == doctest::Approx(720.0f));

    camera.SetViewportSize(eng::Vec2{2560.0f, 1440.0f});
    CHECK(camera.Zoom() == doctest::Approx(1.0f));   // unchanged...
    CHECK(camera.FrameHeight() == doctest::Approx(1440.0f));   // ...so twice the world
}

TEST_CASE("the visible bounds of a zoom-framed camera are what they always were") {
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{800.0f, 600.0f});
    camera.SetZoom(2.0f);
    camera.SetPosition(eng::Vec2{0.0f, 0.0f});

    const eng::AABB seen = camera.VisibleBounds();
    CHECK((seen.max.x - seen.min.x) == doctest::Approx(400.0f));   // 800 / 2
    CHECK((seen.max.y - seen.min.y) == doctest::Approx(300.0f));   // 600 / 2
}

TEST_CASE("Reset puts a camera back to being framed by zoom") {
    eng::Camera camera;
    camera.SetFrameHeight(360.0f);
    REQUIRE(camera.CurrentFraming() == eng::Camera::Framing::Height);

    // Reset means "as a new camera is", and a new camera is framed by zoom -
    // otherwise Reset would leave it in a mode whose number it had just
    // thrown away.
    camera.Reset();
    CHECK(camera.CurrentFraming() == eng::Camera::Framing::Zoom);
    CHECK(camera.Zoom() == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
//  And the new framing
// ---------------------------------------------------------------------------

TEST_CASE("framing by height shows the same world however big the picture is") {
    // THE WHOLE POINT. A bigger viewport draws the same slice LARGER rather
    // than revealing more of the level, which is what makes "what I framed in
    // the Game view is what ships" true.
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{1280.0f, 720.0f});
    camera.SetFrameHeight(720.0f);
    CHECK(camera.Zoom() == doctest::Approx(1.0f));

    camera.SetViewportSize(eng::Vec2{2560.0f, 1440.0f});
    CHECK(camera.FrameHeight() == doctest::Approx(720.0f));   // the same slice...
    CHECK(camera.Zoom() == doctest::Approx(2.0f));            // ...drawn twice as big

    camera.SetViewportSize(eng::Vec2{640.0f, 360.0f});
    CHECK(camera.FrameHeight() == doctest::Approx(720.0f));
    CHECK(camera.Zoom() == doctest::Approx(0.5f));
}

TEST_CASE("the width follows the aspect ratio, as an orthographic camera does") {
    eng::Camera camera;
    camera.SetFrameHeight(360.0f);
    camera.SetPosition(eng::Vec2{0.0f, 0.0f});

    SUBCASE("16:9") {
        camera.SetViewportSize(eng::Vec2{1920.0f, 1080.0f});
        const eng::AABB seen = camera.VisibleBounds();
        CHECK((seen.max.y - seen.min.y) == doctest::Approx(360.0f));
        CHECK((seen.max.x - seen.min.x) == doctest::Approx(640.0f));   // 360 * 16/9
    }

    SUBCASE("21:9 sees further sideways and no further up") {
        // THE CONVENTIONAL BARGAIN, and the one every ultrawide player
        // expects: the same vertical slice, more of it horizontally.
        camera.SetViewportSize(eng::Vec2{2560.0f, 1080.0f});
        const eng::AABB seen = camera.VisibleBounds();
        CHECK((seen.max.y - seen.min.y) == doctest::Approx(360.0f));
        CHECK((seen.max.x - seen.min.x) == doctest::Approx(853.33f).epsilon(0.01));
    }
}

TEST_CASE("setting a zoom by hand switches the framing back") {
    // The mode follows the last thing asked for. A camera framed by height
    // whose zoom was then set would have that zoom thrown away by the next
    // resize, silently - so saying "zoom 3" has to mean it.
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{1280.0f, 720.0f});
    camera.SetFrameHeight(720.0f);
    REQUIRE(camera.CurrentFraming() == eng::Camera::Framing::Height);

    camera.SetZoom(3.0f);
    CHECK(camera.CurrentFraming() == eng::Camera::Framing::Zoom);

    camera.SetViewportSize(eng::Vec2{2560.0f, 1440.0f});
    CHECK(camera.Zoom() == doctest::Approx(3.0f));   // held, not re-derived
}

TEST_CASE("switching framing either way keeps the shot exactly") {
    // What the editor's little switch button does: change which number is
    // held still, not what is on screen.
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{1600.0f, 900.0f});
    camera.SetZoom(2.5f);

    const float heightBefore = camera.FrameHeight();
    camera.SetFrameHeight(camera.FrameHeight());
    CHECK(camera.CurrentFraming() == eng::Camera::Framing::Height);
    CHECK(camera.Zoom() == doctest::Approx(2.5f));
    CHECK(camera.FrameHeight() == doctest::Approx(heightBefore));

    camera.SetZoom(camera.Zoom());
    CHECK(camera.CurrentFraming() == eng::Camera::Framing::Zoom);
    CHECK(camera.FrameHeight() == doctest::Approx(heightBefore));
}

TEST_CASE("a frame height of zero is clamped rather than making the view uninvertible") {
    // A height of zero is a zoom of infinity, and the matrix it produces
    // cannot be undone - so nothing could be clicked on.
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{1280.0f, 720.0f});
    camera.SetFrameHeight(0.0f);

    CHECK(camera.FrameHeight() > 0.0f);
    CHECK(camera.Zoom() > 0.0f);

    // And the round trip through the matrix still works, which is the thing
    // the clamp is actually protecting.
    const eng::Vec2 back = camera.ScreenToWorld(camera.WorldToScreen(eng::Vec2{10.0f, 20.0f}));
    CHECK(back.x == doctest::Approx(10.0f).epsilon(0.01));
    CHECK(back.y == doctest::Approx(20.0f).epsilon(0.01));
}

TEST_CASE("FrameHeight answers in both modes, so nothing has to ask which") {
    eng::Camera camera;
    camera.SetViewportSize(eng::Vec2{1280.0f, 640.0f});

    camera.SetZoom(2.0f);
    CHECK(camera.FrameHeight() == doctest::Approx(320.0f));   // 640 / 2

    camera.SetFrameHeight(320.0f);
    CHECK(camera.Zoom() == doctest::Approx(2.0f));
}

// ---------------------------------------------------------------------------
//  AND WHAT A SCENE FILE SAYS
//
//  The compatibility contract, in the file rather than in the camera: a scene
//  written before frameHeight existed has only a zoom, and has to read
//  exactly as it always did. A scene framed by height has to save as one, or
//  the next load would turn it back into a zoom and the framing would quietly
//  start following the window again.
// ---------------------------------------------------------------------------

TEST_CASE("a scene with only a zoom reads as framed by zoom") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;

    // EVERY SCENE ALREADY WRITTEN looks like this. If this case fails, an
    // existing project has been broken.
    REQUIRE(scene.LoadFromString(
        R"({"name":"Old","camera":{"position":[10,20],"zoom":2.5},"entities":[]})",
        error));

    CHECK(scene.InitialCameraZoom() == doctest::Approx(2.5f));
    CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(0.0f));
}

TEST_CASE("a scene with a frameHeight reads as framed by height") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;
    REQUIRE(scene.LoadFromString(
        R"({"name":"New","camera":{"position":[0,0],"frameHeight":360},"entities":[]})",
        error));

    CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(360.0f));
}

TEST_CASE("a scene missing a camera block entirely still loads with the old defaults") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;
    REQUIRE(scene.LoadFromString(R"({"name":"Bare","entities":[]})", error));

    CHECK(scene.InitialCameraZoom() == doctest::Approx(1.0f));
    CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(0.0f));
}

TEST_CASE("a negative frameHeight is refused and falls back to the zoom") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;
    REQUIRE(scene.LoadFromString(
        R"({"name":"Bad","camera":{"zoom":3,"frameHeight":-5},"entities":[]})", error));

    CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(0.0f));
    CHECK(scene.InitialCameraZoom() == doctest::Approx(3.0f));
}

TEST_CASE("a scene saves exactly one of the two camera keys") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;
    REQUIRE(scene.LoadFromString(R"({"name":"S","entities":[]})", error));

    SUBCASE("framed by zoom writes zoom and no frameHeight") {
        scene.SetCameraState(eng::Vec2{1.0f, 2.0f}, 1.5f);

        std::string text;
        REQUIRE(scene.SaveToString(text, error));
        const eng::Json out = eng::Json::parse(text, nullptr, false);
        REQUIRE_FALSE(out.is_discarded());

        CHECK(out["camera"].contains("zoom"));
        CHECK_FALSE(out["camera"].contains("frameHeight"));
    }

    SUBCASE("framed by height writes frameHeight and no zoom") {
        // AND THE OTHER KEY IS REMOVED, not merely left unwritten. This
        // document is an existing one being updated in place, so a stale
        // zoom would survive - and then somebody hand-edits it and nothing
        // changes.
        scene.SetCameraState(eng::Vec2{0.0f, 0.0f}, 1.5f);
        scene.SetCameraFrameHeight(eng::Vec2{1.0f, 2.0f}, 480.0f, 1.5f);

        std::string text;
        REQUIRE(scene.SaveToString(text, error));
        const eng::Json out = eng::Json::parse(text, nullptr, false);
        REQUIRE_FALSE(out.is_discarded());

        CHECK(out["camera"].contains("frameHeight"));
        CHECK_FALSE(out["camera"].contains("zoom"));
        CHECK(out["camera"]["frameHeight"].get<float>() == doctest::Approx(480.0f));
    }
}

TEST_CASE("a scene's framing survives a save and load, both ways round") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::Scene  scene;
    std::string error;
    REQUIRE(scene.LoadFromString(R"({"name":"S","entities":[]})", error));

    SUBCASE("by height") {
        scene.SetCameraFrameHeight(eng::Vec2{5.0f, 6.0f}, 540.0f, 2.0f);

        std::string text;
        REQUIRE(scene.SaveToString(text, error));
        REQUIRE(scene.LoadFromString(text, error));

        CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(540.0f));
        CHECK(scene.InitialCameraPosition().x == doctest::Approx(5.0f));
    }

    SUBCASE("by zoom") {
        scene.SetCameraState(eng::Vec2{5.0f, 6.0f}, 2.0f);

        std::string text;
        REQUIRE(scene.SaveToString(text, error));
        REQUIRE(scene.LoadFromString(text, error));

        CHECK(scene.InitialCameraFrameHeight() == doctest::Approx(0.0f));
        CHECK(scene.InitialCameraZoom() == doctest::Approx(2.0f));
    }
}

TEST_CASE("a CameraComponent keeps the old spelling working and the new one too") {
    eng::ComponentFactory::RegisterBuiltins();

    eng::CameraComponent camera;
    std::string          error;

    SUBCASE("only a zoom, as every existing camera has") {
        eng::Json node;
        node["zoom"] = 3.0f;
        REQUIRE(camera.Deserialize(node, error));
        CHECK(camera.Zoom() == doctest::Approx(3.0f));
        CHECK(camera.FrameHeight() == doctest::Approx(0.0f));

        eng::Json out;
        REQUIRE(camera.Serialize(out));
        CHECK(out.contains("zoom"));
        CHECK_FALSE(out.contains("frameHeight"));
    }

    SUBCASE("a frameHeight wins when present") {
        eng::Json node;
        node["zoom"]        = 3.0f;
        node["frameHeight"] = 400.0f;
        REQUIRE(camera.Deserialize(node, error));
        CHECK(camera.FrameHeight() == doctest::Approx(400.0f));

        eng::Json out;
        REQUIRE(camera.Serialize(out));
        CHECK(out.contains("frameHeight"));
        CHECK_FALSE(out.contains("zoom"));
    }

    SUBCASE("and a component with nothing said is framed by zoom") {
        REQUIRE(camera.Deserialize(eng::Json::object(), error));
        CHECK(camera.FrameHeight() == doctest::Approx(0.0f));
        CHECK(camera.Zoom() == doctest::Approx(1.0f));
    }
}
