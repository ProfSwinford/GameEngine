// ============================================================================
//  Tests for ImportLibrary - the .oimport files that say how an asset is
//  loaded. See engine/include/engine/resource/ImportSettings.h.
//
//  WHY THIS IS WORTH TESTING
//  Almost everything here is about a file that is NOT THERE. The defaults are
//  what the engine did before import settings existed, and the whole claim of
//  the feature is that a project with no .oimport files behaves exactly as it
//  did - so "no file" is not an edge case here, it is the main case, and a
//  default that quietly changed would alter how every existing sprite in every
//  existing project is drawn.
//
//  The rest is bookkeeping that fails silently when it fails at all: a sidecar
//  left behind by a rename is somebody's settings lost plus an orphan file
//  that nothing will ever mention again.
//
//  ==========================================================================
//  WHAT CANNOT BE TESTED HERE, so that nobody reads a gap as coverage.
//
//  No texture is ever created. ResourceManager::LoadTexture needs a renderer -
//  "there is no renderer yet, so textures cannot be created" - and these tests
//  have no window and no renderer, so LoadTexture hands back an empty
//  reference and no Texture object exists at all.
//
//  So the three places the settings are actually APPLIED are not exercised:
//  the SDL_SetTextureScaleMode call in ResourceManager::CreateTextureFromFile,
//  Renderer::SetTextureFilter and its refusal of the shared placeholder, and
//  ResourceManager::RefreshImportSettings walking the font cache. Those were
//  checked by running the editor and looking at the picture, which is the only
//  way "is this image blurry?" can be checked, and the Inspector shows what
//  SDL actually has rather than what the file asked for so that the two can be
//  seen to agree.
//
//  WHAT IS TESTED IS EVERYTHING THAT DECIDES *WHICH* SETTINGS APPLY: the
//  defaults, the path derivation, the reading, the writing, and the three
//  operations that keep a sidecar beside its asset.
//
//  ==========================================================================
//  EVERY TEST RUNS IN A THROWAWAY PROJECT, not in this one - see the note in
//  test_assets.cpp, which this file's Sandbox is copied from. It has one extra
//  job, and it is load-bearing; see the comment on it.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/AssetArchive.h>
#include <engine/fs/FileSystem.h>
#include <engine/resource/ImportSettings.h>

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
//  A complete throwaway project, with FileSystem pointed at it for as long as
//  this object lives.
//
//  AND IT CLEARS ImportLibrary'S CACHE AT BOTH ENDS, which is not tidiness.
//  That cache is keyed on the VIRTUAL path - "textures/hero.png" - and every
//  sandbox in this file uses the same virtual paths inside a different real
//  folder. Without the clear, the second test to ask about "textures/hero.png"
//  would be answered from the first test's project, and the failure would land
//  in whichever test happened to run second rather than in the one at fault.
//
//  The same reasoning is why ResourceManager::Shutdown clears it in the engine:
//  one project's "textures/hero.png" is not another's.
// ---------------------------------------------------------------------------
struct Sandbox {
    fs::path   root;
    FileSystem fileSystem;

    explicit Sandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-import-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);           // left over from a crashed run
        fs::create_directories(root / "assets", ec);

        SetEnvironment("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
        ImportLibrary::Clear();
    }

    ~Sandbox() {
        AssetArchive::UnmountAll();
        ImportLibrary::Clear();
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
};

// An image and a settings file for it, which is the starting point of most of
// the tests below.
void GiveSettings(const Sandbox& box, const std::string& asset, const char* filter) {
    box.Write(asset, "not really pixels");
    box.Write(ImportLibrary::SidecarPath(asset),
              std::string("{\n  \"filter\": \"") + filter + "\"\n}\n");
    ImportLibrary::Clear();   // so the write is read back rather than cached
}

} // namespace

// ---------------------------------------------------------------------------
//  The defaults - the claim that nothing changes until somebody asks
// ---------------------------------------------------------------------------

TEST_CASE("an image with no settings file is scaled the way the engine always scaled one") {
    const Sandbox box("default-texture");
    box.Write("textures/hero.png", "not really pixels");

    // NEAREST, which is the literal that used to be written into
    // ResourceManager::CreateTextureFromFile for every image it loaded. If
    // this ever fails, every sprite in every existing project has changed
    // appearance.
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);

    // AND ASKING CREATED NOTHING. A read that writes a file would turn opening
    // a project into a change to it, and leave a sidecar beside every image
    // anybody ever looked at.
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK_FALSE(ImportLibrary::HasSettings("textures/hero.png"));
}

TEST_CASE("a font with no settings file is still smoothed") {
    const Sandbox box("default-font");
    box.Write("fonts/ui.ttf", "not really a font");

    // LINEAR, which is the `smooth = true` the atlas baker used to pass. A
    // letter is a curve approximated into coverage values, and drawing those
    // unfiltered throws away what makes it look like a curve.
    CHECK(ImportLibrary::ForFont("fonts/ui.ttf").filter == TextureFilter::Linear);
}

TEST_CASE("an image and a font default to opposite scaling, deliberately") {
    const Sandbox box("opposite-defaults");

    // Stated as its own test because it looks like a bug when you first meet
    // it. The two kinds of asset want different things for reasons written out
    // beside each of them, and a shared default would be silently wrong for
    // one of them.
    CHECK(ImportLibrary::ForTexture("a.png").filter !=
          ImportLibrary::ForFont("a.ttf").filter);
}

// ---------------------------------------------------------------------------
//  Where the file goes
// ---------------------------------------------------------------------------

TEST_CASE("the settings file sits beside the asset and keeps the whole filename") {
    CHECK(ImportLibrary::SidecarPath("textures/hero.png") ==
          "textures/hero.png.oimport");
}

TEST_CASE("two images differing only in extension cannot share a settings file") {
    // THE NEAR MISS THE NAMING SCHEME EXISTS FOR. A "hero.oimport" scheme
    // would hand both of these the same file, and whichever was imported
    // second would take the other's settings.
    CHECK(ImportLibrary::SidecarPath("textures/logo.png") !=
          ImportLibrary::SidecarPath("textures/logo.jpg"));
}

TEST_CASE("a name with two dots keeps both of them") {
    // The bug a std::filesystem::path::replace_extension implementation would
    // have: it strips the last extension, so "hero.idle.png" would become
    // "hero.idle.oimport" - which is also what "hero.idle.bmp" would become.
    CHECK(ImportLibrary::SidecarPath("textures/hero.idle.png") ==
          "textures/hero.idle.png.oimport");
}

TEST_CASE("a file with no extension gets a settings file too") {
    CHECK(ImportLibrary::SidecarPath("textures/hero") == "textures/hero.oimport");
    CHECK(ImportLibrary::SidecarPath("README") == "README.oimport");
}

TEST_CASE("a settings file is recognised whatever case its extension arrives in") {
    // Nobody controls the case of a file name reliably, and this decides
    // whether the Assets panel hides one. See FileSystem::HasExtension, which
    // exists because a scene saved as .OSCENE was visible to one half of the
    // editor and invisible to the other.
    CHECK(ImportLibrary::IsSidecar("textures/hero.png.oimport"));
    CHECK(ImportLibrary::IsSidecar("textures/hero.png.OIMPORT"));
    CHECK_FALSE(ImportLibrary::IsSidecar("textures/hero.png"));
    CHECK_FALSE(ImportLibrary::IsSidecar("textures/oimport.png"));
}

// ---------------------------------------------------------------------------
//  Reading and writing
// ---------------------------------------------------------------------------

TEST_CASE("what was saved is what comes back") {
    const Sandbox box("round-trip");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport wanted;
    wanted.filter = TextureFilter::Linear;

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", wanted, error));
    CHECK(error.empty());
    CHECK(ImportLibrary::HasSettings("textures/hero.png"));

    ImportLibrary::Clear();   // off the disk rather than out of the cache
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
}

TEST_CASE("the settings file is JSON a person can open and fix") {
    const Sandbox box("hand-editable");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport wanted;
    wanted.filter = TextureFilter::Linear;
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", wanted, error));

    // THE WHOLE ARGUMENT FOR THE FORMAT is that these can be edited by hand,
    // so what was written has to parse and has to name the setting in a way
    // somebody reading it would recognise.
    std::string text;
    REQUIRE(FileSystem::ReadTextFile("textures/hero.png.oimport", text, error));
    const Json document = ParseJson(text, error);
    REQUIRE(error.empty());
    CHECK(document.is_object());
    CHECK(ReadString(document, "filter", "", "test") == "Linear");
}

