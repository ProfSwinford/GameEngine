// ============================================================================
//  Tests for sprite animation - the flipbook that plays a spritesheet's own
//  stored animations. See engine/include/engine/scene/SpriteAnimation.h.
//
//  WHY THE TIMING IS A FREE FUNCTION, AND WHY THAT IS THE WHOLE POINT HERE.
//
//  There is no renderer in this binary, so not one frame of an animation can
//  be cut: ResourceManager::LoadTexture hands back the missing-texture
//  placeholder, and with no renderer even that is empty. Anything written
//  against "the component is showing frame 3" would be untestable.
//
//  FrameAt takes four numbers and answers which frame a flipbook is on. That
//  is where every awkward case lives - a rate of zero, a NaN out of a
//  hand-edited sidecar, an elapsed time of 1e30, the exact instant a
//  non-looping clip ends - and all of it is checkable with no window on
//  screen. The same argument SliceGrid makes in ImportSettings.h.
//
//  ==========================================================================
//  WHAT CANNOT BE TESTED HERE, so that nobody reads a gap as coverage.
//
//  No texture is ever created, so none of these are exercised:
//    * SpriteComponent::ShowTexture actually changing what is drawn
//    * the pinned frame vector keeping textures alive
//    * the authored picture being captured and put back
//    * the one-pointer-compare guard that skips most steps
//  Those were checked by running the player over four hundred frames and
//  reading the log: seven textures cut at the start and not one load line
//  afterwards, which is the only way "is the right picture on screen, and is
//  it being re-cut every time round?" can be answered.
//
//  WHICH FRAME IS SHOWING *CAN* BE TESTED, THOUGH, and the last section here
//  does it. m_shown is the component's own bookkeeping and ShowFrame sets it
//  whether or not the texture it pushed is real - so stepping a component on
//  a genuine entity and asking what frame it is on works with no renderer at
//  all. That is the difference between the picture (out of reach) and the
//  decision about which picture (not out of reach).
//
//  WHAT IS TESTED IS EVERYTHING THAT DECIDES *WHICH* FRAME: the timing, the
//  scene-file round trip, the resolution of an animation against a sheet -
//  including every way that resolution can fail, which is the part a user
//  actually meets - and the play/pause/stop/finish state machine.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/fs/FileSystem.h>
#include <engine/resource/ImportSettings.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/SpriteAnimation.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
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

// The same throwaway project the import tests use, for the same reason: the
// ImportLibrary cache is keyed on the VIRTUAL path, so two tests asking about
// "textures/hero.png" in different folders would answer each other's question.
struct Sandbox {
    fs::path   root;
    FileSystem fileSystem;

    explicit Sandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-anim-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets", ec);

        SetEnvironment("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
        ImportLibrary::Clear();
    }

    ~Sandbox() {
        ImportLibrary::Clear();
        fileSystem.Shutdown();
        SetEnvironment("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path real = fs::path(FileSystem::Resolve(virtualPath));
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }
};

// A sheet of four 16x16 frames with one animation over the first three.
void GiveSlicedSheet(const Sandbox& box) {
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.cellWidth  = 16;
    settings.sheet.cellHeight = 16;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 16, 16},
                                 SheetFrame{"f1", 16, 0, 16, 16},
                                 SheetFrame{"f2", 32, 0, 16, 16},
                                 SheetFrame{"f3", 48, 0, 16, 16}};
    // THREE CLIPS, AND "stroll" IS THE POINT OF THE THIRD: it is "walk" with
    // resume turned on, so one sheet holds a clip that carries on and a clip
    // that starts over. That pair is exactly what a setting on the COMPONENT
    // could not have expressed - one component plays both.
    settings.sheet.animations = {
        SheetAnimation{"walk", {"f0", "f1", "f2"}, 8.0f, true, /*resume=*/false},
        SheetAnimation{"die", {"f3"}, 4.0f, false, /*resume=*/false},
        SheetAnimation{"stroll", {"f0", "f1", "f2"}, 8.0f, true, /*resume=*/true},
    };

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));
}

// Attaches a component to nothing and loads it from JSON, which is what a
// scene file does one line later. Returns false the way Deserialize does.
bool LoadFrom(SpriteAnimationComponent& animation, const std::string& json,
              std::string& outError) {
    std::string parseError;
    const Json  node = ParseJson(json, parseError);
    REQUIRE(parseError.empty());
    return animation.Deserialize(node, outError);
}

} // namespace

// ---------------------------------------------------------------------------
//  FrameAt - the timing, which is all of the arithmetic
// ---------------------------------------------------------------------------

