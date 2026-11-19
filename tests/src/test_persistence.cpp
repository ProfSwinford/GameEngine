// ============================================================================
//  test_persistence.cpp - DontDestroyOnLoad: entities that survive a scene
//  change.
//
//  WHY THIS NEEDS REAL FILES. The feature is a distinction between two kinds
//  of unload - Scene::Load keeps marked entities, everything else does not -
//  so testing it through LoadFromString would exercise the half that is
//  supposed to destroy them and prove nothing. These cases write scene files
//  and load them by path, which is the only route that carries anything.
//
//  AND WHY IT IS WORTH THIS MANY. A surviving entity is a live object in a
//  container that has just been rebuilt around it: its slot, its generation,
//  its name index entry, its transform-to-entity lookup and its parent link
//  all have to come out the other side intact. Each of those failing looks
//  like something else - a wrong position, an unfindable object, a crash two
//  frames later - rather than like "persistence is broken".
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

struct SceneSandbox {
    fs::path   root;
    FileSystem fileSystem;
    Scene      scene;

    explicit SceneSandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-persist-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "scenes", ec);

        SetEnv("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});

        ComponentFactory::RegisterBuiltins();
        Scene::SetActive(&scene);
    }

    ~SceneSandbox() {
        scene.Unload();
        Scene::SetActive(nullptr);
        fileSystem.Shutdown();
        SetEnv("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void WriteScene(const std::string& virtualPath, const std::string& body) const {
        const fs::path  real = fs::path(FileSystem::Resolve(virtualPath));
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << body;
    }

    // A scene with one entity of the given name, at the origin.
    void WriteSimple(const std::string& virtualPath, const std::string& entityName) const {
        WriteScene(virtualPath,
                   R"({"name":"S","camera":{"zoom":1},"entities":[{"name":")" +
                       entityName +
                       R"(","components":[{"type":"TransformComponent",)"
                       R"("position":[0,0]}]}]})");
    }
};

} // namespace

TEST_CASE("an ordinary entity does not survive a scene change") {
    SceneSandbox box("plain");
    box.WriteSimple("scenes/a.oscene", "Keeper");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId id     = box.scene.Find("Keeper");
    const Entity*  before = box.scene.Get(id);
    REQUIRE(before != nullptr);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // THE BASELINE. If this ever passes a surviving entity, the feature has
    // leaked into everything rather than being opt-in.
    CHECK(box.scene.Find("Keeper").IsNull());
    CHECK(box.scene.PersistentCount() == 0);

    // ======================================================================
    //  ASSERTED AS "NOT THE SAME OBJECT" RATHER THAN "RESOLVES TO NULL", and
    //  the difference is a pre-existing footgun worth knowing about.
    //
    //  An unload with nothing to carry clears the slot vector outright, so
    //  the next scene starts allocating at index 0 generation 1 again - and
    //  an EntityId from the old scene can therefore ALIAS an entity in the
    //  new one. That is how the engine has always behaved; it is not
    //  something persistence introduced, and it is not something this case
    //  should quietly bless by asserting null.
    //
    //  Note the KeepPersistent path does better: it keeps the slots and bumps
    //  each freed generation, so a stale id there is correctly recognised.
    //
    //  The address cannot stand in for it either - the allocator hands the
    //  same block back for the new Entity often enough to make such a check
    //  pass by luck.
    // ======================================================================
    (void)before;
}

TEST_CASE("a marked entity survives a scene change, as the same object") {
    SceneSandbox box("survive");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId id     = box.scene.Find("Manager");
    Entity*        before = box.scene.Get(id);
    REQUIRE(before != nullptr);

    before->Transform().SetLocalPosition(Vec2{123.0f, 456.0f});
    box.scene.SetPersistent(id, true);
    CHECK(box.scene.IsPersistent(id));

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // ======================================================================
    //  THE SAME OBJECT, NOT A COPY. Not rebuilt from a file on the other
    //  side - the same EntityId, the same address, and whatever its
    //  components were holding. That is the whole point: a behaviour that has
    //  been counting something goes on counting.
    // ======================================================================
    Entity* after = box.scene.Get(id);
    REQUIRE(after != nullptr);
    CHECK(after == before);
    CHECK(after->Name() == "Manager");
    CHECK(after->Transform().LocalPosition().x == doctest::Approx(123.0f));
    CHECK(after->Transform().LocalPosition().y == doctest::Approx(456.0f));

    // And the new scene arrived around it.
    CHECK_FALSE(box.scene.Find("Other").IsNull());
    CHECK(box.scene.PersistentCount() == 1);
}