TEST_CASE("saving the defaults removes the file rather than writing one that says nothing") {
    const Sandbox box("default-removes");
    GiveSettings(box, "textures/hero.png", "Linear");
    REQUIRE(ImportLibrary::HasSettings("textures/hero.png"));

    // "No file" and "a file saying Nearest" would otherwise be two spellings
    // of one state, and a project would collect one of them per image. It also
    // makes the Assets panel's hiding of these files harmless: there is
    // nothing hidden beside an asset that has nothing to say.
    TextureImport defaults;
    std::string   error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", defaults, error));
    CHECK(error.empty());
    CHECK_FALSE(ImportLibrary::HasSettings("textures/hero.png"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);
}

// ---------------------------------------------------------------------------
//  A file somebody typed wrong
// ---------------------------------------------------------------------------

TEST_CASE("a scaling mode this engine does not have falls back instead of refusing") {
    const Sandbox box("unknown-filter");
    GiveSettings(box, "textures/hero.png", "Trilinear");

    // DELIBERATELY NOT TileAsset's CHOICE. TileAsset::Deserialize refuses an
    // unrecognised frameMode and names the accepted values, because a tile
    // asset with a broken field is not a tile asset. An image with broken
    // settings is still an image, and the worst thing this layer could do is
    // turn a typo in a settings file into a level that will not load.
    //
    // The message naming "Nearest" and "Linear" is logged; it is the fallback
    // that is asserted here, because a log line is not a return value.
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);

    // And it falls back to the FONT default for a font, not to the texture
    // one - which is the thing a single shared fallback would get wrong.
    GiveSettings(box, "fonts/ui.ttf", "Trilinear");
    CHECK(ImportLibrary::ForFont("fonts/ui.ttf").filter == TextureFilter::Linear);
}

TEST_CASE("a settings file that is not JSON leaves the asset loadable") {
    const Sandbox box("broken-json");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport", "{ this is not json");
    ImportLibrary::Clear();

    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);
}

TEST_CASE("a settings file holding something other than settings falls back") {
    const Sandbox box("json-not-object");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport", "[1, 2, 3]");
    ImportLibrary::Clear();

    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);
}

TEST_CASE("a filter key of the wrong type falls back rather than failing") {
    const Sandbox box("filter-wrong-type");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport", "{ \"filter\": 7 }");
    ImportLibrary::Clear();

    // ReadString warns and hands back its fallback for a key of the wrong
    // type - see the note in core/Json.h - and the empty string it returns is
    // a name no filter has, so this lands on the same path as a typo.
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);
}

TEST_CASE("a settings file whose asset is gone is still read, and is not an error") {
    const Sandbox box("orphan");
    box.Write("textures/ghost.png.oimport", "{ \"filter\": \"Linear\" }");
    ImportLibrary::Clear();

    // NOTHING HERE REQUIRES THE ASSET TO EXIST. The editor asks before it has
    // a texture, and the exporter asks about files it is only about to copy -
    // so making the asset's presence a condition would break both for the sake
    // of catching a stray file that harms nothing.
    CHECK(ImportLibrary::HasSettings("textures/ghost.png"));
    CHECK(ImportLibrary::ForTexture("textures/ghost.png").filter ==
          TextureFilter::Linear);
}

// ---------------------------------------------------------------------------
//  Keeping the sidecar beside its asset
// ---------------------------------------------------------------------------

