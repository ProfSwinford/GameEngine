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
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <cstdlib>
#include <filesystem>
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

// The last test needs a real file to save to, because Scene::Save is the only
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
    const fs::path root = fs::temp_directory_path() / "engine-newscene-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets" / "scenes", ec);

    SetAssetRoot(root.string().c_str());
    FileSystem fileSystem;
    REQUIRE(fileSystem.Init(BootConfig{}));

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

    fileSystem.Shutdown();
    SetAssetRoot("");
    fs::remove_all(root, ec);
}