TEST_CASE("a survivor is still findable by name, and through its transform") {
    SceneSandbox box("lookups");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId id = box.scene.Find("Manager");
    box.scene.SetPersistent(id, true);

    Entity* entity = box.scene.Get(id);
    REQUIRE(entity != nullptr);
    Transform2D* transform = &entity->Transform();

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // THE NAME INDEX, which EntityRef and GameObjectRef resolve through.
    CHECK(box.scene.Find("Manager") == id);

    // AND THE REVERSE LOOKUP. EntityOf is how a collision, a parent lookup
    // and the editor's picking all get from a Transform2D back to its entity -
    // clearing it on a scene change would leave every survivor unreachable
    // through half the engine.
    CHECK(box.scene.EntityOf(*transform) == entity);
}

TEST_CASE("a survivor's children come with it") {
    SceneSandbox box("children");
    box.WriteScene("scenes/a.oscene",
                   R"({"name":"S","entities":[)"
                   R"({"name":"Root","components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"Child","parent":"Root",)"
                   R"("components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"Grandchild","parent":"Child",)"
                   R"("components":[{"type":"TransformComponent"}]}]})");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId root  = box.scene.Find("Root");
    const EntityId child = box.scene.Find("Child");
    const EntityId grand = box.scene.Find("Grandchild");
    REQUIRE_FALSE(root.IsNull());
    REQUIRE_FALSE(child.IsNull());
    REQUIRE_FALSE(grand.IsNull());

    // ONLY THE ROOT IS MARKED. The descendants are not, and have to be
    // carried anyway - which is why the surviving set is closed over children
    // before anything is destroyed. Doing it as we went would free a child
    // before reaching the parent that was keeping it.
    box.scene.SetPersistent(root, true);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    CHECK(box.scene.Get(root) != nullptr);
    CHECK(box.scene.Get(child) != nullptr);
    CHECK(box.scene.Get(grand) != nullptr);

    // And the hierarchy is intact, not merely alive.
    Entity* childEntity = box.scene.Get(child);
    REQUIRE(childEntity != nullptr);
    CHECK(childEntity->Transform().Parent() == &box.scene.Get(root)->Transform());
}

TEST_CASE("a marked child of a doomed parent is detached rather than orphaned") {
    SceneSandbox box("detach");
    box.WriteScene("scenes/a.oscene",
                   R"({"name":"S","entities":[)"
                   R"({"name":"Root","components":[{"type":"TransformComponent",)"
                   R"("position":[100,0]}]},)"
                   R"({"name":"Child","parent":"Root",)"
                   R"("components":[{"type":"TransformComponent",)"
                   R"("position":[10,0]}]}]})");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId child = box.scene.Find("Child");
    REQUIRE_FALSE(child.IsNull());

    // ======================================================================
    //  THE AWKWARD CASE. Marking a CHILD and not its parent: the parent's
    //  Transform2D is about to be freed, and a child still pointing at it
    //  would read freed memory on the first frame of the new scene - as a
    //  POSITION, so the symptom is an object somewhere absurd rather than a
    //  crash.
    //
    //  Unity avoids the question by only honouring this on a root. Detaching
    //  says the same thing out loud and keeps the object.
    // ======================================================================
    box.scene.SetPersistent(child, true);

    Entity* before = box.scene.Get(child);
    REQUIRE(before != nullptr);
    const Vec2 worldBefore = before->Transform().WorldPosition();

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    Entity* after = box.scene.Get(child);
    REQUIRE(after != nullptr);
    CHECK(after->Transform().Parent() == nullptr);

    // AND IT DID NOT MOVE. Detaching is bookkeeping; an object that jumped
    // across the level on a scene change would be far harder to explain.
    CHECK(after->Transform().WorldPosition().x == doctest::Approx(worldBefore.x));
    CHECK(after->Transform().WorldPosition().y == doctest::Approx(worldBefore.y));

    CHECK(box.scene.Get(box.scene.Find("Root")) == nullptr);
}