TEST_CASE("renaming an asset takes its settings with it") {
    const Sandbox box("follow-rename");
    GiveSettings(box, "textures/hero.png", "Linear");

    std::string error;
    REQUIRE(FileSystem::Rename("textures/hero.png", "champion.png", error));
    ImportLibrary::FollowAsset("textures/hero.png", "textures/champion.png");

    // BOTH HALVES. A copy that arrives without the original going is an orphan
    // file that will outlive the project.
    CHECK(FileSystem::Exists("textures/champion.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK(ImportLibrary::ForTexture("textures/champion.png").filter ==
          TextureFilter::Linear);
}

TEST_CASE("moving an asset into another folder takes its settings with it") {
    const Sandbox box("follow-move");
    GiveSettings(box, "hero.png", "Linear");
    box.MakeFolder("textures");

    std::string error;
    REQUIRE(FileSystem::Move("hero.png", "textures", error));
    ImportLibrary::FollowAsset("hero.png", "textures/hero.png");

    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("hero.png.oimport"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
}

TEST_CASE("moving an asset out to the assets root takes its settings too") {
    const Sandbox box("follow-move-root");
    GiveSettings(box, "textures/hero.png", "Linear");

    // THE ASSETS ROOT IS THE EMPTY VIRTUAL PATH, so "" is a legitimate
    // destination and the joining has to cope with it - "" plus "/" plus a
    // name begins with a slash and means something else entirely.
    std::string error;
    REQUIRE(FileSystem::Move("textures/hero.png", "", error));
    ImportLibrary::FollowAsset("textures/hero.png", "hero.png");

    CHECK(FileSystem::Exists("hero.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
}

TEST_CASE("deleting an asset deletes its settings") {
    const Sandbox box("follow-delete");
    GiveSettings(box, "textures/hero.png", "Linear");

    std::size_t removed = 0;
    std::string error;
    REQUIRE(FileSystem::Remove("textures/hero.png", removed, error));
    ImportLibrary::FollowAsset("textures/hero.png", "");

    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK_FALSE(ImportLibrary::HasSettings("textures/hero.png"));
}

TEST_CASE("an asset that never had settings survives all three without growing a file") {
    const Sandbox box("follow-nothing");
    box.Write("textures/plain.png", "not really pixels");

    // THE ORDINARY CASE IN A REAL PROJECT is the one with no sidecar, so a
    // FollowAsset that created an empty file, or complained, would be wrong far
    // more often than it was right.
    ImportLibrary::FollowAsset("textures/plain.png", "textures/renamed.png");
    ImportLibrary::FollowAsset("textures/plain.png", "");
    ImportLibrary::FollowAsset("textures/plain.png", "elsewhere/plain.png");

    CHECK_FALSE(FileSystem::Exists("textures/plain.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("textures/renamed.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("elsewhere/plain.png.oimport"));
}

TEST_CASE("a folder needs no special case and no flag") {
    const Sandbox box("follow-folder");
    GiveSettings(box, "textures/hero.png", "Linear");
    box.MakeFolder("sprites");

    // A FOLDER CARRIES ITS OWN CONTENTS, sidecars included, because they are
    // ordinary files inside it. This is what lets the three call sites in the
    // Assets panel pass whatever they are holding without working out which it
    // is - the rename dialog does not even record whether it has a folder.
    ImportLibrary::FollowAsset("textures", "sprites");

    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("sprites.oimport"));
}

TEST_CASE("settings arriving at a name already taken replace what was there") {
    const Sandbox box("follow-overwrite");
    GiveSettings(box, "textures/hero.png", "Linear");

    // An orphan sidecar at the destination, with no asset of its own: the only
    // way one can be there, because the rename or move that got us here has
    // already succeeded and FileSystem refuses a name that is taken.
    box.Write("textures/champion.png.oimport", "{ \"filter\": \"Nearest\" }");
    ImportLibrary::Clear();

    ImportLibrary::FollowAsset("textures/hero.png", "textures/champion.png");

    // REPLACED, NOT REFUSED. Refusing would leave the arriving asset wearing a
    // deleted asset's settings, which is the worse of the two wrongs.
    CHECK(ImportLibrary::ForTexture("textures/champion.png").filter ==
          TextureFilter::Linear);
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
}

// ---------------------------------------------------------------------------
//  Back to the defaults
// ---------------------------------------------------------------------------

TEST_CASE("Revert removes the settings file and the asset goes back to the defaults") {
    const Sandbox box("revert");
    GiveSettings(box, "textures/hero.png", "Linear");
    REQUIRE(ImportLibrary::ForTexture("textures/hero.png").filter ==
            TextureFilter::Linear);

    std::string error;
    REQUIRE(ImportLibrary::Revert("textures/hero.png", error));
    CHECK(error.empty());
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Nearest);
}

TEST_CASE("Revert says yes when there was nothing to revert") {
    const Sandbox box("revert-nothing");
    box.Write("textures/plain.png", "not really pixels");

    // The caller asked for an end state and that end state is already the
    // case. Reporting a failure for a job already done is how a Revert button
    // comes to look broken on the one asset that never had settings.
    std::string error;
    CHECK(ImportLibrary::Revert("textures/plain.png", error));
    CHECK(error.empty());
}

// ---------------------------------------------------------------------------
//  The cache
// ---------------------------------------------------------------------------

TEST_CASE("the cache is keyed on the virtual path, which a second project reuses") {
    // ONE PROJECT'S "textures/hero.png" IS NOT ANOTHER'S, and both are spelled
    // the same. Clearing is therefore not housekeeping: without it, opening a
    // second project would show the first one's settings for every path the
    // two happen to share, which for "textures/player.png" is most of them.
    {
        const Sandbox first("cache-first");
        GiveSettings(first, "textures/hero.png", "Linear");
        REQUIRE(ImportLibrary::ForTexture("textures/hero.png").filter ==
                TextureFilter::Linear);
    }
    {
        const Sandbox second("cache-second");
        second.Write("textures/hero.png", "not really pixels");
        CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
              TextureFilter::Nearest);
    }
}

TEST_CASE("clearing the cache does not touch the files") {
    const Sandbox box("cache-clear");
    GiveSettings(box, "textures/hero.png", "Linear");
    REQUIRE(ImportLibrary::CachedCount() == 0);

    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
    CHECK(ImportLibrary::CachedCount() == 1);

    ImportLibrary::Clear();
    CHECK(ImportLibrary::CachedCount() == 0);
    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
}

TEST_CASE("an asset with no settings is cached too, so the common case stops hitting the disk") {
    const Sandbox box("cache-absent");
    box.Write("textures/plain.png", "not really pixels");

    // THE OVERWHELMING MAJORITY OF ASSETS HAVE NO SIDECAR, and the Inspector
    // asks about the selected one on every frame it draws. Caching the hit and
    // not the miss would leave the common case as the one that still reads the
    // disk sixty times a second.
    REQUIRE(ImportLibrary::CachedCount() == 0);
    ImportLibrary::ForTexture("textures/plain.png");
    CHECK(ImportLibrary::CachedCount() == 1);
}

// ---------------------------------------------------------------------------
//  An exported game
// ---------------------------------------------------------------------------

TEST_CASE("a shipped game reads an asset's settings out of the archive it was packed into") {
    // ======================================================================
    //  THE ONE FAILURE IN THIS FEATURE WITH NO SYMPTOM IN THE EDITOR.
    //
    //  An exported game has no loose asset files: each top-level folder is
    //  packed into one <folder>.bin and the engine reads through those. A
    //  sidecar is never NAMED by anything - its path is derived - so if the
    //  exporter does not deliberately ship it, or if the archive reader
    //  cannot serve it, every setting in the project silently reverts to the
    //  defaults in the shipped build and nowhere says so. Pixel art would come
    //  out blurred for the player and look right for the developer.
    //
    //  This packs an asset and its sidecar exactly as GameExport does, throws
    //  the loose copies away, mounts the archive, and asks the ordinary
    //  question through the ordinary door.
    // ======================================================================
    const Sandbox box("archive");

    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport", "{ \"filter\": \"Linear\" }");

    // One archive per top-level folder, named after it, with the rest of the
    // virtual path as the entry name - the split GameExport makes.
    const std::string archive =
        (box.root / "assets" / (std::string("textures") + AssetArchive::kExtension))
            .string();
    const std::vector<AssetArchive::SourceFile> files = {
        {"hero.png", FileSystem::Resolve("textures/hero.png")},
        {"hero.png.oimport", FileSystem::Resolve("textures/hero.png.oimport")},
    };

    std::string error;
    REQUIRE(AssetArchive::Write(archive, files, error));
    CHECK(error.empty());

    // THE LOOSE COPIES GO. Without this the test would pass on the disk read
    // and prove nothing about the archive at all.
    std::error_code ec;
    fs::remove(fs::path(FileSystem::Resolve("textures/hero.png")), ec);
    fs::remove(fs::path(FileSystem::Resolve("textures/hero.png.oimport")), ec);
    REQUIRE_FALSE(fs::exists(fs::path(FileSystem::Resolve("textures/hero.png.oimport"))));

    // Resolve("") rather than AssetRoot(), which is the PROJECT folder - the
    // archives live in assets/ inside it. This is the same call the engine
    // makes in FileSystem::Init.
    REQUIRE(AssetArchive::MountAll(FileSystem::Resolve("")) == 1);
    ImportLibrary::Clear();

    // Exists and ReadTextFile both have to see into the archive, because
    // HasSettings uses the first and the read path uses the second.
    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK(ImportLibrary::HasSettings("textures/hero.png"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
}

// ---------------------------------------------------------------------------
//  The aliased paths - found by review, not by use
// ---------------------------------------------------------------------------

TEST_CASE("renaming an image to its own settings file's name does not destroy the image") {
    // ======================================================================
    //  THIS WAS A SILENT DATA-LOSS BUG, and it is the reason FollowAsset has
    //  guards at the top of it.
    //
    //  FollowAsset DELETES a file whose path it derived rather than was given.
    //  Rename hero.png to hero.png.oimport and that derived path stops being a
    //  sidecar: SidecarPath("hero.png") IS the renamed image. The function
    //  then read the image, wrote a copy of it to hero.png.oimport.oimport,
    //  and deleted the image - with no log line, because the move branch only
    //  speaks up on failure, and with the leftover invisible because the
    //  Assets panel hides anything shaped like a sidecar. There is no undo for
    //  the disk.
    //
    //  The copy was not even a copy: it went through ReadTextFile, which opens
    //  in text mode, and a PNG's signature holds 0x1A six bytes in - which
    //  MSVC's text mode treats as end of file.
    // ======================================================================
    const Sandbox box("follow-alias-self");
    box.Write("textures/hero.png", "the bytes of a picture");

    std::string error;
    REQUIRE(FileSystem::Rename("textures/hero.png", "hero.png.oimport", error));

    // The rename landed the image on exactly the path FollowAsset derives.
    REQUIRE(ImportLibrary::SidecarPath("textures/hero.png") ==
            "textures/hero.png.oimport");

    ImportLibrary::FollowAsset("textures/hero.png", "textures/hero.png.oimport");

    // STILL THERE, AND STILL ITSELF.
    REQUIRE(FileSystem::Exists("textures/hero.png.oimport"));
    std::string text;
    REQUIRE(FileSystem::ReadTextFile("textures/hero.png.oimport", text, error));
    CHECK(text == "the bytes of a picture");

    // And no mangled copy was left behind to be found later.
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport.oimport"));
}

TEST_CASE("following an asset to its own path does nothing at all") {
    const Sandbox box("follow-alias-same");
    GiveSettings(box, "textures/hero.png", "Linear");

    ImportLibrary::FollowAsset("textures/hero.png", "textures/hero.png");

    // Nothing read, nothing written, nothing deleted - and in particular the
    // settings are still there, which a naive read-write-delete would not have
    // managed with the source and the destination the same file.
    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK(ImportLibrary::ForTexture("textures/hero.png").filter ==
          TextureFilter::Linear);
}

TEST_CASE("a settings file is never treated as an asset with settings of its own") {
    const Sandbox box("follow-alias-sidecar");
    GiveSettings(box, "textures/hero.png", "Linear");

    // Somebody - or some future caller - hands FollowAsset the SIDECAR as the
    // asset. SidecarPath of it would be "...oimport.oimport", and acting on
    // that is never anything anybody wanted.
    ImportLibrary::FollowAsset("textures/hero.png.oimport",
                               "textures/champion.png.oimport");

    CHECK(FileSystem::Exists("textures/hero.png.oimport"));
    CHECK_FALSE(FileSystem::Exists("textures/hero.png.oimport.oimport"));
    CHECK_FALSE(FileSystem::Exists("textures/champion.png.oimport"));
}

TEST_CASE("an asset with no settings does not inherit an orphan left at its destination") {
    const Sandbox box("follow-orphan-dest");
    box.Write("textures/plain.png", "not really pixels");

    // An orphan: a settings file whose own asset is long gone.
    box.Write("textures/arrival.png.oimport", "{ \"filter\": \"Linear\" }");
    ImportLibrary::Clear();

    std::string error;
    REQUIRE(FileSystem::Rename("textures/plain.png", "arrival.png", error));
    ImportLibrary::FollowAsset("textures/plain.png", "textures/arrival.png");

    // "NO SETTINGS" IS A STATE THAT HAS TO TRAVEL TOO. The header's overwrite
    // rule covers an arriving sidecar landing on an orphan; this is the same
    // question from the one direction that rule does not reach, and the answer
    // has to match or an asset silently wears a dead asset's settings.
    CHECK_FALSE(ImportLibrary::HasSettings("textures/arrival.png"));
    CHECK(ImportLibrary::ForTexture("textures/arrival.png").filter ==
          TextureFilter::Nearest);
}

TEST_CASE("renaming a folder carries the cached settings of everything inside it") {
    const Sandbox box("follow-folder-cache");
    GiveSettings(box, "sprites/hero.png", "Linear");

    // Cached, which is the state that used to go stale.
    REQUIRE(ImportLibrary::ForTexture("sprites/hero.png").filter ==
            TextureFilter::Linear);
    REQUIRE(ImportLibrary::CachedCount() == 1);

    std::string error;
    REQUIRE(FileSystem::Rename("sprites", "characters", error));
    ImportLibrary::FollowAsset("sprites", "characters");

    // The sidecar travelled with the folder on disk by itself - it is a file
    // inside it - which is why FollowAsset needs no folder flag.
    CHECK(FileSystem::Exists("characters/hero.png.oimport"));

    // RE-KEYED, NOT DROPPED. An exact-key eviction would have left the entry
    // under "sprites/hero.png", still answering for a path that has moved - so
    // the Inspector would show the wrong scaling mode for whatever next
    // occupies it, and a save would write that back over a good sidecar.
    CHECK(ImportLibrary::CachedCount() == 1);
    CHECK(ImportLibrary::ForTexture("characters/hero.png").filter ==
          TextureFilter::Linear);
    CHECK(ImportLibrary::CachedCount() == 1);   // answered from the cache

    // And the old path now answers with the defaults, because there is nothing
    // there any more.
    CHECK(ImportLibrary::ForTexture("sprites/hero.png").filter ==
          TextureFilter::Nearest);
}

// ---------------------------------------------------------------------------
//  Sub-asset paths
//
//  These need no project at all - the rule is a pure string rule, which is
//  most of the point of it. See the note in ImportSettings.h on why the split
//  cannot simply be "is there a hash in it".
// ---------------------------------------------------------------------------

TEST_CASE("a frame of a sheet is recognised by the image on the left of the separator") {
    CHECK(IsSubAsset("textures/hero.png#walk_1"));
    CHECK(SheetPathOf("textures/hero.png#walk_1") == "textures/hero.png");
    CHECK(SubAssetNameOf("textures/hero.png#walk_1") == "walk_1");
}

TEST_CASE("a file whose own name contains a hash is not mistaken for a frame") {
    // ======================================================================
    //  THE CASE THE RULE EXISTS FOR, and it is not hypothetical: nothing
    //  sanitised the separator out of an imported filename, and PrefabPathFor
    //  - which turns an entity's name into a file name - did not replace it
    //  either, so an entity called "Boss#2" wrote a real
    //  prefabs/Boss#2.oprefab.
    //
    //  Reading either of these as a sub-asset would stop an ordinary file that
    //  works today from loading at all.
    // ======================================================================
    CHECK_FALSE(IsSubAsset("textures/hero#2.png"));
    CHECK(SheetPathOf("textures/hero#2.png") == "textures/hero#2.png");
    CHECK(SubAssetNameOf("textures/hero#2.png").empty());

    CHECK_FALSE(IsSubAsset("prefabs/Boss#2.oprefab"));
    CHECK(SheetPathOf("prefabs/Boss#2.oprefab") == "prefabs/Boss#2.oprefab");
}

TEST_CASE("a reference is shown by the file's name, not by its path") {
    // THE WHOLE OF WHAT A SLOT IN THE INSPECTOR PUTS ON ITS BUTTON. A
    // reference shown as a full path is unreadable in a field narrow enough to
    // sit in a panel - what you see is the middle of it with both ends cut off.
    CHECK(AssetDisplayName("textures/Sprites/Blue/hero.png") == "hero.png");
    CHECK(AssetDisplayName("textures/hero.png") == "hero.png");

    // Already at the top of assets/: there is nothing to take off.
    CHECK(AssetDisplayName("hero.png") == "hero.png");

    // Nothing in, nothing out. The word for having no reference is a thing
    // about widgets and belongs to whoever is drawing one.
    CHECK(AssetDisplayName("").empty());
}

TEST_CASE("the sheet's name is kept for a frame or an animation inside it") {
    // ======================================================================
    //  NOT "everything after the last slash", AND THIS IS WHY.
    //
    //  A project with four sliced sheets has four animations called "walk". A
    //  slot showing "walk" would be the same label on four different things,
    //  and the one question a label has to answer is which thing it is.
    // ======================================================================
    CHECK(AssetDisplayName("textures/Sprites/Blue/hero.png#walk") == "hero.png#walk");
    CHECK(AssetDisplayName("textures/atlas.png#f12") == "atlas.png#f12");

    // The folders go from the sheet, not from the name after it.
    CHECK(AssetDisplayName("a/b/c/d/sheet.png#idle") == "sheet.png#idle");
}

TEST_CASE("a file whose own name contains a hash is shown whole") {
    // The same rule the split above follows: "hero#2.png" is a FILE, so its
    // name is "hero#2.png" and there is no part inside it to name.
    CHECK(AssetDisplayName("textures/hero#2.png") == "hero#2.png");
    CHECK(AssetDisplayName("prefabs/Boss#2.oprefab") == "Boss#2.oprefab");

    // And an oddly named sheet with a real frame of it keeps both.
    CHECK(AssetDisplayName("textures/a.png#b.png#c") == "a.png#b.png#c");
}

TEST_CASE("the split takes the last separator, so a sheet may itself be oddly named") {
    // "a.png#b.png" is a real file that could exist; "#c" is a frame of it.
    // Splitting at the FIRST separator would make the sheet "a.png" and the
    // frame "b.png#c", which is a frame nothing can ever have.
    CHECK(IsSubAsset("textures/a.png#b.png#c"));
    CHECK(SheetPathOf("textures/a.png#b.png#c") == "textures/a.png#b.png");
    CHECK(SubAssetNameOf("textures/a.png#b.png#c") == "c");
}

TEST_CASE("a path that is not an image on the left is never a sub-asset") {
    CHECK_FALSE(IsSubAsset("scenes/level.oscene#thing"));
    CHECK_FALSE(IsSubAsset("#walk_1"));             // nothing on the left
    CHECK_FALSE(IsSubAsset("textures/hero.png#"));   // nothing on the right
    CHECK_FALSE(IsSubAsset("textures/hero.png"));
    CHECK_FALSE(IsSubAsset(""));
}

TEST_CASE("an ordinary path comes back from SheetPathOf unchanged") {
    // EVERY CALLER RELIES ON THIS. It is what lets the exporter, the Inspector
    // and PruneSelection map a path unconditionally instead of asking
    // IsSubAsset first and branching - and a branch that is sometimes
    // forgotten is how half of this feature's bugs would have arrived.
    CHECK(SheetPathOf("textures/hero.png") == "textures/hero.png");
    CHECK(SheetPathOf("scenes/level.oscene") == "scenes/level.oscene");
    CHECK(SheetPathOf("").empty());
}

TEST_CASE("a frame name that could escape the asset tree is refused") {
    // ======================================================================
    //  THE DANGEROUS ONE. A name is CONCATENATED INTO A PATH which is then
    //  handed to FileSystem::Resolve, and Resolve lexically normalises - so a
    //  frame named "../../hero.png" resolves to a real file above the project,
    //  and anything that deletes or renames "the selected asset" would have
    //  found it there.
    //
    //  The guard is on the NAME rather than on the separator, because this
    //  hazard exists whatever character joins the two halves.
    // ======================================================================
    std::string reason;
    CHECK_FALSE(IsLegalSubAssetName("../../hero.png", reason));
    CHECK_FALSE(reason.empty());
    CHECK_FALSE(IsLegalSubAssetName("..", reason));
    CHECK_FALSE(IsLegalSubAssetName("a/b", reason));
    CHECK_FALSE(IsLegalSubAssetName("a\\b", reason));
    CHECK_FALSE(IsLegalSubAssetName("C:thing", reason));
    CHECK_FALSE(IsLegalSubAssetName("walk#1", reason));
    CHECK_FALSE(IsLegalSubAssetName("walk.1", reason));
    CHECK_FALSE(IsLegalSubAssetName("", reason));
    CHECK_FALSE(IsLegalSubAssetName(" walk", reason));
    CHECK_FALSE(IsLegalSubAssetName("walk ", reason));

    CHECK(IsLegalSubAssetName("walk_1", reason));
    CHECK(reason.empty());
    CHECK(IsLegalSubAssetName("Blue Walk 1", reason));   // inner spaces are fine
}

TEST_CASE("a path cannot be built out of a name that was refused") {
    // MakeSubAssetPath is the only way a caller should build one of these, so
    // it refuses rather than handing back something Resolve would normalise.
    CHECK(MakeSubAssetPath("textures/hero.png", "walk_1") ==
          "textures/hero.png#walk_1");
    CHECK(MakeSubAssetPath("textures/hero.png", "../../hero.png").empty());
    CHECK(MakeSubAssetPath("textures/hero.png", "").empty());
    CHECK(MakeSubAssetPath("", "walk_1").empty());
}

// ---------------------------------------------------------------------------
//  The sheet block in the sidecar
// ---------------------------------------------------------------------------

TEST_CASE("a sliced sheet reads back its frames and its animation") {
    const Sandbox box("sheet-read");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport",
              "{\n"
              "  \"filter\": \"Nearest\",\n"
              "  \"sheet\": {\n"
              "    \"cellWidth\": 32,\n"
              "    \"cellHeight\": 32,\n"
              "    \"frames\": [\n"
              "      { \"name\": \"walk_1\", \"x\": 0,  \"y\": 0, \"w\": 32, \"h\": 32 },\n"
              "      { \"name\": \"walk_2\", \"x\": 32, \"y\": 0, \"w\": 32, \"h\": 32 }\n"
              "    ],\n"
              "    \"animations\": [\n"
              "      { \"name\": \"walk\", \"frames\": [\"walk_1\", \"walk_2\"],"
              " \"fps\": 8, \"loop\": true }\n"
              "    ]\n"
              "  }\n"
              "}\n");
    ImportLibrary::Clear();

    const TextureImport settings = ImportLibrary::ForTexture("textures/hero.png");
    REQUIRE(settings.sheet.IsSliced());
    CHECK(settings.sheet.cellWidth == 32);
    CHECK(settings.sheet.frames.size() == 2);

    const SheetFrame* second = settings.sheet.FindFrame("walk_2");
    REQUIRE(second != nullptr);
    CHECK(second->x == 32);
    CHECK(second->w == 32);
    CHECK(settings.sheet.FindFrame("nope") == nullptr);

    const SheetAnimation* walk = settings.sheet.FindAnimation("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->frames.size() == 2);
    CHECK(walk->fps == doctest::Approx(8.0f));
    CHECK(walk->loop);
}

TEST_CASE("an unsliced image has no sheet at all, so nothing changes for it") {
    const Sandbox box("sheet-absent");
    box.Write("textures/plain.png", "not really pixels");

    const TextureImport settings = ImportLibrary::ForTexture("textures/plain.png");
    CHECK_FALSE(settings.sheet.IsSliced());
    CHECK(settings.sheet.frames.empty());
    CHECK(settings.sheet.animations.empty());
}

TEST_CASE("a sliced sheet survives a save and a reload") {
    const Sandbox box("sheet-round-trip");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport wanted;
    wanted.sheet.cellWidth  = 16;
    wanted.sheet.cellHeight = 24;
    wanted.sheet.spacing    = 2;
    wanted.sheet.frames.push_back(SheetFrame{"idle", 0, 0, 16, 24});
    wanted.sheet.animations.push_back(SheetAnimation{"loop", {"idle"}, 6.0f, false});

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", wanted, error));
    CHECK(error.empty());

    ImportLibrary::Clear();
    const TextureImport back = ImportLibrary::ForTexture("textures/hero.png");
    CHECK(back.sheet.cellWidth == 16);
    CHECK(back.sheet.cellHeight == 24);
    CHECK(back.sheet.spacing == 2);
    REQUIRE(back.sheet.frames.size() == 1);
    CHECK(back.sheet.frames[0].name == "idle");
    CHECK(back.sheet.frames[0].h == 24);
    REQUIRE(back.sheet.animations.size() == 1);
    CHECK(back.sheet.animations[0].name == "loop");
    CHECK_FALSE(back.sheet.animations[0].loop);
}

TEST_CASE("a sliced sheet keeps its settings file even when the filter is the default") {
    const Sandbox box("sheet-not-default");
    box.Write("textures/hero.png", "not really pixels");

    // Saving the DEFAULTS deletes the sidecar - that is what keeps a project
    // from collecting one file per image that says nothing. A slice is not
    // nothing, so this must not take the frames with it.
    TextureImport wanted;   // filter left at the default
    wanted.sheet.frames.push_back(SheetFrame{"a", 0, 0, 8, 8});

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", wanted, error));
    CHECK(ImportLibrary::HasSettings("textures/hero.png"));

    ImportLibrary::Clear();
    CHECK(ImportLibrary::ForTexture("textures/hero.png").sheet.IsSliced());
}

TEST_CASE("a frame cannot have settings of its own") {
    const Sandbox box("frame-no-save");
    box.Write("textures/hero.png", "not really pixels");

    // The separator IS A LEGAL FILENAME CHARACTER, so this write would
    // otherwise succeed and leave "textures/hero.png#walk_1.oimport" on disk
    // for ever: a file nothing reads and the Assets panel hides.
    TextureImport settings;
    settings.filter = TextureFilter::Linear;
    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png#walk_1", settings, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(FileSystem::Exists("textures/hero.png#walk_1.oimport"));
}

TEST_CASE("a frame the engine cannot name is skipped, and the rest of the sheet loads") {
    const Sandbox box("sheet-bad-frame");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport",
              "{ \"sheet\": { \"frames\": [\n"
              "  { \"name\": \"../escape\", \"x\": 0,  \"y\": 0, \"w\": 8, \"h\": 8 },\n"
              "  { \"name\": \"empty\",     \"x\": 0,  \"y\": 0, \"w\": 0, \"h\": 8 },\n"
              "  { \"name\": \"negative\",  \"x\": -4, \"y\": 0, \"w\": 8, \"h\": 8 },\n"
              "  { \"name\": \"good\",      \"x\": 0,  \"y\": 0, \"w\": 8, \"h\": 8 },\n"
              "  { \"name\": \"good\",      \"x\": 8,  \"y\": 0, \"w\": 8, \"h\": 8 }\n"
              "] } }\n");
    ImportLibrary::Clear();

    // ONE SURVIVOR: the escape attempt, the two malformed rectangles and the
    // duplicate name are each skipped and named in the log, and the sheet is
    // still a sheet. A typo in a settings file must not make an image
    // unloadable.
    const TextureImport settings = ImportLibrary::ForTexture("textures/hero.png");
    REQUIRE(settings.sheet.frames.size() == 1);
    CHECK(settings.sheet.frames[0].name == "good");
    CHECK(settings.sheet.frames[0].x == 0);   // the FIRST of the two "good"s
}

TEST_CASE("a frame and an animation cannot share a name") {
    const Sandbox box("sheet-name-clash");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport",
              "{ \"sheet\": {\n"
              "  \"frames\":     [ { \"name\": \"walk\", \"x\": 0, \"y\": 0,"
              " \"w\": 8, \"h\": 8 } ],\n"
              "  \"animations\": [ { \"name\": \"walk\", \"frames\": [\"walk\"] } ]\n"
              "} }\n");
    ImportLibrary::Clear();

    // BOTH ARE ADDRESSED AS "<sheet>#<name>", so one name for both would make
    // that path mean two things and whichever was looked up first would win.
    // The animation is the one refused, because the frame was there first.
    const TextureImport settings = ImportLibrary::ForTexture("textures/hero.png");
    CHECK(settings.sheet.FindFrame("walk") != nullptr);
    CHECK(settings.sheet.FindAnimation("walk") == nullptr);
}

TEST_CASE("an animation with no frames left is skipped") {
    const Sandbox box("sheet-empty-animation");
    box.Write("textures/hero.png", "not really pixels");
    box.Write("textures/hero.png.oimport",
              "{ \"sheet\": { \"animations\": ["
              " { \"name\": \"walk\", \"frames\": [] } ] } }\n");
    ImportLibrary::Clear();

    CHECK(ImportLibrary::ForTexture("textures/hero.png").sheet.FindAnimation("walk") ==
          nullptr);
}

// ---------------------------------------------------------------------------
//  The slicer - MeasureGrid, SliceGrid and GuessSheetCell
//
//  NO SANDBOX IN THIS SECTION, and that is the point of having written these
//  as free functions over four integers. There is no file, no texture and no
//  renderer involved in "how many 16x20 cells fit in a 48x80 image", so the
//  awkward answers - a cell bigger than the image, a count that leaves the
//  range of an int, a margin that eats the whole picture - can be checked
//  here instead of being discovered by somebody dragging a slider.
// ---------------------------------------------------------------------------

TEST_CASE("a grid that divides the image exactly leaves nothing over") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 20;

    const SliceGeometry geometry = MeasureGrid(48, 80, grid);
    CHECK(geometry.columns == 3);
    CHECK(geometry.rows == 4);
    CHECK(geometry.count == 12);
    CHECK(geometry.leftoverX == 0);
    CHECK(geometry.leftoverY == 0);
    CHECK(geometry.reason.empty());
}

TEST_CASE("frames are cut in row-major order and named with the prefix") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 20;

    std::string                   reason;
    const std::vector<SheetFrame> frames = SliceGrid(48, 80, grid, "f", reason);
    REQUIRE(frames.size() == 12);
    CHECK(reason.empty());

    // ROW-MAJOR IS NOT AN ARBITRARY CHOICE. Sheets are drawn with one
    // animation per row, so consecutive numbers being consecutive frames of
    // the same movement is what makes "f0 to f3" mean something.
    CHECK(frames[0].name == "f0");
    CHECK(frames[0].x == 0);
    CHECK(frames[0].y == 0);

    CHECK(frames[2].name == "f2");
    CHECK(frames[2].x == 32);
    CHECK(frames[2].y == 0);

    // The first of the second row, which is where a column-major mistake
    // would show up as x == 0, y == 20 against x == 16, y == 0.
    CHECK(frames[3].name == "f3");
    CHECK(frames[3].x == 0);
    CHECK(frames[3].y == 20);

    CHECK(frames[5].name == "f5");
    CHECK(frames[5].x == 32);
    CHECK(frames[5].y == 20);

    CHECK(frames[11].name == "f11");
    CHECK(frames[11].x == 32);
    CHECK(frames[11].y == 60);

    for (const SheetFrame& frame : frames) {
        CHECK(frame.w == 16);
        CHECK(frame.h == 20);
    }
}