TEST_CASE("a looping flipbook advances one frame per frame-length and wraps") {
    // Three frames at 10 fps: each frame lasts a tenth of a second.
    CHECK(FrameAt(0.00f, 3, 10.0f, true).frame == 0);
    CHECK(FrameAt(0.05f, 3, 10.0f, true).frame == 0);
    CHECK(FrameAt(0.10f, 3, 10.0f, true).frame == 1);
    CHECK(FrameAt(0.25f, 3, 10.0f, true).frame == 2);

    // AND WRAPS RATHER THAN RUNNING OFF THE END: at 0.30 the clip has played
    // once and is back on frame 0.
    CHECK(FrameAt(0.30f, 3, 10.0f, true).frame == 0);
    CHECK(FrameAt(0.35f, 3, 10.0f, true).frame == 0);
    CHECK(FrameAt(0.45f, 3, 10.0f, true).frame == 1);

    // A LOOP IS NEVER FINISHED. That is what makes IsFinished a useful thing
    // for a script to poll.
    CHECK_FALSE(FrameAt(99.0f, 3, 10.0f, true).finished);
}

TEST_CASE("a non-looping flipbook holds its last frame and reports that it ended") {
    CHECK(FrameAt(0.00f, 3, 10.0f, false).frame == 0);
    CHECK(FrameAt(0.15f, 3, 10.0f, false).frame == 1);
    CHECK(FrameAt(0.25f, 3, 10.0f, false).frame == 2);

    // THE LAST FRAME GETS ITS OWN FRAME-LENGTH FIRST. Finishing at 0.20 - the
    // moment the last frame appears - would show it for no time at all, which
    // for a two-frame explosion is the whole second half of the explosion.
    CHECK_FALSE(FrameAt(0.25f, 3, 10.0f, false).finished);

    const FlipbookPosition ended = FrameAt(0.30f, 3, 10.0f, false);
    CHECK(ended.frame == 2);
    CHECK(ended.finished);

    // AND IT STAYS THERE. Nothing is destroyed and the index does not run on.
    const FlipbookPosition later = FrameAt(500.0f, 3, 10.0f, false);
    CHECK(later.frame == 2);
    CHECK(later.finished);
}

TEST_CASE("a nonsense frame rate holds the first frame instead of dividing by it") {
    // ImportSettings.h declines to refuse a nonsense rate on purpose and says
    // that whatever plays an animation has to cope, because a hand-edited
    // sidecar can still say 0. These are that coping, pinned.
    CHECK(FrameAt(5.0f, 3, 0.0f, true).frame == 0);
    CHECK(FrameAt(5.0f, 3, -8.0f, true).frame == 0);
    CHECK_FALSE(FrameAt(5.0f, 3, 0.0f, false).finished);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(FrameAt(5.0f, 3, nan, true).frame == 0);
    CHECK(FrameAt(nan, 3, 10.0f, true).frame == 0);

    // THE NaN CASE IS WHY THE TEST IS WRITTEN `!(x > 0)` AND NOT `x <= 0`.
    // Every comparison against a NaN is false, so `nan <= 0` would be false
    // and a NaN would fall through into the arithmetic and come out anywhere.
}

TEST_CASE("an absurd elapsed time still gives a frame in range") {
    // A cast of a value too large for the destination is undefined behaviour
    // rather than a large answer, so every path either compares first or goes
    // through fmod. 1e30 seconds is not reachable from a game, and is exactly
    // what a test should try.
    CHECK(FrameAt(1e30f, 5, 60.0f, true).frame < 5);
    CHECK(FrameAt(1e30f, 5, 60.0f, false).frame == 4);
    CHECK(FrameAt(1e30f, 5, 60.0f, false).finished);
}

TEST_CASE("a flipbook with no frames asks for frame zero and never finishes") {
    // The component returns early on an empty list, so this is belt - but it
    // is the one input where "frame 0" names a frame that does not exist, and
    // a caller that indexed on it regardless would read off the end.
    const FlipbookPosition none = FrameAt(1.0f, 0, 10.0f, false);
    CHECK(none.frame == 0);
    CHECK_FALSE(none.finished);
}

TEST_CASE("one frame at any rate is frame zero, and ends if it does not loop") {
    CHECK(FrameAt(0.0f, 1, 4.0f, false).frame == 0);
    CHECK_FALSE(FrameAt(0.1f, 1, 4.0f, false).finished);
    CHECK(FrameAt(0.3f, 1, 4.0f, false).finished);
    CHECK(FrameAt(99.0f, 1, 4.0f, true).frame == 0);
}

// ---------------------------------------------------------------------------
//  The scene file
// ---------------------------------------------------------------------------

TEST_CASE("an animation component round-trips through the scene file") {
    SpriteAnimationComponent animation;
    std::string              error;

    REQUIRE(LoadFrom(animation,
                     R"({ "animation": "textures/hero.png#walk",
                          "speed": 0.5, "playOnStart": false })", error));
    CHECK(error.empty());
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");
    CHECK(animation.Speed() == doctest::Approx(0.5));
    CHECK_FALSE(animation.PlayOnStart());

    Json out;
    REQUIRE(animation.Serialize(out));
    CHECK(out["animation"] == "textures/hero.png#walk");
    CHECK(out["speed"] == doctest::Approx(0.5));
    CHECK(out["playOnStart"] == false);
}

