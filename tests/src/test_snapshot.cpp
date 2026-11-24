// ============================================================================
//  test_snapshot.cpp - the scene round-trip, which two features rest on.
//
//  Scene::SaveToString followed by Scene::LoadFromString is how the Play
//  button is non-destructive (Engine::EnterPlayMode snapshots, ExitPlayMode
//  restores) and how the editor's Ctrl+Z works (editor/src/UndoStack.h). Both
//  are silent when it goes wrong: the scene comes back subtly different and
//  nothing says so.
//
//  So the properties they depend on are pinned here rather than assumed:
//
//    what goes out comes back          - positions, parents, names
//    a child keeps its LOCAL transform - not recomputed from its world one
//    names survive, ids do not         - which is why undo tracks names
//    StartNewScene forgets the file    - and Unload deliberately does not
//    Stop restores WHICH FILE too      - even when the session changed scene
//    a failed restore leaves no file   - so Save cannot overwrite wreckage
//    IsOpen is not IsLoaded            - an empty scene is open, a closed one not
//
//  NO FILE SYSTEM AND NO ENGINE. A Scene is an ordinary object; these tests
//  make one, make it active because entity references resolve through
//  Scene::Active, and throw it away. Same arrangement as TestScene in
//  test_scriptfields.cpp.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/fs/FileSystem.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

using namespace eng;

namespace {

// A scene that is active for as long as it is in scope, and puts back
// whichever one was active before - so one test cannot leave another looking
// at freed memory.
struct Sandbox {
    Scene  scene;
    Scene* previous = nullptr;

    Sandbox() {
        previous = Scene::Active();
        Scene::SetActive(&scene);
    }
    ~Sandbox() { Scene::SetActive(previous); }

    Sandbox(const Sandbox&)            = delete;
    Sandbox& operator=(const Sandbox&) = delete;
};

// Some tests need a real file to save to, because Scene::Save is the only
// thing that sets a source path. Same helper the asset tests use.
void SetAssetRoot(const char* value) {
#if defined(_WIN32)
    _putenv_s("ENGINE_ASSET_ROOT", value);
#else
    if (value[0] == '\0') {
        unsetenv("ENGINE_ASSET_ROOT");
    } else {
        setenv("ENGINE_ASSET_ROOT", value, 1);
    }
#endif
}

// ===========================================================================
//  A TEMPORARY ASSET ROOT THAT CLEANS UP ON THE THROWING PATH TOO.
//
//  doctest's REQUIRE throws, so the three teardown statements these tests used
//  to end with were skipped by any failure between them - leaving
//  ENGINE_ASSET_ROOT pointing into the temp directory, FileSystem's root still
//  set (Subsystem's destructor is defaulted; only Shutdown clears it) and the
//  folder on disk. One real failure then reported as several.
//
//  The same arrangement test_persistence.cpp and the asset tests already use.
// ===========================================================================
struct FileRoot {
    fs::path   root;
    FileSystem fileSystem;

    explicit FileRoot(const char* name) {
        root = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "scenes", ec);
        SetAssetRoot(root.string().c_str());
        REQUIRE(fileSystem.Init(BootConfig{}));
    }

    ~FileRoot() {
        fileSystem.Shutdown();
        SetAssetRoot("");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    FileRoot(const FileRoot&)            = delete;
    FileRoot& operator=(const FileRoot&) = delete;
};

// ===========================================================================
//  A COMPONENT THAT CANNOT BE WRITTEN DOWN.
//
//  Serialize is DELIBERATELY NOT OVERRIDDEN: Component's default returns
//  false, which is this engine's way of saying "this type cannot be saved" -
//  see engine/include/engine/scene/Component.h. BuildSceneDocument counts
//  that as skipped and SaveToString then refuses, which is how Play is refused
//  rather than entered without a way back.
//
//  The omission is the whole point, so it is worth saying out loud: adding a
//  Serialize here would make the test that uses this silently stop testing
//  anything. test_playerinput.cpp leans on the same mechanism.
// ===========================================================================
struct Unsaveable final : Component {
    const char* TypeName() const override { return "Unsaveable"; }
    bool        Deserialize(const Json&, std::string&) override { return true; }
};

} // namespace