TEST_CASE("the margin is skipped on all four sides") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;
    grid.margin     = 2;

    const SliceGeometry geometry = MeasureGrid(52, 52, grid);
    CHECK(geometry.columns == 3);
    CHECK(geometry.rows == 3);
    CHECK(geometry.leftoverX == 0);

    std::string                   reason;
    const std::vector<SheetFrame> frames = SliceGrid(52, 52, grid, "f", reason);
    REQUIRE(frames.size() == 9);
    CHECK(frames[0].x == 2);
    CHECK(frames[0].y == 2);
    CHECK(frames[2].x == 34);

    // The last cell ENDS on the far margin: 34 + 16 == 50 == 52 - 2. An
    // off-by-one here is a frame hanging over the edge, which CopyFrameOut
    // refuses at load time - a magenta square and a log line, rather than
    // anything visible in the slicer.
    CHECK(frames[8].x + frames[8].w == 50);
    CHECK(frames[8].y + frames[8].h == 50);
}

TEST_CASE("the spacing is a gap between cells and not after the last one") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;
    grid.margin     = 2;
    grid.spacing    = 1;

    // 2 + 16 + 1 + 16 + 1 + 16 + 2 == 54. Counting a gap after the third cell
    // as well would make this two columns.
    const SliceGeometry geometry = MeasureGrid(54, 54, grid);
    CHECK(geometry.columns == 3);
    CHECK(geometry.leftoverX == 0);

    std::string                   reason;
    const std::vector<SheetFrame> frames = SliceGrid(54, 54, grid, "f", reason);
    REQUIRE(frames.size() == 9);
    CHECK(frames[0].x == 2);
    CHECK(frames[1].x == 19);
    CHECK(frames[2].x == 36);
    CHECK(frames[2].x + frames[2].w == 52);
}