TEST_CASE("the defaults are written as nothing at all") {
    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    CHECK(animation.Speed() == doctest::Approx(1.0));
    CHECK(animation.PlayOnStart());

    // OMITTED RATHER THAN WRITTEN, so an ordinary animation is one line in the
    // scene file and a prefab override has one leaf instead of three.
    Json out;
    REQUIRE(animation.Serialize(out));
    CHECK(out.contains("animation"));
    CHECK_FALSE(out.contains("speed"));
    CHECK_FALSE(out.contains("playOnStart"));
}

TEST_CASE("the state Add Component produces loads without complaining") {
    // Pressing "Add Component" in the Inspector makes exactly this, and a
    // component that refused to load the editor's own starting state would
    // make the row unusable.
    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ })", error));
    CHECK(error.empty());
    CHECK(animation.AnimationPath().empty());
}

TEST_CASE("a speed of zero or less is refused by name and falls back to one") {
    SpriteAnimationComponent animation;
    std::string              error;

    CHECK_FALSE(LoadFrom(animation,
                         R"({ "animation": "textures/hero.png#walk", "speed": 0 })",
                         error));
    CHECK(error.find("speed") != std::string::npos);

    // THE COMPONENT IS STILL USABLE. Scene loading keeps a component whose
    // Deserialize returned false and reports the message, so falling back to a
    // working value matters more than refusing.
    CHECK(animation.Speed() == doctest::Approx(1.0));
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");
}

// ---------------------------------------------------------------------------
//  Resolving against the sheet - and every way it goes wrong
// ---------------------------------------------------------------------------

TEST_CASE("an animation resolves to the frames the sheet still has") {
    const Sandbox box("resolve");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    CHECK(animation.ResolvedFrames().size() == 3);
    CHECK(animation.FramesListed() == 3);
    CHECK(animation.SheetFps() == doctest::Approx(8.0));
    CHECK(animation.Loops());
    CHECK(animation.SheetPath() == "textures/hero.png");
    CHECK(animation.AnimationName() == "walk");
    CHECK(animation.Problem().empty());

    // The rectangles come with the names, which is what lets a re-slice be
    // told from an unrelated save.
    CHECK(animation.ResolvedFrames()[1].name == "f1");
    CHECK(animation.ResolvedFrames()[1].x == 16);
}

TEST_CASE("a non-looping animation reports the sheet's own loop flag") {
    const Sandbox box("resolve-once");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#die" })", error));

    CHECK(animation.ResolvedFrames().size() == 1);
    CHECK_FALSE(animation.Loops());
    CHECK(animation.SheetFps() == doctest::Approx(4.0));
}

TEST_CASE("a frame renamed away makes the animation shorter, not broken") {
    const Sandbox box("resolve-short");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 8, 8},
                                 SheetFrame{"f2", 16, 0, 8, 8}};
    // "f1" is listed and no longer exists, which is what renaming a frame in
    // the slicer leaves behind.
    settings.sheet.animations = {SheetAnimation{"walk", {"f0", "f1", "f2"}, 8.0f, true}};
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));

    SpriteAnimationComponent animation;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    // SKIPPED, AND THE TWO COUNTS DIFFER so the Inspector can say so. It still
    // plays, which is why this is a warning rather than a Problem().
    CHECK(animation.ResolvedFrames().size() == 2);
    CHECK(animation.FramesListed() == 3);
    CHECK(animation.Problem().empty());
    CHECK(animation.ResolvedFrames()[0].name == "f0");
    CHECK(animation.ResolvedFrames()[1].name == "f2");
}

TEST_CASE("an animation whose every frame is gone says so rather than playing nothing") {
    const Sandbox box("resolve-empty");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 8, 8}};
    settings.sheet.animations = {SheetAnimation{"walk", {"old1", "old2"}, 8.0f, true}};
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));

    SpriteAnimationComponent animation;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    CHECK(animation.ResolvedFrames().empty());
    CHECK_FALSE(animation.Problem().empty());
    CHECK(animation.Problem().find("walk") != std::string::npos);
}

TEST_CASE("pointing it at a FRAME says which mistake was made") {
    const Sandbox box("resolve-frame");
    GiveSlicedSheet(box);

    // A frame and an animation are the same shape of path, so this is the
    // likely mistake - and "no animation called f0" would be a worse answer
    // than "f0 is a frame, not an animation".
    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#f0" })", error));

    CHECK(animation.ResolvedFrames().empty());
    CHECK(animation.Problem().find("frame") != std::string::npos);
}