TEST_CASE("a snapshot puts a moved entity back where it was") {
    Sandbox world;

    const EntityId id = world.scene.CreateEntity("Rock");
    REQUIRE(world.scene.Get(id) != nullptr);
    world.scene.Get(id)->Transform().SetLocalPosition(Vec2{3.0f, 4.0f});

    std::string snapshot;
    std::string error;
    REQUIRE(world.scene.SaveToString(snapshot, error));
    CHECK(error.empty());

    // Move it somewhere else, the way a gizmo drag would.
    world.scene.Get(id)->Transform().SetLocalPosition(Vec2{99.0f, -7.0f});

    REQUIRE(world.scene.LoadFromString(snapshot, error));

    // THE ID IS LOOKED UP AGAIN BY NAME, because the old one is meaningless -
    // see the test below.
    const EntityId restored = world.scene.Find("Rock");
    REQUIRE(!restored.IsNull());
    const Entity* entity = world.scene.Get(restored);
    REQUIRE(entity != nullptr);

    CHECK(entity->Transform().LocalPosition().x == doctest::Approx(3.0f));
    CHECK(entity->Transform().LocalPosition().y == doctest::Approx(4.0f));
}

TEST_CASE("names survive a snapshot and entity ids do not") {
    // ======================================================================
    //  THE IDENTITY CLAIM THE EDITOR'S UNDO IS BUILT ON.
    //
    //  LoadFromString unloads and rebuilds, so indices are handed out afresh
    //  and generations go back to their default. An id taken before a restore
    //  is therefore not merely stale - it can still be VALID and resolve to a
    //  DIFFERENT entity, which no validity check can catch.
    //
    //  That is why editor/src/UndoStack.cpp carries the selection and the tile
    //  brush's target as NAMES and re-resolves them with Scene::Find. If this
    //  test ever fails because ids became stable, that code is merely
    //  redundant; if it fails the other way nothing warns anybody.
    // ======================================================================
    Sandbox world;

    const EntityId first  = world.scene.CreateEntity("First");
    const EntityId second = world.scene.CreateEntity("Second");
    CHECK(first != second);

    std::string snapshot;
    std::string error;
    REQUIRE(world.scene.SaveToString(snapshot, error));

    // Destroying the first one leaves a free slot, so the rebuild below
    // compacts and the indices shift - which is the case that makes a stale
    // id dangerous rather than simply invalid.
    world.scene.DestroyEntityImmediate(first);
    REQUIRE(world.scene.LoadFromString(snapshot, error));

    CHECK(!world.scene.Find("First").IsNull());
    CHECK(!world.scene.Find("Second").IsNull());

    const Entity* byName = world.scene.Get(world.scene.Find("Second"));
    REQUIRE(byName != nullptr);
    CHECK(byName->Name() == "Second");
}