TEST_CASE("a grid that does not divide the image slices anyway and says what is left") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;

    // TRAILING BLANK SPACE IN A SHEET IS REAL, so this is reported rather than
    // refused - but it is reported, because far more often it means one of the
    // four numbers is wrong.
    const SliceGeometry geometry = MeasureGrid(50, 50, grid);
    CHECK(geometry.columns == 3);
    CHECK(geometry.rows == 3);
    CHECK(geometry.leftoverX == 2);
    CHECK(geometry.leftoverY == 2);
    CHECK(geometry.reason.empty());

    std::string reason;
    CHECK(SliceGrid(50, 50, grid, "f", reason).size() == 9);
}

TEST_CASE("a cell bigger than the image cuts nothing, and says so") {
    SheetSlice grid;
    grid.cellWidth  = 64;
    grid.cellHeight = 64;

    const SliceGeometry geometry = MeasureGrid(32, 32, grid);
    CHECK(geometry.columns == 0);
    CHECK(geometry.rows == 0);
    CHECK(geometry.count == 0);
    CHECK_FALSE(geometry.reason.empty());

    std::string reason;
    CHECK(SliceGrid(32, 32, grid, "f", reason).empty());
    CHECK_FALSE(reason.empty());
}

TEST_CASE("a cell of nothing is refused rather than dividing by zero") {
    SheetSlice grid;   // cellWidth and cellHeight are both 0

    const SliceGeometry geometry = MeasureGrid(32, 32, grid);
    CHECK(geometry.count == 0);
    CHECK_FALSE(geometry.reason.empty());

    std::string reason;
    CHECK(SliceGrid(32, 32, grid, "f", reason).empty());
}