TEST_CASE("pointing it at a whole image is refused, and says that") {
    const Sandbox box("resolve-image");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png" })", error));

    CHECK(animation.ResolvedFrames().empty());
    CHECK_FALSE(animation.Problem().empty());

    // AND SetAnimationPath REFUSES ONE OUTRIGHT, leaving the old clip alone
    // rather than half-replacing it.
    SpriteAnimationComponent other;
    REQUIRE(LoadFrom(other, R"({ "animation": "textures/hero.png#walk" })", error));
    REQUIRE(other.ResolvedFrames().size() == 3);
    CHECK_FALSE(other.SetAnimationPath("textures/hero.png"));
    CHECK(other.AnimationPath() == "textures/hero.png#walk");
    CHECK(other.ResolvedFrames().size() == 3);
}

TEST_CASE("an unsliced sheet says it has not been sliced") {
    const Sandbox box("resolve-unsliced");
    box.Write("textures/hero.png", "not really pixels");

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    CHECK(animation.ResolvedFrames().empty());
    CHECK(animation.Problem().find("sliced") != std::string::npos);
}

TEST_CASE("an empty path is not a problem, because that is what Add Component makes") {
    const Sandbox box("resolve-empty-path");

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ })", error));

    CHECK(animation.ResolvedFrames().empty());

    // NOT a Problem(): there is nothing wrong with a component somebody has
    // just added and not yet pointed at anything. The Inspector asks for one
    // in its own words instead.
    CHECK(animation.Problem().empty());
}

// ---------------------------------------------------------------------------
//  Re-resolving when the sheet changes underneath
// ---------------------------------------------------------------------------

TEST_CASE("the import generation moves on every write and not on a read") {
    const Sandbox box("generation");
    box.Write("textures/hero.png", "not really pixels");

    const std::uint64_t start = ImportLibrary::Generation();

    // A READ DOES NOT MOVE IT. Reading the disk is not a change, and bumping
    // here would make every holder re-resolve on every first ask.
    (void)ImportLibrary::ForTexture("textures/hero.png");
    CHECK(ImportLibrary::Generation() == start);

    TextureImport settings;
    settings.filter = TextureFilter::Linear;
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));
    const std::uint64_t afterSave = ImportLibrary::Generation();
    CHECK(afterSave > start);

    REQUIRE(ImportLibrary::Revert("textures/hero.png", error));
    CHECK(ImportLibrary::Generation() > afterSave);

    const std::uint64_t afterRevert = ImportLibrary::Generation();
    ImportLibrary::Clear();
    CHECK(ImportLibrary::Generation() > afterRevert);
}

TEST_CASE("re-slicing a sheet is picked up by an animation already resolved against it") {
    const Sandbox box("re-slice");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));
    REQUIRE(animation.ResolvedFrames().size() == 3);
    CHECK(animation.SheetFps() == doctest::Approx(8.0));

    // Re-slice: one fewer frame in the animation, and a different rate. This
    // is what pressing Slice again, or editing the animation, does.
    TextureImport changed;
    changed.sheet.frames     = {SheetFrame{"f0", 0, 0, 16, 16},
                                SheetFrame{"f1", 16, 0, 16, 16}};
    changed.sheet.animations = {SheetAnimation{"walk", {"f0", "f1"}, 24.0f, false}};
    REQUIRE(ImportLibrary::Save("textures/hero.png", changed, error));

    // ASKED AGAIN WITH NO PROMPTING. The component holds the generation it
    // resolved at; Save moved it, so the next ask re-reads. Without this the
    // animation would play the old rectangles for ever, because
    // InvalidateSubAssets cannot reach a texture something already holds.
    CHECK(animation.ResolvedFrames().size() == 2);
    CHECK(animation.SheetFps() == doctest::Approx(24.0));
    CHECK_FALSE(animation.Loops());
}

TEST_CASE("an unrelated save costs a re-read but changes no frame") {
    const Sandbox box("unrelated-save");
    GiveSlicedSheet(box);
    box.Write("textures/other.png", "not really pixels");

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));
    const std::vector<SheetFrame> before = animation.ResolvedFrames();
    REQUIRE(before.size() == 3);

    TextureImport other;
    other.filter = TextureFilter::Linear;
    REQUIRE(ImportLibrary::Save("textures/other.png", other, error));

    // THE COUNTER IS DELIBERATELY NOT PER ASSET: a holder that re-reads
    // because an unrelated image was saved pays one map lookup and one
    // comparison, and a holder that MISSES a change shows the wrong picture
    // with nothing to explain it. The frames must come back identical, which
    // is what makes the cheap mistake the right one to allow.
    CHECK(animation.ResolvedFrames() == before);
}

TEST_CASE("Play by name finds an animation in the sheet this component already knows") {
    const Sandbox box("play-by-name");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));
    REQUIRE(animation.ResolvedFrames().size() == 3);

    // A NAME, NOT A PATH. The sheet is the component's own, so a script does
    // not repeat it at every call site and does not break when it is renamed.
    REQUIRE(animation.Play("die"));
    CHECK(animation.AnimationPath() == "textures/hero.png#die");
    CHECK(animation.ResolvedFrames().size() == 1);
    CHECK(animation.IsPlaying());
    CHECK_FALSE(animation.IsFinished());

    // A name the sheet does not have is refused, with the sheet and the name
    // in the message, and the clip that was playing is left alone.
    CHECK_FALSE(animation.Play("nope"));
    CHECK(animation.AnimationPath() == "textures/hero.png#die");
    CHECK(animation.Problem().find("nope") != std::string::npos);
}