TEST_CASE("a child keeps its local transform across a snapshot") {
    // ======================================================================
    //  THE ONE THAT WOULD DRIFT SILENTLY.
    //
    //  Scene::ResolveParents reattaches children with SetParent(parent),
    //  whose `keepWorldTransform` argument defaults to FALSE - so the local
    //  numbers just read from the document are kept as they are. Were that
    //  default ever flipped to true, every child's local transform would be
    //  recomputed from its world matrix on every load, and children would
    //  creep a little further every time anybody pressed Play or Ctrl+Z.
    //
    //  Nothing else in the tree would notice. This is the thing that notices.
    // ======================================================================
    Sandbox world;

    const EntityId parent = world.scene.CreateEntity("Parent");
    const EntityId child  = world.scene.CreateEntity("Child");
    REQUIRE(world.scene.Get(parent) != nullptr);
    REQUIRE(world.scene.Get(child) != nullptr);

    world.scene.Get(parent)->Transform().SetLocalPosition(Vec2{10.0f, 0.0f});
    world.scene.Get(child)->Transform().SetParent(&world.scene.Get(parent)->Transform());
    world.scene.Get(child)->Transform().SetLocalPosition(Vec2{2.0f, 1.0f});

    // Where it ought to be afterwards, worked out before anything is saved.
    CHECK(world.scene.Get(child)->Transform().WorldPosition().x == doctest::Approx(12.0f));

    std::string snapshot;
    std::string error;
    REQUIRE(world.scene.SaveToString(snapshot, error));

    // Disturb both of them, then put it all back.
    world.scene.Get(parent)->Transform().SetLocalPosition(Vec2{-50.0f, -50.0f});
    world.scene.Get(child)->Transform().SetLocalPosition(Vec2{0.0f, 0.0f});
    REQUIRE(world.scene.LoadFromString(snapshot, error));

    const Entity* restoredChild = world.scene.Get(world.scene.Find("Child"));
    REQUIRE(restoredChild != nullptr);
    REQUIRE(restoredChild->Transform().Parent() != nullptr);

    CHECK(restoredChild->Transform().LocalPosition().x == doctest::Approx(2.0f));
    CHECK(restoredChild->Transform().LocalPosition().y == doctest::Approx(1.0f));

    // And the world position follows from it, which is what anybody looking
    // at the screen would actually notice.
    CHECK(restoredChild->Transform().WorldPosition().x == doctest::Approx(12.0f));
    CHECK(restoredChild->Transform().WorldPosition().y == doctest::Approx(1.0f));
}

TEST_CASE("a snapshot of an empty scene is still a snapshot") {
    // The state File -> New Scene leaves behind, and the state undo has to be
    // able to step back TO - deleting the last entity must be undoable, which
    // means the empty scene has to round-trip like any other.
    Sandbox world;

    std::string empty;
    std::string error;
    REQUIRE(world.scene.SaveToString(empty, error));
    CHECK_FALSE(empty.empty());

    const EntityId id = world.scene.CreateEntity("Added");
    CHECK(!id.IsNull());
    CHECK(world.scene.EntityCount() == 1);

    REQUIRE(world.scene.LoadFromString(empty, error));
    CHECK(world.scene.EntityCount() == 0);
    CHECK(world.scene.Find("Added").IsNull());
}

TEST_CASE("StartNewScene forgets the file and Unload deliberately does not") {
    // ======================================================================
    //  THE DIFFERENCE BETWEEN THE TWO, which is the whole reason there are
    //  two. Unload means "put this away" and keeps the path, because Reload
    //  Scene needs it. StartNewScene means "start something else", and a kept
    //  path would make the first Ctrl+S of a brand new scene silently
    //  overwrite the file that was open before it.
    //
    //  IT TAKES A REAL FILE TO TEST, which is why this one has a file system
    //  under it while the others in this file do not: Scene::Save is the only
    //  thing that sets m_sourcePath, so without saving there is no path for
    //  Unload to keep and the test asserts nothing. It said as much in a
    //  comment and checked only that an empty path was still empty - a
    //  tautology wearing the name of a contract.
    // ======================================================================
    const FileRoot files{"engine-newscene-test"};

    {
        Sandbox world;
        world.scene.CreateEntity("Something");

        std::string error;
        REQUIRE(world.scene.Save("scenes/kept.oscene", error));
        CHECK(world.scene.SourcePath() == "scenes/kept.oscene");

        // ---- UNLOAD KEEPS IT ---------------------------------------------
        world.scene.Unload();
        CHECK(world.scene.EntityCount() == 0);
        CHECK(world.scene.SourcePath() == "scenes/kept.oscene");

        // Which is what makes Reload Scene possible after a Close Scene.
        REQUIRE(world.scene.Load("scenes/kept.oscene", error));
        CHECK(world.scene.EntityCount() == 1);
        CHECK(!world.scene.Find("Something").IsNull());

        // ---- StartNewScene DOES NOT ---------------------------------------
        world.scene.StartNewScene();
        CHECK(world.scene.EntityCount() == 0);
        CHECK(world.scene.SourcePath().empty());
        CHECK(world.scene.Find("Something").IsNull());

        // And it is usable immediately afterwards - a new scene that cannot be
        // built in would be no use. This is the path a freshly created project
        // takes to its first scene.
        CHECK(!world.scene.CreateEntity("Fresh").IsNull());
        CHECK(world.scene.EntityCount() == 1);

        // The file it forgot is untouched, which is the point: saving the new
        // scene now has to be told where to go.
        CHECK(FileSystem::Exists("scenes/kept.oscene"));
    }
}