TEST_CASE("a negative margin or spacing is refused") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;
    grid.margin     = -4;

    // A NEGATIVE MARGIN WOULD MAKE THE USABLE AREA BIGGER THAN THE IMAGE, and
    // the cells past the edge would all be refused one at a time at load time.
    CHECK(MeasureGrid(32, 32, grid).count == 0);

    grid.margin  = 0;
    grid.spacing = -8;
    CHECK(MeasureGrid(32, 32, grid).count == 0);
}

TEST_CASE("a margin that eats the image cuts nothing") {
    SheetSlice grid;
    grid.cellWidth  = 8;
    grid.cellHeight = 8;
    grid.margin     = 20;

    CHECK(MeasureGrid(32, 32, grid).count == 0);
}

TEST_CASE("a count too big for an int is still counted") {
    SheetSlice grid;
    grid.cellWidth  = 1;
    grid.cellHeight = 1;

    // 65535 * 65535 is 4,294,836,225 - past the end of an int, which is why
    // SliceGeometry::count is a long long. Counted correctly and then refused.
    const SliceGeometry geometry = MeasureGrid(65535, 65535, grid);
    CHECK(geometry.columns == 65535);
    CHECK(geometry.count == 4294836225LL);

    std::string reason;
    CHECK(SliceGrid(65535, 65535, grid, "f", reason).empty());
    CHECK(reason.find("4294836225") != std::string::npos);
    CHECK(reason.find("4096") != std::string::npos);
}

TEST_CASE("a grid just inside the frame limit is cut, and one past it is not") {
    SheetSlice grid;
    grid.cellWidth  = 1;
    grid.cellHeight = 1;

    std::string reason;
    CHECK(SliceGrid(64, 64, grid, "f", reason).size() == 4096);   // exactly the cap
    CHECK(reason.empty());

    CHECK(SliceGrid(65, 64, grid, "f", reason).empty());          // 4160
    CHECK_FALSE(reason.empty());
}

TEST_CASE("frames cannot be named with a prefix that makes an illegal name") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;

    // '#' IS THE SEPARATOR ITSELF. A prefix holding one would make every frame
    // a path that splits in the wrong place, and "../" would make one that
    // resolves outside the project - see IsLegalSubAssetName.
    std::string reason;
    CHECK(SliceGrid(32, 32, grid, "a#b", reason).empty());
    CHECK_FALSE(reason.empty());

    CHECK(SliceGrid(32, 32, grid, "../", reason).empty());
    CHECK_FALSE(reason.empty());

    CHECK(SliceGrid(32, 32, grid, "a.b", reason).empty());
    CHECK_FALSE(reason.empty());
}

TEST_CASE("an empty prefix is legal and numbers the frames on their own") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;

    // AN EMPTY NAME IS REFUSED but an empty PREFIX is not, because the digit
    // is part of the name. "textures/hero.png#3" is a perfectly good path.
    std::string                   reason;
    const std::vector<SheetFrame> frames = SliceGrid(32, 32, grid, "", reason);
    REQUIRE(frames.size() == 4);
    CHECK(frames[0].name == "0");
    CHECK(frames[3].name == "3");
}

TEST_CASE("a cell size is guessed from the dimensions when they divide") {
    int cell = 0;

    REQUIRE(GuessSheetCell(48, 80, cell));
    CHECK(cell == 16);   // their greatest common divisor, 3 by 5 cells

    REQUIRE(GuessSheetCell(32, 64, cell));
    CHECK(cell == 32);   // one column of two

    // A SQUARE IMAGE'S GCD IS THE WHOLE IMAGE - one cell, which is the answer
    // "this is not a sheet". Halved, to offer the most conservative reading
    // that IS a grid.
    REQUIRE(GuessSheetCell(64, 64, cell));
    CHECK(cell == 32);

    // Nothing divides evenly from the gcd down, so it walks down to one that
    // does rather than giving up at the first miss.
    REQUIRE(GuessSheetCell(45, 45, cell));
    CHECK(cell == 15);
}

TEST_CASE("a cell size is not guessed when the dimensions say nothing") {
    int cell = 7;

    // Coprime: the gcd is 1, and a thousand one-pixel frames is not a guess.
    CHECK_FALSE(GuessSheetCell(101, 203, cell));
    CHECK(cell == 0);   // cleared rather than left as it was found

    CHECK_FALSE(GuessSheetCell(1, 1, cell));
    CHECK_FALSE(GuessSheetCell(6, 6, cell));    // would be 3, below the floor
    CHECK_FALSE(GuessSheetCell(0, 32, cell));
    CHECK_FALSE(GuessSheetCell(-8, -8, cell));
}

// ---------------------------------------------------------------------------
//  IsLegalSheet - the strict half of the deliberately lopsided pair
//
//  Every refusal below is a case the READER would have skipped and named, and
//  gone on without. That is right for a hand-edited file and wrong for one the
//  editor just wrote: the frames would be on screen looking saved until the
//  next reload and then be quietly gone. So Save refuses them instead.
// ---------------------------------------------------------------------------

TEST_CASE("every field of a slice comes back off the disk identical") {
    const Sandbox box("sheet-exact-round-trip");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport written;
    written.filter            = TextureFilter::Linear;
    written.sheet.cellWidth   = 16;
    written.sheet.cellHeight  = 20;
    written.sheet.margin      = 2;
    written.sheet.spacing     = 1;
    written.sheet.frames      = {SheetFrame{"f0", 2, 2, 16, 20},
                                 SheetFrame{"f1", 19, 2, 16, 20}};
    written.sheet.animations  = {SheetAnimation{"walk", {"f0", "f1"}, 8.5f, false}};

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", written, error));

    // THE CACHE IS WHAT MAKES THIS A TEST OF THE FILE. Save keeps what it
    // wrote, so without this the comparison below would be the struct against
    // itself and would pass however broken WriteSheet was.
    ImportLibrary::Clear();

    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    CHECK(read.filter == written.filter);
    CHECK(read.sheet == written.sheet);
}