TEST_CASE("Play through an AnimationRef absorbs the name-or-path decision") {
    const Sandbox box("play-by-ref");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));
    REQUIRE(animation.ResolvedFrames().size() == 3);

    // ---- A PATH NAMING THIS SHEET takes the cheap branch ------------------
    //
    // The same work Play("die") does - the frames already cut are kept and
    // the clip is pre-validated - and a script no longer has to know that.
    REQUIRE(animation.Play(eng::AnimationRef{"textures/hero.png#die"}));
    CHECK(animation.AnimationPath() == "textures/hero.png#die");
    CHECK(animation.ResolvedFrames().size() == 1);

    // ---- A BARE NAME still works -----------------------------------------
    //
    // Which is what the text box this type replaces produced, and what a
    // scene saved before the type existed still holds.
    REQUIRE(animation.Play(eng::AnimationRef{"walk"}));
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");
    CHECK(animation.ResolvedFrames().size() == 3);

    // ---- AN UNSET REFERENCE DOES NOTHING, AND SAYS NOTHING ---------------
    //
    // Deliberately unlike a missing prefab, which is always a mistake. A
    // blank animation slot is a direction a character does not have - a
    // side-on sprite has no up or down - so the clip that was playing is
    // left alone and no problem is recorded.
    const std::string before = animation.Problem();
    CHECK_FALSE(animation.Play(eng::AnimationRef{}));
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");
    CHECK(animation.Problem() == before);

    // ---- A CLIP THE SHEET DOES NOT HAVE is refused, by name --------------
    CHECK_FALSE(animation.Play(eng::AnimationRef{"textures/hero.png#nope"}));
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");
    CHECK(animation.Problem().find("nope") != std::string::npos);

    // ---- ANOTHER SHEET pays for the whole path, and is asked to play -----
    //
    // SetAnimationPath honours the "play on start" tick, which is the
    // authored intent for the FIRST clip and not for this one, so the
    // overload asks it to play straight after. Without that an entity set
    // not to play on start would change direction and stand still.
    box.Write("textures/boss.png", "not really pixels");
    TextureImport other;
    other.sheet.cellWidth  = 16;
    other.sheet.cellHeight = 16;
    other.sheet.frames     = {SheetFrame{"g0", 0, 0, 16, 16}};
    other.sheet.animations = {
        SheetAnimation{"roar", {"g0"}, 6.0f, true, /*resume=*/false}};
    REQUIRE(ImportLibrary::Save("textures/boss.png", other, error));

    REQUIRE(animation.Play(eng::AnimationRef{"textures/boss.png#roar"}));
    CHECK(animation.AnimationPath() == "textures/boss.png#roar");
    CHECK(animation.IsPlaying());
}