// ---------------------------------------------------------------------------
//  WHAT MUST NOT CARRY IT
// ---------------------------------------------------------------------------

TEST_CASE("nothing survives a snapshot restore, which is how Stop works") {
    SceneSandbox box("snapshot");
    box.WriteSimple("scenes/a.oscene", "Manager");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId id = box.scene.Find("Manager");
    box.scene.SetPersistent(id, true);

    std::string snapshot;
    REQUIRE(box.scene.SaveToString(snapshot, error));

    // ======================================================================
    //  PRESSING STOP MUST LEAVE NOTHING BEHIND. LoadFromString is how play
    //  mode puts the authored scene back, and an object that outlived it
    //  would leave the editor holding something the scene file does not
    //  describe - unsaveable, and gone the next time the project opened.
    // ======================================================================
    REQUIRE(box.scene.LoadFromString(snapshot, error));
    CHECK(box.scene.PersistentCount() == 0);
}

TEST_CASE("nothing survives starting a new scene") {
    SceneSandbox box("newscene");
    box.WriteSimple("scenes/a.oscene", "Manager");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    box.scene.SetPersistent(box.scene.Find("Manager"), true);

    box.scene.StartNewScene();
    CHECK(box.scene.PersistentCount() == 0);
    CHECK(box.scene.Find("Manager").IsNull());
}

TEST_CASE("a survivor is not written into the scene it has arrived in") {
    SceneSandbox box("notsaved");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Local");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    box.scene.SetPersistent(box.scene.Find("Manager"), true);
    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // ======================================================================
    //  OTHERWISE EVERY VISIT MAKES ANOTHER ONE. The manager exists to be a
    //  singleton; writing it into scene B's file would produce one more copy
    //  of it on each load of B, for ever.
    // ======================================================================
    std::string text;
    REQUIRE(box.scene.SaveToString(text, error));

    const Json out = Json::parse(text, nullptr, false);
    REQUIRE_FALSE(out.is_discarded());

    bool sawManager = false;
    bool sawLocal   = false;
    for (const Json& entity : out["entities"]) {
        const std::string name = entity.value("name", std::string());
        sawManager = sawManager || name == "Manager";
        sawLocal   = sawLocal || name == "Local";
    }
    CHECK_FALSE(sawManager);
    CHECK(sawLocal);
}

// ---------------------------------------------------------------------------
//  TWO THINGS WITH ONE NAME, which is what a scene change onto a manager
//  produces - and which is Unity's behaviour too.
// ---------------------------------------------------------------------------

TEST_CASE("the survivor keeps the name when the new scene has one of its own") {
    SceneSandbox box("collide");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Manager");   // the same name

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId survivor = box.scene.Find("Manager");
    box.scene.SetPersistent(survivor, true);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // ======================================================================
    //  THE NAME GOES ON MEANING THE ONE THAT WAS ALREADY HERE.
    //
    //  The usual singleton check - the one Unity users write - is for the
    //  newcomer to look for an existing one and destroy itself, and that
    //  lookup is by name. Letting the newcomer win would make the survivor
    //  unfindable by the very check meant to find it.
    // ======================================================================
    CHECK(box.scene.Find("Manager") == survivor);
    CHECK(box.scene.Get(survivor) != nullptr);
}

TEST_CASE("destroying one of two entities sharing a name leaves the other findable") {
    // A latent bug the collision above exposes: the erase on destroy used to
    // be unconditional, so removing the newcomer took the SURVIVOR's index
    // entry out and left a live object nothing could reach by name.
    SceneSandbox box("dupdestroy");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Manager");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId survivor = box.scene.Find("Manager");
    box.scene.SetPersistent(survivor, true);
    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // Find the newcomer: the other entity of that name.
    EntityId newcomer;
    box.scene.ForEach([&](Entity& entity) {
        if (entity.Name() == "Manager" && entity.Id() != survivor) {
            newcomer = entity.Id();
        }
    });
    REQUIRE_FALSE(newcomer.IsNull());

    box.scene.DestroyEntityImmediate(newcomer);

    CHECK(box.scene.Find("Manager") == survivor);
    CHECK(box.scene.Get(survivor) != nullptr);
}