TEST_CASE("two frames with one name are refused at the write") {
    const Sandbox box("sheet-save-duplicate");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames = {SheetFrame{"f0", 0, 0, 8, 8}, SheetFrame{"f0", 8, 0, 8, 8}};

    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
    CHECK(error.find("f0") != std::string::npos);

    // AND NOTHING WAS WRITTEN. A refusal that still left a file behind would
    // be the worse half of both behaviours.
    CHECK_FALSE(ImportLibrary::HasSettings("textures/hero.png"));
}

TEST_CASE("an animation with no frames is refused at the write") {
    const Sandbox box("sheet-save-empty-animation");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 8, 8}};
    settings.sheet.animations = {SheetAnimation{"walk", {}, 12.0f, true}};

    // THE HEADLINE CASE. The reader drops an animation that lists no frames,
    // so an editor allowed to write one would show it until the next reload.
    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
    CHECK(error.find("walk") != std::string::npos);
}

TEST_CASE("a frame and an animation cannot be given the same name by the editor") {
    const Sandbox box("sheet-save-name-clash");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"walk", 0, 0, 8, 8}};
    settings.sheet.animations = {SheetAnimation{"walk", {"walk"}, 12.0f, true}};

    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
    CHECK(error.find("walk") != std::string::npos);
}

TEST_CASE("two animations with one name are refused at the write") {
    const Sandbox box("sheet-save-duplicate-animation");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 8, 8}};
    settings.sheet.animations = {SheetAnimation{"walk", {"f0"}, 12.0f, true},
                                 SheetAnimation{"walk", {"f0"}, 6.0f, true}};

    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
}

TEST_CASE("a frame with no area or a negative corner is refused at the write") {
    const Sandbox box("sheet-save-no-area");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames = {SheetFrame{"f0", 0, 0, 0, 8}};

    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));

    settings.sheet.frames = {SheetFrame{"f0", -1, 0, 8, 8}};
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
}

TEST_CASE("a frame named something that is not a name is refused at the write") {
    const Sandbox box("sheet-save-bad-name");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames = {SheetFrame{"../../hero", 0, 0, 8, 8}};

    // The dangerous one: concatenated into a path, this resolves above the
    // project. Refused here as well as in the reader.
    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));

    settings.sheet.frames = {SheetFrame{"", 0, 0, 8, 8}};
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));

    settings.sheet.frames = {SheetFrame{"walk#1", 0, 0, 8, 8}};
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
}

TEST_CASE("a negative grid number is refused at the write") {
    const Sandbox box("sheet-save-negative-grid");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.cellWidth = -16;

    std::string error;
    CHECK_FALSE(ImportLibrary::Save("textures/hero.png", settings, error));
}

TEST_CASE("an animation may name a frame that is not there, because the reader allows it") {
    const Sandbox box("sheet-save-missing-frame");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 8, 8}};
    settings.sheet.animations = {SheetAnimation{"walk", {"f0", "f9"}, 12.0f, true}};

    // NOT A LEGALITY QUESTION, AND THE LINE MATTERS. IsLegalSheet's whole
    // contract is "the reader gives this back unchanged", and it does: a name
    // that matches no frame is skipped when the animation is RESOLVED, not
    // when it is read. The Inspector warns about it instead.
    std::string error;
    CHECK(ImportLibrary::Save("textures/hero.png", settings, error));
    ImportLibrary::Clear();

    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    REQUIRE(read.sheet.FindAnimation("walk") != nullptr);
    CHECK(read.sheet.FindAnimation("walk")->frames.size() == 2);
}

TEST_CASE("an unsliced image still writes the sidecar it always wrote") {
    const Sandbox box("sheet-none-unchanged");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.filter = TextureFilter::Linear;

    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));

    // NO "sheet" BLOCK AT ALL, which is the promise that a project that never
    // opens the slicer sees no change in its files.
    const std::string sidecar = ImportLibrary::SidecarPath("textures/hero.png");
    std::string       text;
    std::string readError;
    REQUIRE(FileSystem::ReadTextFile(sidecar, text, readError));
    CHECK(text.find("sheet") == std::string::npos);
}


// ---------------------------------------------------------------------------
//  Where the two halves meet
//
//  The slicer and the sidecar were written against each other and tested
//  apart. These two check the join: that what SliceGrid produces is something
//  Save will accept, and that the names it invents survive being turned into
//  paths and back. Either could fail without a single test above failing.
// ---------------------------------------------------------------------------

TEST_CASE("what the slicer cuts is what the writer accepts") {
    const Sandbox box("slicer-into-save");
    box.Write("textures/hero.png", "not really pixels");

    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 20;
    grid.margin     = 2;
    grid.spacing    = 1;

    std::string reason;
    TextureImport settings;
    settings.sheet        = grid;
    settings.sheet.frames = SliceGrid(54, 66, grid, "f", reason);
    REQUIRE(reason.empty());
    REQUIRE(settings.sheet.frames.size() == 9);   // 3 across, 3 down

    // THE WRITER IS THE STRICT ONE - see IsLegalSheet - so this is the test
    // that the slicer cannot produce a grid the editor then refuses to save.
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));
    CHECK(error.empty());

    ImportLibrary::Clear();
    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    REQUIRE(read.sheet.frames.size() == 9);

    // AND EVERY RECTANGLE IS INSIDE THE IMAGE. One that is not loads as the
    // magenta placeholder and a log line, which is the failure that would be
    // noticed last - the editor would show the frames in a list either way.
    for (const SheetFrame& frame : read.sheet.frames) {
        CHECK(frame.x >= 0);
        CHECK(frame.y >= 0);
        CHECK(frame.x + frame.w <= 54);
        CHECK(frame.y + frame.h <= 66);
    }
}

TEST_CASE("the names the slicer invents survive being made into paths") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 16;

    std::string                   reason;
    const std::vector<SheetFrame> frames = SliceGrid(64, 32, grid, "f", reason);
    REQUIRE(frames.size() == 8);

    for (const SheetFrame& frame : frames) {
        const std::string path = MakeSubAssetPath("textures/hero.png", frame.name);

        // NOT A FORMALITY. MakeSubAssetPath refuses a name it cannot use by
        // handing back an EMPTY string - so a prefix that produced an unusable
        // name would show up here, rather than as a selection that silently
        // cleared itself in the editor.
        CHECK(IsSubAsset(path));
        CHECK(SheetPathOf(path) == "textures/hero.png");
        CHECK(SubAssetNameOf(path) == frame.name);
    }
}

// ---------------------------------------------------------------------------
//  What the Assets panel's part tiles rely on
//
//  The panel draws one tile per frame and per animation of an open sheet, out
//  of the sheet's own texture, and it has to decide two things the engine
//  already decides elsewhere: whether a rectangle is usable, and which frame
//  an animation shows. These four cases pin the engine's answers so the two
//  cannot drift - the panel's copies are hand-written, and the symptom of a
//  drift is asymmetric: a tile that looks perfectly fine while the same part
//  dragged into the scene fails in the log.
//
//  Nothing in editor/ is reachable from here. The test binary links the engine
//  only, which is stated in tests/CMakeLists.txt and is deliberate - so the
//  panel's own code is checked by running the editor, not by these.
// ---------------------------------------------------------------------------

TEST_CASE("a frame reaching past the image is still saved, because the writer cannot see the image") {
    const Sandbox box("sheet-out-of-bounds-saves");
    box.Write("textures/hero.png", "not really pixels");

    TextureImport settings;
    settings.sheet.frames = {SheetFrame{"huge", 0, 0, 9000, 9000}};

    // ======================================================================
    //  THIS IS THE TEST THAT JUSTIFIES THE PANEL CHECKING BOUNDS AT ALL.
    //
    //  IsLegalSheet is handed a SheetSlice and nothing else - no path, no
    //  width, no height - so "9000 is past the right edge of a 48-pixel image"
    //  is not a question it is able to ask. It can only refuse a non-positive
    //  size or a negative corner.
    //
    //  So this SUCCEEDS, and it has to: the alternative would be a writer that
    //  refuses a file the reader accepts. The rectangle is bounded in the three
    //  places that know the image's size instead - the Inspector as it is
    //  typed, CopyFrameOut at load, and the Assets panel when it draws - and
    //  anybody tempted to delete one of those as redundant should fail here.
    // ======================================================================
    std::string error;
    REQUIRE(ImportLibrary::Save("textures/hero.png", settings, error));
    CHECK(error.empty());

    ImportLibrary::Clear();
    const TextureImport read = ImportLibrary::ForTexture("textures/hero.png");
    REQUIRE(read.sheet.frames.size() == 1);
    CHECK(read.sheet.frames[0].w == 9000);
    CHECK(read.sheet.frames[0].h == 9000);
}