TEST_CASE("Play through an AnimationRef needs no resolve to know its own sheet") {
    // ======================================================================
    //  THE BUG THIS OVERLOAD EXISTS TO STOP BEING WRITTEN AGAIN.
    //
    //  SheetPath() is EMPTY until something has asked ResolvedFrames(), and
    //  on an entity that is not playing nothing has - Step returns before it.
    //  A caller comparing a wanted path against SheetPath() therefore always
    //  missed, and took the expensive re-read-the-whole-sheet branch on every
    //  single call. assets/scripts/TilemapWalker.cpp did exactly that, under
    //  a comment explaining that it was avoiding that cost.
    //
    //  This overload compares against SheetPathOf(AnimationPath()) instead.
    //  AnimationPath() is a plain member, so it is answerable with no resolve
    //  and no side effect at all.
    // ======================================================================
    const Sandbox box("play-by-ref-unresolved");
    GiveSlicedSheet(box);

    SpriteAnimationComponent animation;
    std::string              error;
    REQUIRE(LoadFrom(animation, R"({ "animation": "textures/hero.png#walk" })", error));

    // THE STATE THE BUG NEEDED: loaded, knows its path, has resolved nothing.
    CHECK(animation.SheetPath().empty());
    CHECK(animation.AnimationPath() == "textures/hero.png#walk");

    // Same sheet, so this must be the CHEAP branch even from here.
    REQUIRE(animation.Play(eng::AnimationRef{"textures/hero.png#die"}));
    CHECK(animation.AnimationPath() == "textures/hero.png#die");
    CHECK(animation.ResolvedFrames().size() == 1);
}

// ---------------------------------------------------------------------------
//  Playback - the state machine, driven on a real entity
// ---------------------------------------------------------------------------

namespace {

// An entity with a sprite and an animation on it, and a way to step it.
//
// WHY THIS NEEDS NO RENDERER: see the note at the top of the file. Which frame
// is showing is bookkeeping; what that frame looks like is not.
struct Played {
    Sandbox                   box;
    Scene                     scene;
    Entity*                   entity = nullptr;
    SpriteAnimationComponent* anim   = nullptr;

    Played(const char* name, const char* animationPath, bool withSprite = true)
        : box(name) {
        GiveSlicedSheet(box);

        // ==================================================================
        //  WHAT Engine::Init DOES, and this binary never runs it: Add<T>
        //  builds components through the factory, by type name.
        //
        //  BOTH OF THESE, and the first one was missing. RegisterBuiltins is
        //  where the factory learns SpriteComponent - so without it Add
        //  returned NULLPTR below and this fixture dereferenced it.
        //
        //  The file passed anyway, every time, because some earlier test file
        //  had already registered the builtins into the same global factory.
        //  Run this one on its own and it segfaulted. A test that only works
        //  because of what ran before it is not testing what it says it is.
        // ==================================================================
        ComponentFactory::RegisterBuiltins();
        SpriteAnimationSystem::RegisterComponentTypes();
        Scene::SetActive(&scene);

        entity = scene.Get(scene.CreateEntity("Hero"));
        REQUIRE(entity != nullptr);

        if (withSprite) {
            // CHECKED, NOT ASSUMED. A registration that goes missing again
            // should fail here by name rather than crash two lines later.
            SpriteComponent* sprite = entity->Add<SpriteComponent>();
            REQUIRE(sprite != nullptr);

            // A DIFFERENT FRAME FROM ANY THE ANIMATION USES, so "the picture
            // the scene file asked for" is distinguishable from the ones the
            // flipbook pushes.
            sprite->SetTexture("textures/hero.png#f3");
        }

        anim = entity->Add<SpriteAnimationComponent>();
        REQUIRE(anim != nullptr);
        REQUIRE(anim->SetAnimationPath(animationPath));
    }

    ~Played() {
        scene.Unload();
        Scene::SetActive(nullptr);
        SpriteAnimationSystem::Clear();
    }

    // Sixty a second, which is the fixed step the engine hands down.
    void Steps(int count) const {
        for (int i = 0; i < count; ++i) {
            anim->Step(1.0f / 60.0f);
        }
    }
};

} // namespace

TEST_CASE("a looping animation walks through its frames and comes round again") {
    const Played played("step-loop", "textures/hero.png#walk");

    // NOTHING IS SHOWING UNTIL THE FIRST STEP. The sprite keeps the picture
    // its own "texture" names, which is what makes an animation safe to add to
    // something in the editor without the view changing under you.
    CHECK(played.anim->CurrentFrame() == SpriteAnimationComponent::kAuthoredFrame);
    CHECK(played.anim->CurrentFrameName().empty());
    CHECK(played.anim->IsPlaying());   // playOnStart defaults to true

    // Three frames at 8 fps: an eighth of a second each. The step counts below
    // are deliberately off the exact boundaries - 8 steps is 1.07 frames, not
    // 1.00 - because an elapsed time built by adding 1/60 sixteen times is not
    // exactly 16/60, and a test should not be asserting on the last bit of a
    // float.
    played.Steps(1);
    CHECK(played.anim->CurrentFrame() == 0);
    CHECK(played.anim->CurrentFrameName() == "f0");

    played.Steps(7);   // 8 total
    CHECK(played.anim->CurrentFrame() == 1);
    CHECK(played.anim->CurrentFrameName() == "f1");

    played.Steps(8);   // 16 total
    CHECK(played.anim->CurrentFrame() == 2);
    CHECK(played.anim->CurrentFrameName() == "f2");

    // AND ROUND AGAIN rather than stopping on the last one.
    played.Steps(8);   // 24 total
    CHECK(played.anim->CurrentFrame() == 0);
    CHECK(played.anim->IsPlaying());
    CHECK_FALSE(played.anim->IsFinished());
}

TEST_CASE("a non-looping animation stops on its last frame and stays there") {
    const Played played("step-once", "textures/hero.png#die");

    // 'die' is one frame at 4 fps, so it ends a quarter of a second in.
    played.Steps(10);   // 0.167s
    CHECK(played.anim->CurrentFrame() == 0);
    CHECK(played.anim->CurrentFrameName() == "f3");
    CHECK(played.anim->IsPlaying());
    CHECK_FALSE(played.anim->IsFinished());

    played.Steps(10);   // 0.333s - past the end
    CHECK(played.anim->IsFinished());
    CHECK_FALSE(played.anim->IsPlaying());

    // STILL ON ITS LAST FRAME. Nothing is destroyed and nothing is hidden;
    // that is what "played once" leaves behind.
    CHECK(played.anim->CurrentFrame() == 0);
    CHECK(played.anim->CurrentFrameName() == "f3");

    // AND IT STAYS FINISHED, so a script can poll it rather than having to
    // catch the one frame it ended on.
    played.Steps(600);
    CHECK(played.anim->IsFinished());
    CHECK(played.anim->CurrentFrame() == 0);

    // Play on something that had finished starts it again, rather than doing
    // nothing because it is already at the end.
    played.anim->Play();
    CHECK(played.anim->IsPlaying());
    CHECK_FALSE(played.anim->IsFinished());
}

TEST_CASE("Pause holds the frame on screen and Play carries on from there") {
    const Played played("step-pause", "textures/hero.png#walk");

    played.Steps(8);
    CHECK(played.anim->CurrentFrame() == 1);

    played.anim->Pause();
    CHECK_FALSE(played.anim->IsPlaying());

    // A WHOLE SECOND OF STEPS CHANGES NOTHING, which is the difference between
    // pausing and hiding: the frame it was on is still the frame on screen.
    played.Steps(60);
    CHECK(played.anim->CurrentFrame() == 1);
    CHECK(played.anim->CurrentFrameName() == "f1");

    // CARRIES ON rather than restarting - 8 more steps from 8 is frame 2, not
    // frame 1 again.
    played.anim->Play();
    CHECK(played.anim->IsPlaying());
    played.Steps(8);
    CHECK(played.anim->CurrentFrame() == 2);
}

TEST_CASE("Stop rewinds and hands the sprite its own picture back") {
    const Played played("step-stop", "textures/hero.png#walk");

    played.Steps(8);
    REQUIRE(played.anim->CurrentFrame() == 1);

    played.anim->Stop();

    // THE AUTHORED FRAME, which is the one state that means "this animation is
    // not showing anything of its own".
    CHECK(played.anim->CurrentFrame() == SpriteAnimationComponent::kAuthoredFrame);
    CHECK(played.anim->CurrentFrameName().empty());
    CHECK_FALSE(played.anim->IsPlaying());
    CHECK_FALSE(played.anim->IsFinished());

    // REWOUND, not paused: the next play starts at the beginning.
    played.anim->Play();
    played.Steps(1);
    CHECK(played.anim->CurrentFrame() == 0);
}

TEST_CASE("an animation that does not play on start costs nothing until it does") {
    Played played("step-not-on-start", "textures/hero.png#walk");

    std::string error;
    REQUIRE(LoadFrom(*played.anim,
                     R"({ "animation": "textures/hero.png#walk",
                          "playOnStart": false })",
                     error));
    played.anim->OnAttach();   // what the scene does once the fields are in

    CHECK_FALSE(played.anim->IsPlaying());

    // NOT EVEN RESOLVED YET, and that is worth a test of its own: a component
    // that is waiting for a script to start it has not read the sheet, cut a
    // frame or loaded a texture. Observed in the player: an entity set not to
    // play on start contributed no texture loads at all.
    CHECK(played.anim->FramesListed() == 0);

    played.Steps(600);   // ten seconds of nothing
    CHECK(played.anim->CurrentFrame() == SpriteAnimationComponent::kAuthoredFrame);
    CHECK(played.anim->FramesListed() == 0);

    played.anim->Play();
    played.Steps(1);
    CHECK(played.anim->CurrentFrame() == 0);
    CHECK(played.anim->FramesListed() == 3);
}