// ---------------------------------------------------------------------------
//  The flag itself
// ---------------------------------------------------------------------------

TEST_CASE("marking is idempotent, and can be taken back") {
    SceneSandbox box("flag");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId id     = box.scene.Find("Manager");
    const Entity*  before = box.scene.Get(id);
    REQUIRE(before != nullptr);

    box.scene.SetPersistent(id, true);
    box.scene.SetPersistent(id, true);   // twice is harmless
    CHECK(box.scene.PersistentCount() == 1);

    // A manager that is done - a tutorial controller, say - stops surviving.
    box.scene.SetPersistent(id, false);
    CHECK_FALSE(box.scene.IsPersistent(id));
    CHECK(box.scene.PersistentCount() == 0);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // ASSERTED ON THE NAME. Neither the id nor the address can tell this: a
    // cleared slot vector hands out the same index and generation again, and
    // the allocator cheerfully hands back the same block for the new Entity.
    // See the note on id aliasing in the first case in this file.
    CHECK(box.scene.Find("Manager").IsNull());
    CHECK(box.scene.PersistentCount() == 0);
}

TEST_CASE("marking a stale id does nothing rather than writing into a slot") {
    SceneSandbox box("stale");
    box.WriteSimple("scenes/a.oscene", "Gone");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId id = box.scene.Find("Gone");
    box.scene.DestroyEntityImmediate(id);

    // The slot is free and its generation has moved on, so this id names
    // nothing. Marking it must not set a flag that a future occupant inherits.
    box.scene.SetPersistent(id, true);
    CHECK_FALSE(box.scene.IsPersistent(id));
    CHECK(box.scene.PersistentCount() == 0);
}

TEST_CASE("a slot freed by a scene change is reused without inheriting anything") {
    SceneSandbox box("reuse");
    box.WriteScene("scenes/a.oscene",
                   R"({"name":"S","entities":[)"
                   R"({"name":"Manager","components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"Doomed","components":[{"type":"TransformComponent"}]}]})");
    box.WriteScene("scenes/b.oscene",
                   R"({"name":"S","entities":[)"
                   R"({"name":"New1","components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"New2","components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"New3","components":[{"type":"TransformComponent"}]}]})");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId manager = box.scene.Find("Manager");
    box.scene.SetPersistent(manager, true);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // THE SURVIVOR'S SLOT DID NOT MOVE. An EntityId is an index plus a
    // generation, so compacting the slot vector would silently invalidate
    // every id a script was holding - including the one its own behaviour
    // uses to find itself.
    CHECK(box.scene.Get(manager) != nullptr);

    // And the newcomers filled the gaps without inheriting the flag.
    CHECK(box.scene.PersistentCount() == 1);
    for (const char* name : {"New1", "New2", "New3"}) {
        const EntityId id = box.scene.Find(name);
        REQUIRE_FALSE(id.IsNull());
        CHECK_FALSE(box.scene.IsPersistent(id));
    }
}

TEST_CASE("several survivors all come through, with their components") {
    SceneSandbox box("several");
    box.WriteScene("scenes/a.oscene",
                   R"({"name":"S","entities":[)"
                   R"({"name":"A","components":[{"type":"TransformComponent",)"
                   R"("position":[1,1]}]},)"
                   R"({"name":"B","components":[{"type":"TransformComponent",)"
                   R"("position":[2,2]}]},)"
                   R"({"name":"C","components":[{"type":"TransformComponent",)"
                   R"("position":[3,3]}]}]})");
    box.WriteSimple("scenes/b.oscene", "Other");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    box.scene.SetPersistent(box.scene.Find("A"), true);
    box.scene.SetPersistent(box.scene.Find("C"), true);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    CHECK(box.scene.PersistentCount() == 2);
    REQUIRE_FALSE(box.scene.Find("A").IsNull());
    REQUIRE(box.scene.Find("B").IsNull());
    REQUIRE_FALSE(box.scene.Find("C").IsNull());

    CHECK(box.scene.Get(box.scene.Find("C"))
              ->Transform()
              .LocalPosition()
              .x == doctest::Approx(3.0f));
}