// ============================================================================
//  THE PLAY-MODE PAIR, AND THE BUG THAT PUT IT HERE.
//
//  Reported by somebody who lost a level to it. They were editing a main menu
//  whose button script called Engine::RequestScene; they pressed Play, clicked
//  the button, the level loaded, they pressed Stop, and they pressed Ctrl+S.
//  The menu's entities were written over the level file. Two menus, no level.
//
//  The mechanism was two correct decisions meeting:
//
//      Scene::Load ASSIGNS m_sourcePath          - of course it does
//      Scene::LoadFromString leaves it alone     - undo needs that
//
//  and nothing in between, so Stop handed back the pre-play contents under the
//  played scene's name. Every readout in the editor agreed, because all six of
//  them ask SourcePath().
//
//  WHY THESE TESTS CAN EXIST AT ALL, which is the other half of the fix.
//  Play mode used to be two bare strings inside Engine, and Engine::m_scene is
//  only created inside Engine::Init, which needs a window - so the suite could
//  not enter play mode and this invariant had no test anywhere. Moving the pair
//  onto Scene, which is an ordinary object, is what makes the whole thing
//  checkable with no engine and no display. These tests fail on the old code.
// ============================================================================
TEST_CASE("a play session that changes scene comes back to the file it started in") {
    const FileRoot files{"engine-playsnapshot-test"};

    {
        Sandbox world;

        // ---- the menu, as the person was editing it ----------------------
        world.scene.CreateEntity("MenuButton");
        std::string error;
        REQUIRE(world.scene.Save("scenes/menu.oscene", error));
        REQUIRE(world.scene.SourcePath() == "scenes/menu.oscene");

        // ---- a level for the script to ask for ---------------------------
        //
        // Written through a second scene so that menu.oscene on disk is left
        // exactly as it was saved above - which is what lets the last check
        // in this case mean something.
        {
            Scene level;
            Scene::SetActive(&level);
            level.CreateEntity("Boss");
            REQUIRE(level.Save("scenes/level.oscene", error));
            Scene::SetActive(&world.scene);
        }

        // ---- Play --------------------------------------------------------
        Scene::PlaySnapshot snapshot;
        REQUIRE(world.scene.TakePlaySnapshot(snapshot, error));
        CHECK(snapshot.sourcePath == "scenes/menu.oscene");

        // ---- the button is clicked: Engine::RequestScene -> Scene::Load ---
        REQUIRE(world.scene.Load("scenes/level.oscene", error));
        CHECK(world.scene.SourcePath() == "scenes/level.oscene");
        CHECK(!world.scene.Find("Boss").IsNull());

        // ---- Stop --------------------------------------------------------
        REQUIRE(world.scene.RestorePlaySnapshot(snapshot, error));

        // THE CONTENTS CAME BACK, which always worked.
        CHECK(!world.scene.Find("MenuButton").IsNull());
        CHECK(world.scene.Find("Boss").IsNull());

        // AND THE FILE, which is the whole point and the thing that did not.
        // Without this line the next Save() writes the menu over the level.
        CHECK(world.scene.SourcePath() == "scenes/menu.oscene");

        // ---- and Ctrl+S goes where it should -----------------------------
        //
        // The real check: not what the field says, but what a save DOES with
        // it. Engine::SaveScene({}) targets exactly this, so saving here is
        // the keystroke the bug report describes.
        // COPIED FIRST, not passed straight from the member: Scene::Save
        // assigns the argument to m_sourcePath, and handing it a view of that
        // same member would be a string assigning from its own buffer.
        const std::string target = world.scene.SourcePath();
        REQUIRE(world.scene.Save(target, error));

        Scene reopened;
        Scene::SetActive(&reopened);
        REQUIRE(reopened.Load("scenes/level.oscene", error));
        CHECK(!reopened.Find("Boss").IsNull());          // the level survived
        CHECK(reopened.Find("MenuButton").IsNull());     // nothing menu-shaped in it
        Scene::SetActive(&world.scene);
    }
}