TEST_CASE("the system steps the animations registered with it") {
    const Played played("step-system", "textures/hero.png#walk");

    // REGISTERED BY OnAttach, which is what the system walks. One entity, one
    // animation.
    CHECK(SpriteAnimationSystem::Count() == 1);

    // Driven through the system rather than by hand, so that the registration
    // and the hand-off of the fixed step are both exercised. This is the path
    // the engine actually uses - and it is NOT gated on play mode, because the
    // player never enters it.
    SpriteAnimationSystem system;
    for (int i = 0; i < 8; ++i) {
        system.Update(1.0f / 60.0f);
    }
    CHECK(played.anim->CurrentFrame() == 1);
    CHECK(played.anim->CurrentFrameName() == "f1");
}

// ---------------------------------------------------------------------------
//  Carrying on, or starting over
// ---------------------------------------------------------------------------

TEST_CASE("a clip says whether playing it again carries on or starts over") {
    const Sandbox box("resume-flag");
    GiveSlicedSheet(box);

    SpriteAnimationComponent starts;
    std::string              error;
    REQUIRE(LoadFrom(starts, R"({ "animation": "textures/hero.png#walk" })", error));
    REQUIRE(starts.ResolvedFrames().size() == 3);
    CHECK_FALSE(starts.Resumes());

    SpriteAnimationComponent carries;
    REQUIRE(LoadFrom(carries, R"({ "animation": "textures/hero.png#stroll" })", error));
    REQUIRE(carries.ResolvedFrames().size() == 3);
    CHECK(carries.Resumes());
}

TEST_CASE("playing the clip again starts it over when it says to") {
    const Played played("resume-off", "textures/hero.png#walk");

    played.Steps(8);
    REQUIRE(played.anim->CurrentFrame() == 1);

    played.anim->Pause();
    REQUIRE(played.anim->Play("walk"));
    CHECK(played.anim->IsPlaying());

    // ONE STEP BEFORE ASKING, and that is the component's contract rather
    // than a convenience here: Play rewinds the CLOCK, and which frame is on
    // the sprite changes when the clip is next stepped. Asking in between
    // reports the frame that is still on screen, which is the one it had.
    played.Steps(1);

    // BACK TO THE BEGINNING, which is what every clip did before the flag
    // existed and still what one without it does.
    CHECK(played.anim->CurrentFrame() == 0);
}

TEST_CASE("playing the clip again carries on when it says to") {
    const Played played("resume-on", "textures/hero.png#stroll");

    played.Steps(8);
    REQUIRE(played.anim->CurrentFrame() == 1);

    played.anim->Pause();
    CHECK_FALSE(played.anim->IsPlaying());

    REQUIRE(played.anim->Play("stroll"));
    CHECK(played.anim->IsPlaying());

    // STILL ON THE FRAME IT STOPPED ON after a step - where the clip above
    // went back to its first. This is the hitch a walk cycle used to have
    // every time somebody stopped and set off again.
    played.Steps(1);
    CHECK(played.anim->CurrentFrame() == 1);

    // And it goes ON from there rather than from the start: 8 more steps from
    // 9 is frame 2, where a restart would still be on frame 1.
    played.Steps(8);
    CHECK(played.anim->CurrentFrame() == 2);
}

TEST_CASE("switching to a DIFFERENT clip always starts it at the beginning") {
    // ======================================================================
    //  THE LIMIT OF THE FLAG, AND WHY IT IS GUARDED ON THE PATH.
    //
    //  There is one playback position per component. Switching animations has
    //  nothing to carry on FROM - the position belonged to the clip being
    //  left - so a clip that resumes still begins at its first frame when it
    //  is the one being switched TO.
    // ======================================================================
    const Played played("resume-switch", "textures/hero.png#walk");

    played.Steps(16);
    REQUIRE(played.anim->CurrentFrame() == 2);

    REQUIRE(played.anim->Play("stroll"));   // resume is ON for this one
    CHECK(played.anim->AnimationPath() == "textures/hero.png#stroll");

    played.Steps(1);
    CHECK(played.anim->CurrentFrame() == 0);
}

TEST_CASE("a clip that has finished starts again even when it carries on") {
    // Resuming a clip that ran to its end would leave it on its last frame for
    // ever, which is not "carrying on" by any reading. Play() already draws
    // that distinction and Play(name) leans on it rather than restating it.
    const Sandbox box("resume-finished");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.cellWidth  = 16;
    settings.sheet.cellHeight = 16;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 16, 16},
                                 SheetFrame{"f1", 16, 0, 16, 16}};

    // Two frames, 4 fps, does NOT loop, and carries on when replayed.
    settings.sheet.animations = {
        SheetAnimation{"swing", {"f0", "f1"}, 4.0f, false, /*resume=*/true}};

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));

    Scene scene;
    ComponentFactory::RegisterBuiltins();
    SpriteAnimationSystem::RegisterComponentTypes();
    Scene::SetActive(&scene);

    Entity* entity = scene.Get(scene.CreateEntity("Hero"));
    REQUIRE(entity != nullptr);
    SpriteComponent* sprite = entity->Add<SpriteComponent>();
    REQUIRE(sprite != nullptr);
    sprite->SetTexture("textures/hero.png#f0");

    SpriteAnimationComponent* anim = entity->Add<SpriteAnimationComponent>();
    REQUIRE(anim != nullptr);
    REQUIRE(anim->SetAnimationPath("textures/hero.png#swing"));

    for (int i = 0; i < 60; ++i) {   // a whole second: well past the end
        anim->Step(1.0f / 60.0f);
    }
    REQUIRE(anim->IsFinished());
    REQUIRE(anim->CurrentFrame() == 1);

    REQUIRE(anim->Play("swing"));
    CHECK_FALSE(anim->IsFinished());
    CHECK(anim->IsPlaying());

    anim->Step(1.0f / 60.0f);
    CHECK(anim->CurrentFrame() == 0);   // the beginning, not the end it was on

    scene.Unload();
    Scene::SetActive(nullptr);
    SpriteAnimationSystem::Clear();
}