TEST_CASE("surviving twice over, through two scene changes") {
    SceneSandbox box("twice");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteSimple("scenes/b.oscene", "B");
    box.WriteSimple("scenes/c.oscene", "C");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId id = box.scene.Find("Manager");
    box.scene.SetPersistent(id, true);

    // The mark has to survive the load as well as the entity, or a manager
    // would make exactly one hop.
    REQUIRE(box.scene.Load("scenes/b.oscene", error));
    REQUIRE(box.scene.IsPersistent(id));

    REQUIRE(box.scene.Load("scenes/c.oscene", error));
    CHECK(box.scene.Get(id) != nullptr);
    CHECK_FALSE(box.scene.Find("C").IsNull());
}

// ---------------------------------------------------------------------------
//  TWO DEFECTS A REVIEW PASS FOUND, pinned so they stay fixed.
// ---------------------------------------------------------------------------

TEST_CASE("a slot freed by a destroy does not hand on persistence") {
    // ======================================================================
    //  DestroyEntityImmediate scrubs the prefab link and the derived flag
    //  from a slot for exactly this reason, and persistence was the third one
    //  that needed it. Left behind, a new entity in a reused slot would
    //  inherit persistence it never asked for, outlive a scene change nobody
    //  meant it to, AND be left out of every save - so it would also vanish
    //  the next time the project opened.
    // ======================================================================
    SceneSandbox box("slotflag");
    box.WriteSimple("scenes/a.oscene", "Manager");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));

    const EntityId first = box.scene.Find("Manager");
    box.scene.SetPersistent(first, true);
    REQUIRE(box.scene.PersistentCount() == 1);

    box.scene.DestroyEntityImmediate(first);
    CHECK(box.scene.PersistentCount() == 0);

    // The next entity lands in that slot.
    const EntityId second = box.scene.CreateEntity("Fresh");
    REQUIRE_FALSE(second.IsNull());
    CHECK_FALSE(box.scene.IsPersistent(second));
    CHECK(box.scene.PersistentCount() == 0);
}

TEST_CASE("a name collision does not re-parent the incoming scene's own hierarchy") {
    // ======================================================================
    //  THE SUBTLE ONE. A scene file resolves its PARENTS by name, so a
    //  survivor holding "Manager" throughout the build would have the new
    //  scene's child of Manager parented to the SURVIVOR - quietly rewriting
    //  the hierarchy of the scene being loaded, in a way that looks like the
    //  file being wrong.
    //
    //  So the newcomer owns the name while the file is read, and the survivor
    //  takes it back afterwards.
    // ======================================================================
    SceneSandbox box("reparent");
    box.WriteSimple("scenes/a.oscene", "Manager");
    box.WriteScene("scenes/b.oscene",
                   R"({"name":"B","entities":[)"
                   R"({"name":"Manager","components":[{"type":"TransformComponent"}]},)"
                   R"({"name":"Minion","parent":"Manager",)"
                   R"("components":[{"type":"TransformComponent"}]}]})");

    std::string error;
    REQUIRE(box.scene.Load("scenes/a.oscene", error));
    const EntityId survivor = box.scene.Find("Manager");
    box.scene.SetPersistent(survivor, true);

    REQUIRE(box.scene.Load("scenes/b.oscene", error));

    // The newcomer is the other Manager.
    EntityId newcomer;
    box.scene.ForEach([&](Entity& entity) {
        if (entity.Name() == "Manager" && entity.Id() != survivor) {
            newcomer = entity.Id();
        }
    });
    REQUIRE_FALSE(newcomer.IsNull());

    // MINION BELONGS TO ITS OWN SCENE'S MANAGER, not to the survivor.
    Entity* minion = box.scene.Get(box.scene.Find("Minion"));
    REQUIRE(minion != nullptr);
    CHECK(minion->Transform().Parent() == &box.scene.Get(newcomer)->Transform());

    // AND THE NAME STILL MEANS THE SURVIVOR afterwards, so the singleton
    // check the newcomer runs in OnStart finds something to stand down to.
    CHECK(box.scene.Find("Manager") == survivor);
}