TEST_CASE("a scene that had never been saved comes back never having been saved") {
    // ======================================================================
    //  THE EMPTY PATH IS A VALUE, NOT A MISSING ONE.
    //
    //  StartNewScene clears the source path precisely so that the first
    //  Ctrl+S of a brand new scene cannot overwrite the file that was open
    //  before it - see the case above this one. A play session that changed
    //  scene used to put a path back onto such a scene, which defeated that
    //  guard and the matching one in Engine::SaveScene at the same time: both
    //  ask whether the path is empty, and it no longer was.
    //
    //  So restoring the path is UNCONDITIONAL, including when it is empty. A
    //  "only if we have one" version of RestorePlaySnapshot would pass every
    //  other test in this file and fail only here.
    // ======================================================================
    const FileRoot files{"engine-playsnapshot-new-test"};

    {
        Sandbox     world;
        std::string error;

        {
            Scene level;
            Scene::SetActive(&level);
            level.CreateEntity("Boss");
            REQUIRE(level.Save("scenes/level.oscene", error));
            Scene::SetActive(&world.scene);
        }

        // A scratch scene: entities, no file. File -> New Scene leaves this.
        world.scene.StartNewScene();
        world.scene.CreateEntity("Scratch");
        REQUIRE(world.scene.SourcePath().empty());

        Scene::PlaySnapshot snapshot;
        REQUIRE(world.scene.TakePlaySnapshot(snapshot, error));
        CHECK(snapshot.sourcePath.empty());

        REQUIRE(world.scene.Load("scenes/level.oscene", error));
        REQUIRE(world.scene.RestorePlaySnapshot(snapshot, error));

        CHECK(!world.scene.Find("Scratch").IsNull());
        CHECK(world.scene.SourcePath().empty());

        // Which is what makes Save refuse rather than pick somewhere. The
        // scene's own Save says so; Engine::SaveScene({}) says the same thing
        // in the editor's words.
        CHECK_FALSE(world.scene.Save("", error));
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("a restore that fails leaves the scene with no file to overwrite") {
    // ======================================================================
    //  THE ONLY OUTCOME THAT CANNOT DESTROY SOMETHING.
    //
    //  A snapshot that will not parse should not happen - SaveToString made
    //  it - but "should not happen" is not a plan, and the two obvious
    //  answers are both bad. Leave the path at the pre-play file and the next
    //  Ctrl+S writes whatever the failed restore left over the scene somebody
    //  was editing. Leave it at the played file and it overwrites that one
    //  instead. Pointing at NEITHER means Save has nowhere to go and asks for
    //  a name, which costs a dialog and no files.
    //
    //  Scene::Unload has already run by then, so the entities are gone either
    //  way; the only question is which file is at risk, and the answer is
    //  none.
    // ======================================================================
    Sandbox world;

    world.scene.CreateEntity("Keep");

    std::string error;
    Scene::PlaySnapshot snapshot;
    REQUIRE(world.scene.TakePlaySnapshot(snapshot, error));

    // Pretend the path had been set by a save, without needing a file system:
    // SetSourcePath is the editor's rename hook and does exactly this.
    world.scene.SetSourcePath("scenes/precious.oscene");
    snapshot.sourcePath = "scenes/precious.oscene";

    // Not JSON at all.
    snapshot.text = "{ this is not a scene";

    CHECK_FALSE(world.scene.RestorePlaySnapshot(snapshot, error));
    CHECK_FALSE(error.empty());

    // NO FILE. Not the pre-play one, not the played one.
    CHECK(world.scene.SourcePath().empty());
}

TEST_CASE("a snapshot refused on the way in carries nothing back out") {
    // ======================================================================
    //  A REFUSAL MUST NOT HALF-FILL THE CALLER'S SNAPSHOT.
    //
    //  TakePlaySnapshot fails when the scene holds a component that cannot be
    //  written down, which is how Play is refused rather than entered without
    //  a way back. The danger is the obvious simplification: write the path
    //  straight into outSnapshot, then the text. A caller would then hold a
    //  snapshot with a path and NO TEXT - and Engine::ExitPlayMode tests
    //  Empty(), which asks about the text, so a session that was never
    //  entered could still re-point the scene on the way out.
    //
    //  THIS IS WHY TakePlaySnapshot STAGES INTO A LOCAL. The test earlier in
    //  this file would pass against that simplification; this one would not.
    // ======================================================================
    Sandbox     world;
    std::string error;

    world.scene.SetSourcePath("scenes/somewhere.oscene");

    // A good snapshot first, so there is something to be clobbered.
    Scene::PlaySnapshot kept;
    REQUIRE(world.scene.TakePlaySnapshot(kept, error));
    CHECK_FALSE(kept.Empty());
    CHECK(kept.sourcePath == "scenes/somewhere.oscene");

    const Scene::PlaySnapshot before = kept;

    // ======================================================================
    //  THE PATH HAS TO MOVE BETWEEN THE TWO CALLS, or this test cannot see
    //  the bug it is for.
    //
    //  Written without this line, the scene was still at the same path when
    //  the refusal happened - so a version that wrote the path straight into
    //  the caller's snapshot wrote the value that was already there, and
    //  every assertion below passed. That was checked by mutation and it did
    //  NOT fail, which is the only way to find out.
    //
    //  The real case is exactly this, because Engine::m_playModeSnapshot is a
    //  member that outlives one session: a snapshot of one scene still in
    //  hand when Play is refused on a different one.
    // ======================================================================
    world.scene.SetSourcePath("scenes/elsewhere.oscene");

    // Now make the scene unsaveable. See Unsaveable at the top of this file.
    const EntityId id = world.scene.CreateEntity("Awkward");
    REQUIRE(world.scene.Get(id) != nullptr);
    world.scene.Get(id)->AddComponent(std::make_unique<Unsaveable>());

    CHECK_FALSE(world.scene.TakePlaySnapshot(kept, error));
    CHECK_FALSE(error.empty());

    // NOTHING CAME BACK OUT - not even the path, which Empty() cannot see and
    // which is the half a path-first version overwrites.
    CHECK(kept.text == before.text);
    CHECK(kept.sourcePath == before.sourcePath);
    CHECK(kept.sourcePath == "scenes/somewhere.oscene");   // NOT "elsewhere"

    // And the shape of a snapshot nobody has filled in, which is what
    // Engine::ExitPlayMode's Empty() test rests on.
    const Scene::PlaySnapshot nothing;
    CHECK(nothing.Empty());
    CHECK(nothing.sourcePath.empty());
}

TEST_CASE("an ordinary undo step still does not move the file") {
    // ======================================================================
    //  THE HALF THAT MUST NOT CHANGE.
    //
    //  The editor's Ctrl+Z is SaveToString/LoadFromString, the contents-only
    //  pair, and it has to stay that way. If undo restored the path as well,
    //  Save As followed by Ctrl+Z would send the scene back to the file it had
    //  just been saved away from, and the next Ctrl+S would overwrite that
    //  one - the same class of bug as the one this file was extended for,
    //  introduced by over-applying the fix.
    //
    //  So this pins LoadFromString's silence about the path deliberately, and
    //  it is the reason the play-mode pair is two extra functions rather than
    //  a change to these two.
    // ======================================================================
    Sandbox world;

    world.scene.CreateEntity("Rock");

    std::string error;
    std::string undoStep;
    REQUIRE(world.scene.SaveToString(undoStep, error));

    // Save As, in the only part of it that matters here.
    world.scene.SetSourcePath("scenes/variant.oscene");

    world.scene.CreateEntity("Tree");

    // Ctrl+Z.
    REQUIRE(world.scene.LoadFromString(undoStep, error));

    CHECK(world.scene.Find("Tree").IsNull());
    CHECK(!world.scene.Find("Rock").IsNull());
    CHECK(world.scene.SourcePath() == "scenes/variant.oscene");
}

TEST_CASE("a closed scene is not an empty one, and only one of them can be saved") {
    // ======================================================================
    //  TWO QUESTIONS THAT LOOK LIKE ONE, AND USING THE WRONG ONE COST A FILE.
    //
    //  IsLoaded() asks "are there any entities?". IsOpen() asks "is there a
    //  scene here at all?". They differ in both directions:
    //
    //      an emptied scene   open, not loaded   - and saving it is correct
    //      a closed scene     not open           - and saving it is not
    //
    //  The source path deliberately SURVIVES an unload, because "reload what
    //  I just unloaded" needs it - see the case above. So after File -> Close
    //  Scene the path still names a real asset with nothing behind it, and
    //  every save route that asked only whether the path was empty offered to
    //  save, then serialised the zero live entities straight over the file.
    //
    //  THAT GOT MUCH MORE DANGEROUS when closing the editor started asking
    //  about unsaved work, because that dialog is the one moment everybody
    //  presses Save - and its own text argues for pressing it.
    //
    //  IsLoaded() IS NOT THE FIX. Gating a save on it would stop somebody
    //  saving a scene they had deliberately emptied, which the File menu
    //  records as a bug that was already fixed once.
    // ======================================================================
    const FileRoot files{"engine-isopen-test"};

    {
        Sandbox     world;
        std::string error;

        // ---- nothing has happened yet ------------------------------------
        CHECK_FALSE(world.scene.IsOpen());
        CHECK_FALSE(world.scene.IsLoaded());

        // ---- a brand new scene IS open, with nothing in it ---------------
        world.scene.StartNewScene();
        CHECK(world.scene.IsOpen());
        CHECK_FALSE(world.scene.IsLoaded());

        world.scene.CreateEntity("Something");
        REQUIRE(world.scene.Save("scenes/thing.oscene", error));
        CHECK(world.scene.IsOpen());
        CHECK(world.scene.IsLoaded());

        // ---- EMPTIED ON PURPOSE: still open, and still saveable ----------
        //
        // The distinction that rules IsLoaded() out as the gate.
        world.scene.DestroyEntityImmediate(world.scene.Find("Something"));
        CHECK(world.scene.IsOpen());
        CHECK_FALSE(world.scene.IsLoaded());
        REQUIRE(world.scene.Save("scenes/thing.oscene", error));

        // ---- CLOSED: not open, and the path is still there ---------------
        world.scene.Unload();
        CHECK_FALSE(world.scene.IsOpen());
        CHECK_FALSE(world.scene.IsLoaded());

        // Which is the whole trap: a path with nothing behind it.
        CHECK(world.scene.SourcePath() == "scenes/thing.oscene");

        // ---- and a load opens it again -----------------------------------
        REQUIRE(world.scene.Load("scenes/thing.oscene", error));
        CHECK(world.scene.IsOpen());

        // ---- as does restoring a play snapshot ---------------------------
        Scene::PlaySnapshot snapshot;
        REQUIRE(world.scene.TakePlaySnapshot(snapshot, error));
        world.scene.Unload();
        CHECK_FALSE(world.scene.IsOpen());
        REQUIRE(world.scene.RestorePlaySnapshot(snapshot, error));
        CHECK(world.scene.IsOpen());
    }
}

TEST_CASE("unloading a scene that holds nothing still closes it") {
    // ======================================================================
    //  THE EARLY RETURN IN Unload WAS THE WHOLE RISK.
    //
    //  Unload gives up immediately when there are no entity slots, which is
    //  the common no-op case - and a flag set after that return would leave a
    //  brand new empty scene reporting itself as open for ever after a Close
    //  Scene. The one state that has to behave is the one with nothing in it.
    // ======================================================================
    Sandbox world;

    world.scene.StartNewScene();
    REQUIRE(world.scene.IsOpen());
    REQUIRE_FALSE(world.scene.IsLoaded());

    world.scene.Unload();
    CHECK_FALSE(world.scene.IsOpen());
}