TEST_CASE("the flag survives the sidecar, and an old sidecar reads as 'start over'") {
    const Sandbox box("resume-roundtrip");
    GiveSlicedSheet(box);

    // WRITTEN AND READ BACK, because a setting that does not survive the file
    // is a setting somebody has to set again every time they open the project.
    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    const SheetAnimation* stroll = read.sheet.FindAnimation("stroll");
    REQUIRE(stroll != nullptr);
    CHECK(stroll->resume);

    const SheetAnimation* walk = read.sheet.FindAnimation("walk");
    REQUIRE(walk != nullptr);
    CHECK_FALSE(walk->resume);
}

TEST_CASE("a sidecar written before this existed has no resume key and starts over") {
    // EVERY PROJECT ON DISK IS IN THIS STATE, so the absence has to mean the
    // old behaviour rather than something new.
    const Sandbox box("resume-absent");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport", R"({
  "sheet": {
    "cellWidth": 16,
    "cellHeight": 16,
    "frames": [ { "name": "f0", "x": 0, "y": 0, "w": 16, "h": 16 } ],
    "animations": [ { "name": "old", "frames": ["f0"], "fps": 8.0, "loop": true } ]
  }
})");

    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    const SheetAnimation* old = read.sheet.FindAnimation("old");
    REQUIRE(old != nullptr);
    CHECK_FALSE(old->resume);
    CHECK(old->loop);
    CHECK(old->fps == doctest::Approx(8.0f));
}