TEST_CASE("an animation resolves to the first frame that still EXISTS, not the first it lists") {
    SheetSlice slice;
    slice.frames = {SheetFrame{"f1", 16, 0, 16, 20}, SheetFrame{"f2", 32, 0, 16, 20}};

    // "gone" is what a frame renamed in the Inspector leaves behind in every
    // animation that used it. The rename rewrites the ones it can see; a name
    // that matches nothing is skipped when the animation is RESOLVED.
    slice.animations = {SheetAnimation{"walk", {"gone", "f1", "f2"}, 8.0f, true}};

    const SheetAnimation* animation = slice.FindAnimation("walk");
    REQUIRE(animation != nullptr);

    // The walk the engine makes in CreateSubAssetTexture, and the one the
    // Assets panel makes to choose the picture for an animation's tile. It
    // must not stop at the missing name, and it must not fall through to the
    // end either.
    const SheetFrame* first = nullptr;
    for (const std::string& member : animation->frames) {
        if (const SheetFrame* found = slice.FindFrame(member); found != nullptr) {
            first = found;
            break;
        }
    }

    REQUIRE(first != nullptr);
    CHECK(first->name == "f1");
    CHECK(first->x == 16);
}

TEST_CASE("an animation whose every frame has been renamed away resolves to nothing") {
    SheetSlice slice;
    slice.frames     = {SheetFrame{"f1", 0, 0, 8, 8}};
    slice.animations = {SheetAnimation{"walk", {"old1", "old2"}, 8.0f, true}};

    // NOT THE SAME CASE AS AN EMPTY `frames` ARRAY, which the reader drops
    // outright. This animation lists two names and is kept, because the reader
    // has no way to know they are gone - so whatever draws it has to cope with
    // having no rectangle, rather than reaching for frames.front().
    const SheetAnimation* animation = slice.FindAnimation("walk");
    REQUIRE(animation != nullptr);
    CHECK(animation->frames.size() == 2);

    const SheetFrame* first = nullptr;
    for (const std::string& member : animation->frames) {
        if (const SheetFrame* found = slice.FindFrame(member); found != nullptr) {
            first = found;
            break;
        }
    }
    CHECK(first == nullptr);
}

TEST_CASE("a grid with no frames is not sliced, even though it is not a default slice") {
    SheetSlice grid;
    grid.cellWidth  = 16;
    grid.cellHeight = 20;

    // TWO DIFFERENT QUESTIONS, AND THE DIFFERENCE IS WHAT THE PANEL DRAWS ON.
    //
    // "Is this image marked as a spritesheet?" is `slice != SheetSlice{}`, and
    // the Inspector's checkbox reads it: a grid typed in but not yet cut is
    // marked. "Does it open?" is IsSliced(), and the Assets panel's disclosure
    // arrow reads that one: there is nothing to show until there are frames.
    //
    // Drawing the arrow on the first question would give a marked-but-unsliced
    // image an arrow that opens onto nothing.
    CHECK_FALSE(grid.IsSliced());
    CHECK_FALSE(grid == SheetSlice{});

    grid.frames.push_back(SheetFrame{"f0", 0, 0, 16, 20});
    CHECK(grid.IsSliced());
}

// ---------------------------------------------------------------------------
//  WHICH PICTURE A NAME RESOLVES TO, and whether a reference resolves at all
//
//  These two decide what the Inspector paints red for an eng::ImageRef and
//  for the older `// [image]` string, and they have to agree with what
//  ResourceManager::LoadTexture will actually do - a red slot holding a
//  picture that draws perfectly well is worse than no colour at all.
// ---------------------------------------------------------------------------

namespace {

// A sliced sheet with two frames and three animations. "ghost" lists a frame
// that is not there, which is the case FindImage has to step over, and
// "empty" lists only missing ones, which is the case it cannot answer.
void GiveSlicedSheet(const Sandbox& box, const std::string& asset) {
    box.Write(asset, "not really pixels");

    TextureImport settings;
    settings.sheet.cellWidth  = 16;
    settings.sheet.cellHeight = 16;
    settings.sheet.frames     = {SheetFrame{"f0", 0, 0, 16, 16},
                                 SheetFrame{"f1", 16, 0, 16, 16}};
    settings.sheet.animations = {
        SheetAnimation{"walk", {"f0", "f1"}, 8.0f, true, false},
        SheetAnimation{"ghost", {"gone", "f1"}, 8.0f, true, false},
        SheetAnimation{"empty", {"gone"}, 8.0f, true, false},
    };

    std::string error;
    REQUIRE(ImportLibrary::Save(asset, settings, error));
}

} // namespace

TEST_CASE("a part of a sheet resolves to a picture, frame or animation alike") {
    SheetSlice slice;
    slice.frames     = {SheetFrame{"f0", 0, 0, 16, 16},
                        SheetFrame{"f1", 16, 0, 16, 16}};
    slice.animations = {
        SheetAnimation{"walk", {"f0", "f1"}, 8.0f, true, false},
        SheetAnimation{"ghost", {"gone", "f1"}, 8.0f, true, false},
        SheetAnimation{"empty", {"gone"}, 8.0f, true, false},
    };

    const SheetFrame* frame = slice.FindImage("f1");
    REQUIRE(frame != nullptr);
    CHECK(frame->name == "f1");
    CHECK(frame->x == 16);

    // AN ANIMATION RESOLVES TO ITS FIRST FRAME, which is what makes
    // "hero.png#walk" a picture as well as a clip - and therefore what makes
    // an eng::ImageRef and an eng::AnimationRef able to hold the same string.
    const SheetFrame* first = slice.FindImage("walk");
    REQUIRE(first != nullptr);
    CHECK(first->name == "f0");

    // ...THE FIRST THAT STILL EXISTS. An animation holds frame NAMES, so one
    // it lists may have been renamed away since, and stopping at the first
    // entry would draw the placeholder for a clip that plays.
    const SheetFrame* survivor = slice.FindImage("ghost");
    REQUIRE(survivor != nullptr);
    CHECK(survivor->name == "f1");

    CHECK(slice.FindImage("empty") == nullptr);
    CHECK(slice.FindImage("nothing like this") == nullptr);
    CHECK(slice.FindImage("") == nullptr);
}

TEST_CASE("ImageExists answers for every shape a reference can hold") {
    Sandbox box("image-exists");

    // ---- a whole picture -------------------------------------------------
    box.Write("textures/solo.png", "not really pixels");
    CHECK(ImageExists("textures/solo.png"));
    CHECK_FALSE(ImageExists("textures/gone.png"));

    // ---- one part of a sliced sheet, WHICH IS THE CASE THAT WAS WRONG ----
    //
    // The Inspector's image picker offers every frame of every sliced sheet,
    // so this is a path somebody CHOOSES FROM A LIST the engine itself drew.
    // FileSystem::Exists answers no to it, because there is no such file -
    // so a correct reference, picked from that list, painted red.
    ImportLibrary::Clear();
    GiveSlicedSheet(box, "textures/sheet.png");

    CHECK(ImageExists("textures/sheet.png#f0"));
    CHECK_FALSE(FileSystem::Exists("textures/sheet.png#f0"));   // the old test

    // An animation is a picture too - its first frame. See FindImage.
    CHECK(ImageExists("textures/sheet.png#walk"));
    CHECK(ImageExists("textures/sheet.png#ghost"));

    // And these genuinely do not resolve.
    CHECK_FALSE(ImageExists("textures/sheet.png#empty"));
    CHECK_FALSE(ImageExists("textures/sheet.png#nosuch"));
    CHECK_FALSE(ImageExists("textures/nosheet.png#f0"));

    // ---- a real file whose own name contains a separator ------------------
    //
    // '#' is a legal filename character, so "a.png#b" can be a real file -
    // and it reads as frame "b" of the sheet "a.png" by the split rule. The
    // loader falls back to the disk once the sub-asset reading has failed, so
    // this has to as well or the Inspector would mark a file that loads.
    box.Write("textures/odd.png#part", "not really pixels");
    REQUIRE(IsSubAsset("textures/odd.png#part"));   // it IS read as a frame
    CHECK(ImageExists("textures/odd.png#part"));    // ...and still found

    // ---- nothing chosen is not broken ------------------------------------
    CHECK_FALSE(ImageExists(""));
}

TEST_CASE("SheetHasAnimation tells an animation from a frame, and needs a sheet") {
    // It had no test of its own: it was written to replace four hand-rolled
    // copies of this question and was only ever exercised through them.
    Sandbox box("sheet-has-animation");
    ImportLibrary::Clear();
    GiveSlicedSheet(box, "textures/sheet.png");

    CHECK(SheetHasAnimation("textures/sheet.png#walk"));

    // A FRAME IS NOT AN ANIMATION, and nothing about the path says which it
    // is - that is the whole reason this function exists. It is also where it
    // parts company with ImageExists, which says yes to both.
    CHECK_FALSE(SheetHasAnimation("textures/sheet.png#f0"));
    CHECK(ImageExists("textures/sheet.png#f0"));

    CHECK_FALSE(SheetHasAnimation("textures/sheet.png#nosuch"));

    // FALSE FOR A BARE NAME, which is "cannot be answered" rather than "no":
    // a path with no '#' does not say which sheet to look in, and guessing
    // would make the answer depend on load order. A
    // SpriteAnimationComponent can still play a bare name because IT knows
    // its own sheet.
    CHECK_FALSE(SheetHasAnimation("walk"));
    CHECK_FALSE(SheetHasAnimation(""));
}
