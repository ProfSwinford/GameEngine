// ============================================================================
//  test_scriptapi.cpp - the surface a script writes against.
//
//  ==========================================================================
//  WHAT THESE TESTS ARE FOR
//
//  Three different jobs, and they are kept apart on purpose:
//
//  1. THE CONVENIENCES DO WHAT THE LONG SPELLING DID. Instantiate with a
//     placement, the typed component lookups, the tag searches. Each of these
//     replaced several lines somebody was writing out by hand, and the thing
//     worth proving is that the short version is not subtly different.
//
//  2. SWITCHING AN ENTITY OFF REACHES EVERY SYSTEM. One test per system, by
//     name, and that is not thoroughness for its own sake - Entity.h argues
//     that a flag honoured by the renderer and not by the physics is WORSE
//     than no flag at all, because the thing disappears and goes on hurting
//     you. A claim like that is only worth making if each half is checked
//     separately, so each half gets its own case that fails on its own.
//
//  3. THE SHARP EDGES STAY SHARP. A fuse must not switch collisions off while
//     it burns. A raycast must find the nearest thing rather than the nearest
//     of the first sixty-four. Two entities with one name must be reported.
//     Each of those was a real bug or a near miss, and a test is the only
//     thing that keeps the answer from drifting back.
//
//  ==========================================================================
//  WHAT CANNOT BE TESTED HERE, so that nobody reads a gap as coverage
//
//  No compiled script runs in these tests. ScriptBehaviour's own methods need
//  a bound behaviour, which needs a script DLL, which needs the editor's
//  compiler - so this file tests what those methods FORWARD TO, through the
//  same Scene and Entity calls, and the forwarding itself is one line each.
//
//  ==========================================================================
//  TWO ACTIVE GATES ARE NOT COVERED HERE, AND IT IS WORTH NAMING THEM
//
//  Every system that must honour Entity::SetActive has a case below, and each
//  was confirmed by breaking the check and watching exactly that case fail -
//  EXCEPT two:
//
//    SpriteRenderSystem::RenderRange   the sprite DRAW gate
//    TilemapRenderSystem               the tilemap DRAW gate
//
//  Both need a window and a renderer to observe, and these tests have
//  neither - "was it drawn?" has no headless answer, and the registration
//  count (which IS checked) stays the same either way by design. Breaking
//  either of those two checks currently breaks no test.
//
//  They are exercised by the editor and player end-to-end run, which does
//  draw, and they are two lines each sitting next to seven that are tested.
//  Said here rather than left as a gap somebody later reads as coverage.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/core/LogBuffer.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/Pointer.h>
#include <engine/math/Transform2D.h>
#include <engine/math/Vec2.h>
#include <engine/physics/Collider.h>
#include <engine/physics/Physics2D.h>
#include <engine/physics/Rigidbody.h>
#include <engine/scene/Component.h>
#include <engine/scene/CameraComponent.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Prefab.h>
#include <engine/scene/PrefabRef.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/SpinComponent.h>
#include <engine/scene/UI.h>
#include <engine/render/Camera.h>

#include <cstdlib>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace eng;

namespace {

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

// The same shape as test_prefab.cpp's fixture, and for the same reasons - see
// the long note there about why the component factory has to be told what the
// built-in type names mean when there is no Engine to do it.
//
// DeferredOps IS CLEARED AT BOTH ENDS. The queues are file-static, so a test
// that leaves something queued hands it to whichever test runs next - and a
// leftover id from a destroyed scene can match a brand new entity, which is
// the exact failure Engine::ApplyPendingSceneRequest clears them to avoid.
struct Fixture {
    fs::path   root;
    FileSystem fileSystem;
    Scene      scene;
    Scene*     previous = nullptr;

    explicit Fixture(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-scriptapi-") + name);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "prefabs", ec);

        SetAssetRoot(root.string().c_str());
        REQUIRE(fileSystem.Init(BootConfig{}));

        ComponentFactory::RegisterBuiltins();
        CollisionSystem::RegisterComponentTypes();
        SpinSystem::RegisterComponentTypes();
        ScriptSystem::RegisterComponentTypes();
        // The UI types too, for the "neither drawn nor clickable" case. Left
        // out at first, and the symptom was Add<UIRectComponent>() returning
        // null - because a component is created through the factory by NAME
        // and until something registers it, the name means nothing.
        UISystem::RegisterComponentTypes();

        PrefabLibrary::Clear();
        DeferredOps::Clear();
        SpriteRenderSystem::Clear();
        CollisionSystem::Clear();
        RigidbodySystem::Clear();
        SpinSystem::Clear();

        previous = Scene::Active();
        Scene::SetActive(&scene);
    }

    ~Fixture() {
        Scene::SetActive(previous);
        scene.Unload();
        DeferredOps::Clear();
        // The UI pointer is one file-static set shared by the whole process,
        // and SetPointer marks it host-driven for good - so a case that moved
        // it hands that position to whichever case runs next. Put back where
        // nothing is being pointed at.
        UISystem::SetPointer(Vec2{}, Vec2{1280.0f, 720.0f}, false, false);
        PrefabLibrary::Clear();
        fileSystem.Shutdown();
        SetAssetRoot("");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    Fixture(const Fixture&)            = delete;
    Fixture& operator=(const Fixture&) = delete;

    void Write(const std::string& virtualPath, const Json& document) const {
        std::string error;
        REQUIRE(FileSystem::WriteTextFile(virtualPath, document.dump(2), error));
        PrefabLibrary::Forget(virtualPath);
    }

    // An entity with a box collider on it, which is what every physics case
    // here needs and what nothing else about them is about.
    EntityId WithCollider(const char* name, Vec2 at, Vec2 size = Vec2{10.0f, 10.0f}) {
        const EntityId id     = scene.CreateEntity(name);
        Entity*        entity = scene.Get(id);
        REQUIRE(entity != nullptr);
        entity->Transform().SetLocalPosition(at);
        auto* box = entity->Add<BoxColliderComponent>();
        REQUIRE(box != nullptr);
        box->SetSize(size);
        return id;
    }
};

Entity& EntityNamed(Scene& scene, const std::string& name) {
    Entity* entity = scene.FindEntity(name);
    REQUIRE_MESSAGE(entity != nullptr, "no entity called '" << name << "'");
    return *entity;
}

// A prefab with a root and one child. Deliberately uses only components that
// need no images and no compiled scripts.
//
// NOTE WHERE THE POSITION LIVES: inside a TransformComponent entry, not on the
// entity node. An entity node carries a name, an id and a parent, and
// everything else about it is a component - which is what makes a new
// component type need no change to the loader. Writing "position" on the
// entity instead compiles, loads without complaint, and silently places
// everything at the origin, which is how this comment came to be here.
Json TwoEntityPrefab() {
    Json transform        = Json::object();
    transform["type"]     = "TransformComponent";
    transform["position"] = Json::array({3.0, 4.0});
    transform["rotation"] = 0.5;
    transform["scale"]    = Json::array({1.0, 1.0});

    Json childTransform        = transform;
    childTransform["position"] = Json::array({-5.0, 0.0});
    childTransform["rotation"] = 0.0;

    Json spin                = Json::object();
    spin["type"]             = "SpinComponent";
    spin["radiansPerSecond"] = 1.0;

    Json rootNode          = Json::object();
    rootNode["id"]         = 1;
    rootNode["name"]       = "Bullet";
    rootNode["components"] = Json::array({transform, spin});

    Json childNode          = Json::object();
    childNode["id"]         = 2;
    childNode["parent"]     = 1;
    childNode["name"]       = "Trail";
    childNode["components"] = Json::array({childTransform});

    Json document        = Json::object();
    document["prefab"]   = 1;
    document["name"]     = "Bullet";
    document["root"]     = 1;
    document["entities"] = Json::array({rootNode, childNode});
    return document;
}

} // namespace

// ===========================================================================
//  A REAL, RUNNING SCRIPT - inside the test binary.
//
//  ==========================================================================
//  WHY THIS IS POSSIBLE AND WHY IT MATTERS
//
//  A script normally lives in its own DLL, compiled by the editor, and these
//  tests have no editor and no compiler. So the first draft of this file used
//  an UNRESOLVED script - a ScriptComponent naming a class that is not in the
//  build - and checked the bookkeeping around it.
//
//  THAT TURNED OUT TO TEST ALMOST NOTHING, and a mutation found it: breaking
//  the "a switched-off entity's scripts do not tick" check in
//  ScriptComponent::FixedTick broke no test at all, because an unresolved
//  script returns at `m_behaviour == nullptr` several lines earlier and never
//  reaches the check.
//
//  The fix is that the registry is just a table of function pointers, and
//  ENGINE_REGISTER_SCRIPT is just a file-scope object that fills one in. This
//  translation unit links against the engine, so a script declared HERE is as
//  real as one from a DLL: it binds, it starts, it ticks, and ScriptBehaviour's
//  own accessors work inside it.
//
//  Which means the things that were previously described as "forwarding, one
//  line each, not covered" are now covered by running them.
// ===========================================================================

namespace {

// Counts what happened to it, so a test can assert on the lifecycle rather
// than on its own copy of the bookkeeping.
class Probe final : public ScriptBehaviour {
public:
    int starts      = 0;
    int fixedTicks  = 0;
    int frameTicks  = 0;

    // Filled in by OnStart, so a test can check that the accessors work from
    // the first hook onwards - which is what ScriptComponent.h promises.
    std::string nameAtStart;
    bool        sawOwner     = false;
    bool        sawScene     = false;
    bool        sawTransform = false;

    void OnStart() {
        ++starts;
        nameAtStart  = Name();
        sawOwner     = Owner() != nullptr;
        sawScene     = GetScene() != nullptr;
        sawTransform = Transform() != nullptr;
    }

    void OnFixedUpdate() { ++fixedTicks; }
    void OnUpdate() { ++frameTicks; }
};

// A script that spawns on its first tick, which is the thing the whole
// exercise is about.
class Spawner final : public ScriptBehaviour {
public:
    PrefabRef what{"prefabs/bullet.oprefab"};
    EntityId  spawned{};
    int       attempts = 0;

    void OnFixedUpdate() {
        ++attempts;
        spawned = Instantiate(what, SpawnOptions{}.At(Vec2{42.0f, 0.0f}));
    }
};

// And one that removes itself, through the queue.
class Suicide final : public ScriptBehaviour {
public:
    void OnFixedUpdate() { DestroySelf(); }
};

// Counts contact events, so the "a switched-off script hears nothing" claim
// can be checked by sending one.
class Touchy final : public ScriptBehaviour {
public:
    int enters = 0;
    int stays  = 0;

    // OnStart is needed as well: a contact arriving before the first tick is
    // dropped, because delivering OnTriggerEnter to a behaviour that has not
    // started is the kind of surprise that makes scripting feel unreliable.
    void OnStart() {}
    void OnTriggerEnter(EntityId) { ++enters; }
    void OnTriggerStay(EntityId) { ++stays; }
};

// Reaches another entity through the verbs rather than through the scene.
class Seeker final : public ScriptBehaviour {
public:
    bool foundByTag    = false;
    bool foundByName   = false;
    bool foundOwnSpin  = false;
    bool foundInChild  = false;

    void OnStart() {
        foundByTag   = FindEntityWithTag("Target") != nullptr;
        foundByName  = FindEntity("Target") != nullptr;
        foundOwnSpin = Find<SpinComponent>() != nullptr;
        foundInChild = FindInChildren<BoxColliderComponent>() != nullptr;
    }
};

} // namespace

ENGINE_REGISTER_SCRIPT(Probe)
ENGINE_REGISTER_SCRIPT(Spawner)
ENGINE_REGISTER_SCRIPT(Suicide)
ENGINE_REGISTER_SCRIPT(Touchy)
ENGINE_REGISTER_SCRIPT(Seeker)

namespace {

// Attaches a registered script and hands back the running object.
template <class T>
T* AttachScript(Scene& scene, EntityId id, const char* scriptName) {
    Entity* entity = scene.Get(id);
    REQUIRE(entity != nullptr);

    Json node      = Json::object();
    node["script"] = scriptName;

    auto component = ComponentFactory::Create(ScriptComponent::kTypeName);
    REQUIRE(component != nullptr);
    std::string error;
    REQUIRE(component->Deserialize(node, error));
    auto* script = static_cast<ScriptComponent*>(entity->AddComponent(std::move(component)));
    REQUIRE(script != nullptr);
    REQUIRE_MESSAGE(script->IsResolved(),
                    "the script '" << scriptName << "' is not in this build");
    return static_cast<T*>(script->Behaviour());
}

} // namespace

// ===========================================================================
//  PART 1 - THE CONVENIENCES
// ===========================================================================

TEST_CASE("a transform can be turned back into the entity that owns it") {
    // ======================================================================
    //  THE ONE EVERYTHING ELSE IS BUILT ON. Transform2D::Children() hands back
    //  transforms, and before this there was no way to get from one to the
    //  components beside it - so "the sprite on my first child" could not be
    //  written at all, and the editor paid for the gap with a whole-scene scan
    //  per child. See the note at Scene::EntityOf.
    // ======================================================================
    Fixture world("entity-of");

    const EntityId parent = world.scene.CreateEntity("Tank");
    const EntityId child  = world.scene.CreateEntity("Turret");
    world.scene.Get(child)->Transform().SetParent(&world.scene.Get(parent)->Transform());

    const Transform2D& childTransform = world.scene.Get(child)->Transform();
    CHECK(world.scene.EntityOf(childTransform) == world.scene.Get(child));
    CHECK(world.scene.IdOf(childTransform) == child);

    // Through the tree, which is the spelling that was impossible.
    REQUIRE(world.scene.Get(parent)->Transform().ChildCount() == 1);
    Entity* viaTree =
        world.scene.EntityOf(*world.scene.Get(parent)->Transform().ChildAt(0));
    CHECK(viaTree == world.scene.Get(child));

    // A TRANSFORM THAT BELONGS TO NOTHING ANSWERS NULL, which is an ordinary
    // answer rather than an error - a Transform2D on the stack belongs to no
    // entity at all.
    Transform2D orphan;
    CHECK(world.scene.EntityOf(orphan) == nullptr);
    CHECK(world.scene.IdOf(orphan).IsNull());

    // AND A DESTROYED ENTITY'S TRANSFORM DOES NOT COME BACK AS SOMETHING ELSE.
    // The table is keyed on an address, and a freed Transform2D's address can
    // be handed straight back out for the next one - so a stale entry would
    // answer with the wrong entity rather than with none. The entry is
    // removed by TransformComponent::OnDetach, and the generation check in
    // IdOf is the second line of defence.
    world.scene.DestroyEntityImmediate(child);
    CHECK(world.scene.Get(parent)->Transform().ChildCount() == 0);
}

TEST_CASE("components can be found down the tree and up the chain") {
    Fixture world("find-in-tree");

    const EntityId root   = world.scene.CreateEntity("Ship");
    const EntityId middle = world.scene.CreateEntity("Wing");
    const EntityId leaf   = world.scene.CreateEntity("Engine");

    world.scene.Get(middle)->Transform().SetParent(&world.scene.Get(root)->Transform());
    world.scene.Get(leaf)->Transform().SetParent(&world.scene.Get(middle)->Transform());

    // The spin is TWO LEVELS DOWN, which is the case a search that only looked
    // at direct children would miss.
    REQUIRE(world.scene.Get(leaf)->Add<SpinComponent>() != nullptr);

    Entity& ship = EntityNamed(world.scene, "Ship");
    CHECK(ship.FindInChildren<SpinComponent>() != nullptr);
    CHECK(ship.Find<SpinComponent>() == nullptr);   // not on the root itself

    // Upwards, from the leaf.
    REQUIRE(ship.Add<BoxColliderComponent>() != nullptr);
    Entity& engine = EntityNamed(world.scene, "Engine");
    CHECK(engine.FindInParents<BoxColliderComponent>() != nullptr);

    // includeSelf, and the default is TRUE because Unity's is - the difference
    // is a silent one, so it is pinned rather than left to be discovered.
    CHECK(engine.FindInParents<SpinComponent>(true) != nullptr);
    CHECK(engine.FindInParents<SpinComponent>(false) == nullptr);

    // Every match, in the subtree.
    REQUIRE(middle != EntityId{});
    REQUIRE(world.scene.Get(middle)->Add<SpinComponent>() != nullptr);
    std::vector<SpinComponent*> spins;
    CHECK(ship.FindAllInChildren<SpinComponent>(spins) == 2);
    CHECK(spins.size() == 2);

    // The entity-level tree accessors.
    CHECK(ship.ChildCount() == 1);
    CHECK(ship.ChildAt(0) == &EntityNamed(world.scene, "Wing"));
    CHECK(ship.ChildAt(99) == nullptr);   // past the end answers null
    CHECK(engine.ParentEntity() == &EntityNamed(world.scene, "Wing"));
    CHECK(engine.RootEntity() == &ship);
    CHECK(ship.FindChild("Wing") != nullptr);

    // DIRECT CHILDREN ONLY, deliberately - see Entity::FindChild. "Engine" is
    // a grandchild, and a recursive search by name would find a thing further
    // away than something nearer that shares its name.
    CHECK(ship.FindChild("Engine") == nullptr);
}

TEST_CASE("several components of one type come back together") {
    Fixture world("find-all");

    const EntityId id     = world.scene.CreateEntity("Compound");
    Entity*        entity = world.scene.Get(id);
    REQUIRE(entity->Add<BoxColliderComponent>() != nullptr);
    REQUIRE(entity->Add<BoxColliderComponent>() != nullptr);
    REQUIRE(entity->Add<SpinComponent>() != nullptr);

    std::vector<BoxColliderComponent*> boxes;
    CHECK(entity->FindAll<BoxColliderComponent>(boxes) == 2);

    // CLEARED FIRST, not appended to. A function that added to whatever was
    // already in the vector would quietly double its contents on the second
    // call, which in a per-frame walk is a leak that looks like a logic bug.
    CHECK(entity->FindAll<BoxColliderComponent>(boxes) == 2);

    std::vector<SpinComponent*> spins;
    CHECK(entity->FindAll<SpinComponent>(spins) == 1);
}

TEST_CASE("a tag is not a name, and finds every entity that carries it") {
    Fixture world("tags");

    const EntityId a = world.scene.CreateEntity("Enemy_1");
    const EntityId b = world.scene.CreateEntity("Enemy_2");
    const EntityId c = world.scene.CreateEntity("Wall");

    world.scene.Get(a)->SetTag("Enemy");
    world.scene.Get(b)->SetTag("Enemy");

    CHECK(world.scene.CountWithTag("Enemy") == 2);
    CHECK(world.scene.FindByTag("Enemy") != nullptr);
    CHECK(world.scene.FindByTag("Boss") == nullptr);
    CHECK(world.scene.Get(c)->Tag().empty());

    // EMPTY IS UNTAGGED, AND HasTag("") IS FALSE. An empty required tag
    // answering true for everything is how an uninitialised variable turns
    // into a test that silently passes - see Entity::Tag.
    CHECK_FALSE(world.scene.Get(c)->HasTag(""));
    CHECK(world.scene.FindByTag("") == nullptr);
    CHECK(world.scene.CountWithTag("") == 0);

    // ROUND TRIP. A tag belongs to the entity, so it has to survive a save.
    std::string text;
    std::string error;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));
    CHECK(world.scene.CountWithTag("Enemy") == 2);
    CHECK(EntityNamed(world.scene, "Enemy_1").HasTag("Enemy"));

    // AND AN UNTAGGED ENTITY WRITES NO "tag" KEY. This is what makes adding
    // tags invisible to every scene file written before they existed - and
    // the claim is only worth making if something checks it.
    const Json document = ParseJson(text, error);
    REQUIRE(error.empty());
    bool sawWall = false;
    for (const Json& entity : document.at("entities")) {
        if (entity.at("name") == "Wall") {
            sawWall = true;
            CHECK_FALSE(entity.contains("tag"));
        }
    }
    CHECK(sawWall);
}

TEST_CASE("the whole scene can be searched by component type") {
    Fixture world("find-first-with");

    world.scene.CreateEntity("Empty");
    const EntityId withSpin = world.scene.CreateEntity("Spinner");
    REQUIRE(world.scene.Get(withSpin)->Add<SpinComponent>() != nullptr);

    CHECK(world.scene.FindFirstWith<SpinComponent>() == world.scene.Get(withSpin));
    CHECK(world.scene.FindFirstComponent<SpinComponent>() != nullptr);
    CHECK(world.scene.FindFirstWith<BoxColliderComponent>() == nullptr);
    CHECK(world.scene.CountWith<SpinComponent>() == 1);

    // SOMETHING DYING THIS TICK IS NOT FOUND, which is rule 1 in
    // DeferredOps.h applied to a search: "how many enemies are left" answered
    // with the one that died this step is the bug that rule creates.
    DeferredOps::QueueDestroy(withSpin);
    CHECK(world.scene.FindFirstWith<SpinComponent>() == nullptr);
    CHECK(world.scene.CountWith<SpinComponent>() == 0);

    // Unless the caller says otherwise, which is what the editor needs.
    CHECK(world.scene.CountWith<SpinComponent>(/*skipPendingDestroy=*/false) == 1);
}

TEST_CASE("the early-out walk actually stops") {
    Fixture world("for-each-until");

    for (int i = 0; i < 10; ++i) {
        world.scene.CreateEntity("E" + std::to_string(i));
    }

    int seen = 0;
    const bool ranToEnd = world.scene.ForEachUntil([&](Entity&) {
        ++seen;
        return seen < 3;   // stop on the third
    });

    CHECK(seen == 3);
    // FALSE MEANS THE CALLBACK STOPPED IT, which is what lets "did I find one"
    // be the return value rather than a captured flag.
    CHECK_FALSE(ranToEnd);

    int all = 0;
    CHECK(world.scene.ForEachUntil([&](Entity&) {
        ++all;
        return true;
    }));
    CHECK(all == 10);
}

TEST_CASE("spawning a prefab places it, parents it, and keeps the record in step") {
    // ======================================================================
    //  THE EXPLICIT ASK. Instantiate with a placement, which replaces fourteen
    //  lines - one of which is the trap: reparenting a prefab instance's root
    //  without writing PrefabInstance::parentName loses the parent on the next
    //  SAVE, not now, so nothing reports it until much later. See
    //  Scene::InstantiatePrefab(path, placement, outError).
    // ======================================================================
    Fixture world("instantiate-placed");
    world.Write("prefabs/bullet.oprefab", TwoEntityPrefab());

    const EntityId holder = world.scene.CreateEntity("Spawners");

    std::string error;
    const EntityId first = world.scene.InstantiatePrefab(
        "prefabs/bullet.oprefab",
        SpawnOptions{}.At(Vec2{100.0f, 50.0f}).Facing(1.25f).Under(holder), error);
    REQUIRE_MESSAGE(!first.IsNull(), error);

    Entity* spawned = world.scene.Get(first);
    REQUIRE(spawned != nullptr);

    // Placed where it was asked to go, in WORLD space, and still there after
    // being parented - which is the whole reason positionIsWorldSpace defaults
    // to true. See SpawnOptions.
    CHECK(ApproxEqual(spawned->Transform().WorldPosition(), Vec2{100.0f, 50.0f}));
    CHECK(spawned->Transform().Parent() ==
          &EntityNamed(world.scene, "Spawners").Transform());

    // THE RECORD FOLLOWED THE TREE. Without this the instance saves at the top
    // level and comes back unparented.
    const PrefabInstance* record = world.scene.PrefabInstanceOf(first);
    REQUIRE(record != nullptr);
    CHECK(record->parentName == "Spawners");

    // Proven the only way that matters: through a save and load.
    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));
    Entity& restored = EntityNamed(world.scene, "Bullet");
    REQUIRE(restored.Transform().Parent() != nullptr);
    CHECK(restored.Transform().Parent() ==
          &EntityNamed(world.scene, "Spawners").Transform());
    CHECK(ApproxEqual(restored.Transform().WorldPosition(), Vec2{100.0f, 50.0f}));
}

TEST_CASE("a default placement leaves the prefab exactly where the asset says") {
    // ======================================================================
    //  THE OTHER HALF OF SpawnOptions, and the reason every field is a flag
    //  plus a value rather than a value alone. A default-constructed
    //  SpawnOptions must mean "don't touch it" - if it meant "put it at the
    //  origin at scale (1,1)", then Instantiate(path, {}) would be the
    //  DESTRUCTIVE spelling and the asset's own placement would be
    //  unreachable.
    // ======================================================================
    Fixture world("instantiate-default");
    world.Write("prefabs/bullet.oprefab", TwoEntityPrefab());

    std::string error;
    const EntityId withOptions =
        world.scene.InstantiatePrefab("prefabs/bullet.oprefab", SpawnOptions{}, error);
    REQUIRE_MESSAGE(!withOptions.IsNull(), error);

    // The asset says position [3, 4] and rotation 0.5.
    Entity* spawned = world.scene.Get(withOptions);
    REQUIRE(spawned != nullptr);
    CHECK(ApproxEqual(spawned->Transform().LocalPosition(), Vec2{3.0f, 4.0f}));
    CHECK(spawned->Transform().LocalRotation() == doctest::Approx(0.5f));
    CHECK(spawned->Transform().Parent() == nullptr);
}

TEST_CASE("a spawned prefab gets a unique name, and so does a spawned entity") {
    Fixture world("unique-names");
    world.Write("prefabs/bullet.oprefab", TwoEntityPrefab());

    std::string error;
    for (int i = 0; i < 3; ++i) {
        REQUIRE_FALSE(
            world.scene.InstantiatePrefab("prefabs/bullet.oprefab", error).IsNull());
    }

    // THREE DISTINCT NAMES. Names are an identity here - parents and entity
    // references are both recorded by name - so three live entities all called
    // "Bullet" is three things claiming to be one. See Scene::CreateEntity.
    CHECK(world.scene.Find("Bullet").IsNull() == false);
    CHECK(world.scene.Find("Bullet_1").IsNull() == false);
    CHECK(world.scene.Find("Bullet_2").IsNull() == false);

    // And the from-scratch route does the same, which is the one a script
    // reaches for when there is no prefab.
    const EntityId a = world.scene.CreateEntityUniquelyNamed("Spark");
    const EntityId b = world.scene.CreateEntityUniquelyNamed("Spark");
    CHECK(a != b);
    CHECK(world.scene.Get(a)->Name() != world.scene.Get(b)->Name());

    // With a placement, too.
    const EntityId placed = world.scene.CreateEntityUniquelyNamed(
        "Spark", SpawnOptions{}.At(Vec2{7.0f, 8.0f}));
    CHECK(ApproxEqual(world.scene.Get(placed)->Transform().WorldPosition(),
                      Vec2{7.0f, 8.0f}));
}

TEST_CASE("the maths helpers behave at their edges") {
    // The edges are the whole reason these exist rather than being written
    // inline at each call site - every one of them is a case the obvious
    // one-liner gets wrong.

    // MoveTowards ARRIVES, EXACTLY. A lerp never does, so `== target` is never
    // true and whatever was waiting on it never fires.
    CHECK(MoveTowards(0.0f, 10.0f, 3.0f) == doctest::Approx(3.0f));
    CHECK(MoveTowards(0.0f, 10.0f, 100.0f) == doctest::Approx(10.0f));   // no overshoot
    CHECK(MoveTowards(10.0f, 0.0f, 100.0f) == doctest::Approx(0.0f));    // backwards too
    CHECK(ApproxEqual(MoveTowards(Vec2{0.0f, 0.0f}, Vec2{10.0f, 0.0f}, 99.0f),
                      Vec2{10.0f, 0.0f}));
    // The degenerate one: already there, so there is no direction to move in
    // and the naive version divides by zero.
    CHECK(ApproxEqual(MoveTowards(Vec2{5.0f, 5.0f}, Vec2{5.0f, 5.0f}, 1.0f),
                      Vec2{5.0f, 5.0f}));

    // ClampMagnitude LEAVES A SHORT VECTOR ALONE. Normalized() * min(len, max)
    // would snap it UP to the limit, so a thing drifting slowly jumps to full
    // speed.
    CHECK(ApproxEqual(ClampMagnitude(Vec2{3.0f, 0.0f}, 10.0f), Vec2{3.0f, 0.0f}));
    CHECK(ClampMagnitude(Vec2{30.0f, 0.0f}, 10.0f).Length() == doctest::Approx(10.0f));
    CHECK(ApproxEqual(ClampMagnitude(Vec2{3.0f, 4.0f}, 0.0f), Vec2::Zero()));

    // Reflect: straight down onto a floor comes straight back up.
    CHECK(ApproxEqual(Reflect(Vec2{0.0f, -1.0f}, Vec2{0.0f, 1.0f}), Vec2{0.0f, 1.0f}));
    // And a glancing blow keeps its sideways component.
    const Vec2 glance = Reflect(Vec2{1.0f, -1.0f}, Vec2{0.0f, 1.0f});
    CHECK(glance.x == doctest::Approx(1.0f));
    CHECK(glance.y == doctest::Approx(1.0f));

    // Rotated, against the exact case Perpendicular already covers.
    CHECK(ApproxEqual(Rotated(Vec2{1.0f, 0.0f}, kPi * 0.5f), Vec2{0.0f, 1.0f}));
    CHECK(ApproxEqual(Rotated(Vec2{1.0f, 0.0f}, kPi * 0.5f),
                      Vec2{1.0f, 0.0f}.Perpendicular()));

    // ANGLES WRAP, and that is the point. The plain subtraction says -340 and
    // sends a turret the long way round.
    CHECK(DeltaAngle(0.1f, kTwoPi - 0.1f) == doctest::Approx(-0.2f));
    CHECK(std::fabs(WrapAngle(kTwoPi * 3.0f + 0.25f) - 0.25f) < 1e-4f);
    CHECK(MoveTowardsAngle(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
    CHECK(MoveTowardsAngle(0.0f, 0.1f, 99.0f) == doctest::Approx(0.1f));  // arrives

    // SignedAngle has a SIGN, which is the question "which way do I turn" -
    // acos(Dot(..)) does not and cannot.
    CHECK(SignedAngle(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}) == doctest::Approx(kPi * 0.5f));
    CHECK(SignedAngle(Vec2{0.0f, 1.0f}, Vec2{1.0f, 0.0f}) ==
          doctest::Approx(-kPi * 0.5f));

    CHECK(Clamp01(-5.0f) == doctest::Approx(0.0f));
    CHECK(Clamp01(5.0f) == doctest::Approx(1.0f));
    CHECK(InverseLerp(10.0f, 20.0f, 15.0f) == doctest::Approx(0.5f));
    // A zero-length range answers 0 rather than dividing.
    CHECK(InverseLerp(5.0f, 5.0f, 5.0f) == doctest::Approx(0.0f));
}

TEST_CASE("a transform can be aimed, and turned about a point") {
    Fixture world("aim");

    const EntityId id     = world.scene.CreateEntity("Turret");
    Transform2D&   turret = world.scene.Get(id)->Transform();

    turret.LookAt(Vec2{0.0f, 10.0f});
    CHECK(turret.WorldRotation() == doctest::Approx(kPi * 0.5f));
    CHECK(ApproxEqual(turret.Right(), Vec2{0.0f, 1.0f}));
    CHECK(ApproxEqual(turret.Up(), Vec2{-1.0f, 0.0f}));

    // A TARGET ON TOP OF IT LEAVES THE ROTATION ALONE. atan2(0,0) is 0, so the
    // unguarded version snaps to facing right the instant two things touch -
    // which looks like a glitch at the worst possible moment.
    turret.LookAt(turret.WorldPosition());
    CHECK(turret.WorldRotation() == doctest::Approx(kPi * 0.5f));

    // RotateAround moves AND turns.
    const EntityId moonId = world.scene.CreateEntity("Moon");
    Transform2D&   moon   = world.scene.Get(moonId)->Transform();
    moon.SetWorldPosition(Vec2{10.0f, 0.0f});
    moon.RotateAround(Vec2::Zero(), kPi * 0.5f);
    CHECK(ApproxEqual(moon.WorldPosition(), Vec2{0.0f, 10.0f}));
    CHECK(moon.WorldRotation() == doctest::Approx(kPi * 0.5f));

    // World rotation and scale, under a parent - the halves that were missing
    // next to SetWorldPosition.
    const EntityId parentId = world.scene.CreateEntity("Rig");
    Transform2D&   rig      = world.scene.Get(parentId)->Transform();
    rig.SetLocalRotation(1.0f);

    const EntityId childId = world.scene.CreateEntity("Mounted");
    Transform2D&   mounted = world.scene.Get(childId)->Transform();
    mounted.SetParent(&rig);

    // Rotation is exact under any parent at all, because rotations simply add
    // up the chain.
    mounted.SetWorldRotation(0.25f);
    CHECK(mounted.WorldRotation() == doctest::Approx(0.25f));

    // ======================================================================
    //  SCALE IS EXACT ONLY WHERE IT CAN BE, and the boundary is worth pinning
    //  rather than discovering.
    //
    //  When a NON-UNIFORM SCALE sits above a ROTATION in the chain - and this
    //  node's own rotation counts - the combined transform contains shear: the
    //  axes stop being at right angles and no (x, y) pair describes the
    //  result. That is a fact about hierarchies rather than about this engine;
    //  Unity names its equivalent `lossyScale` for the same reason. See
    //  Transform2D::SetWorldScale.
    //
    //  WHICH MAKES THE BOUNDARY WORTH PINNING, because "it works here and not
    //  there" is otherwise something somebody discovers at a bad moment. Both
    //  exact cases below, and then the lossy one, asserted as lossy.
    // ======================================================================

    // Exact: the parent is rotated, but scaled EVENLY.
    rig.SetLocalScale(Vec2{2.0f, 2.0f});
    mounted.SetWorldScale(Vec2{1.0f, 1.0f});
    CHECK(ApproxEqual(mounted.WorldScale(), Vec2{1.0f, 1.0f}));

    // Exact: the parent is scaled UNEVENLY, and nothing below it is rotated.
    rig.SetLocalRotation(0.0f);
    rig.SetLocalScale(Vec2{2.0f, 4.0f});
    mounted.SetLocalRotation(0.0f);
    mounted.SetWorldScale(Vec2{3.0f, 3.0f});
    CHECK(ApproxEqual(mounted.WorldScale(), Vec2{3.0f, 3.0f}));
    CHECK(mounted.LocalScale().x == doctest::Approx(1.5f));
    CHECK(mounted.LocalScale().y == doctest::Approx(0.75f));

    // LOSSY, and asserted as such: an uneven parent scale with a rotation
    // under it. What comes back is the length of the sheared axes, which is
    // not what was asked for - and the point of this assertion is that the
    // header says so rather than that the number is right.
    mounted.SetLocalRotation(0.7f);
    mounted.SetWorldScale(Vec2{3.0f, 3.0f});
    CHECK_FALSE(ApproxEqual(mounted.WorldScale(), Vec2{3.0f, 3.0f}));
    // Still finite and still positive, so nothing downstream breaks.
    CHECK(std::isfinite(mounted.WorldScale().x));
    CHECK(mounted.WorldScale().x > 0.0f);

    // A PARENT FLAT ON AN AXIS LEAVES THAT AXIS ALONE rather than dividing by
    // zero. The division produces an infinity that turns every world position
    // in the subtree into a NaN, and NaN positions are silent - the sprites
    // simply stop appearing. See Transform2D::SetWorldScale.
    rig.SetLocalScale(Vec2{0.0f, 2.0f});
    const Vec2 before = mounted.LocalScale();
    mounted.SetWorldScale(Vec2{5.0f, 5.0f});
    CHECK(mounted.LocalScale().x == doctest::Approx(before.x));   // untouched
    CHECK(mounted.LocalScale().y == doctest::Approx(2.5f));       // 5 / 2
    CHECK(std::isfinite(mounted.WorldPosition().x));
}

TEST_CASE("a prefab reference is a path, and says whether it is set") {
    PrefabRef empty;
    CHECK_FALSE(empty.IsSet());
    CHECK(empty.Path().empty());

    // Not explicit, so a script can state a default in its own code - the same
    // thing EntityRef allows.
    PrefabRef named{"prefabs/bullet.oprefab"};
    CHECK(named.IsSet());
    CHECK(named.Path() == "prefabs/bullet.oprefab");

    named.Clear();
    CHECK_FALSE(named.IsSet());

    // IT DOES NOT LOOK AT THE DISK, deliberately. A reference to a prefab that
    // does not exist yet is an ordinary state for a level being built, and the
    // Inspector reports it rather than this type refusing it.
    PrefabRef missing{"prefabs/not-there.oprefab"};
    CHECK(missing.IsSet());
}

// ===========================================================================
//  PART 2 - SWITCHING AN ENTITY OFF REACHES EVERY SYSTEM
//
//  One case per system, each of which fails on its own. See Entity.h for why
//  this is the one feature in the engine where partial is worse than absent.
// ===========================================================================

TEST_CASE("switching an entity off is inherited by everything under it") {
    Fixture world("active-hierarchy");

    const EntityId parent = world.scene.CreateEntity("Menu");
    const EntityId child  = world.scene.CreateEntity("Button");
    const EntityId grand  = world.scene.CreateEntity("Label");

    world.scene.Get(child)->Transform().SetParent(&world.scene.Get(parent)->Transform());
    world.scene.Get(grand)->Transform().SetParent(&world.scene.Get(child)->Transform());

    CHECK(world.scene.Get(grand)->IsActiveInHierarchy());

    world.scene.Get(parent)->SetActive(false);

    // THE CHILD'S OWN FLAG IS UNTOUCHED, and that distinction is the whole
    // reason there are two accessors: switching the parent back on has to
    // restore exactly what was there before, which it cannot do if switching
    // it off rewrote every descendant.
    CHECK(world.scene.Get(child)->IsActiveSelf());
    CHECK_FALSE(world.scene.Get(child)->IsActiveInHierarchy());
    CHECK_FALSE(world.scene.Get(grand)->IsActiveInHierarchy());

    world.scene.Get(parent)->SetActive(true);
    CHECK(world.scene.Get(grand)->IsActiveInHierarchy());

    // NOT REFERENCE COUNTED. Off twice and on once leaves it on.
    world.scene.Get(parent)->SetActive(false);
    world.scene.Get(parent)->SetActive(false);
    world.scene.Get(parent)->SetActive(true);
    CHECK(world.scene.Get(parent)->IsActiveInHierarchy());

    // AND IT SURVIVES A SAVE, because an entity that was switched off in the
    // editor has to come back switched off.
    world.scene.Get(child)->SetActive(false);
    std::string text;
    std::string error;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));
    CHECK_FALSE(EntityNamed(world.scene, "Button").IsActiveSelf());
    CHECK(EntityNamed(world.scene, "Menu").IsActiveSelf());

    // An ON entity writes no "active" key, which is what made adding this
    // change nothing in any existing scene file.
    const Json document = ParseJson(text, error);
    REQUIRE(error.empty());
    for (const Json& entity : document.at("entities")) {
        if (entity.at("name") == "Menu") {
            CHECK_FALSE(entity.contains("active"));
        }
        if (entity.at("name") == "Button") {
            CHECK(entity.contains("active"));
            CHECK(entity.at("active") == false);
        }
    }
}

TEST_CASE("a switched-off entity does not collide") {
    Fixture world("active-collision");

    // Two boxes right on top of each other, on layers that see each other.
    const EntityId a = world.WithCollider("A", Vec2{0.0f, 0.0f});
    const EntityId b = world.WithCollider("B", Vec2{1.0f, 0.0f});

    CollisionSystem system;
    system.Update(1.0f / 60.0f);
    CHECK(CollisionSystem::ActivePairCount() > 0);

    // SWITCHED OFF, AND NOW THERE IS NO CONTACT. This is the half that matters
    // most: a flag the renderer honoured and this one did not would make the
    // thing invisible and still lethal.
    world.scene.Get(a)->SetActive(false);
    system.Update(1.0f / 60.0f);
    CHECK(CollisionSystem::ActivePairCount() == 0);

    world.scene.Get(a)->SetActive(true);
    system.Update(1.0f / 60.0f);
    CHECK(CollisionSystem::ActivePairCount() > 0);

    // And through a parent, which is how a whole subtree gets switched off.
    const EntityId holder = world.scene.CreateEntity("Holder");
    world.scene.Get(a)->Transform().SetParent(&world.scene.Get(holder)->Transform(),
                                              /*keepWorldTransform=*/true);
    world.scene.Get(holder)->SetActive(false);
    system.Update(1.0f / 60.0f);
    CHECK(CollisionSystem::ActivePairCount() == 0);

    CHECK(b != EntityId{});
}

TEST_CASE("a switched-off entity is invisible to every physics query") {
    Fixture world("active-queries");

    const EntityId target = world.WithCollider("Target", Vec2{50.0f, 0.0f},
                                               Vec2{20.0f, 20.0f});

    // Every shape of query, because "invisible to everything" is the claim and
    // one of them answering differently is exactly the half-switch the design
    // refuses.
    CHECK(static_cast<bool>(
        Physics2D::Raycast(Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 200.0f)));
    CHECK(Physics2D::OverlapPoint(Vec2{50.0f, 0.0f}) != nullptr);
    ColliderComponent* found[8] = {};
    CHECK(Physics2D::OverlapCircle(Vec2{50.0f, 0.0f}, 5.0f, found, 8) == 1);

    world.scene.Get(target)->SetActive(false);

    CHECK_FALSE(static_cast<bool>(
        Physics2D::Raycast(Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 200.0f)));
    CHECK(Physics2D::OverlapPoint(Vec2{50.0f, 0.0f}) == nullptr);
    CHECK(Physics2D::OverlapCircle(Vec2{50.0f, 0.0f}, 5.0f, found, 8) == 0);
}

TEST_CASE("a switched-off entity's rigidbody does not move, and keeps its velocity") {
    Fixture world("active-rigidbody");

    const EntityId id     = world.scene.CreateEntity("Crate");
    Entity*        entity = world.scene.Get(id);
    auto*          body   = entity->Add<Rigidbody2DComponent>();
    REQUIRE(body != nullptr);
    body->SetBodyType(RigidbodyType::Dynamic);
    body->SetGravityScale(0.0f);
    body->SetVelocity(Vec2{100.0f, 0.0f});

    RigidbodySystem system;
    system.Update(0.1f);
    const float movedWhileOn = entity->Transform().LocalPosition().x;
    CHECK(movedWhileOn > 0.0f);

    entity->SetActive(false);
    system.Update(0.1f);
    CHECK(entity->Transform().LocalPosition().x == doctest::Approx(movedWhileOn));

    // THE VELOCITY IS KEPT, not zeroed. Switching something off and on again
    // must not quietly stop it - and SetBodyType already destroys velocity, so
    // there is a spelling for that when it is what was wanted.
    CHECK(body->Velocity().x == doctest::Approx(100.0f));

    entity->SetActive(true);
    system.Update(0.1f);
    CHECK(entity->Transform().LocalPosition().x > movedWhileOn);
}

TEST_CASE("a switched-off entity's sprite is not drawn and its spin does not turn") {
    Fixture world("active-visual");

    const EntityId id     = world.scene.CreateEntity("Rotor");
    Entity*        entity = world.scene.Get(id);
    auto*          spin   = entity->Add<SpinComponent>();
    REQUIRE(spin != nullptr);
    spin->SetRadiansPerSecond(1.0f);

    SpinSystem system;
    system.Update(0.5f);
    const float turnedWhileOn = entity->Transform().LocalRotation();
    CHECK(turnedWhileOn > 0.0f);

    entity->SetActive(false);
    system.Update(0.5f);
    CHECK(entity->Transform().LocalRotation() == doctest::Approx(turnedWhileOn));

    // The sprite half is checked through the render system's own count rather
    // than by looking at a picture: a sprite with no texture is skipped
    // anyway, so what is asserted is that the system was asked about it and
    // the registration is unchanged - switching off must not unregister, or
    // switching back on would need more than the flag.
    REQUIRE(entity->Add<SpriteComponent>() != nullptr);
    CHECK(SpriteRenderSystem::Count() == 1);
    entity->SetActive(true);
    CHECK(SpriteRenderSystem::Count() == 1);
}

TEST_CASE("a switched-off entity's scripts neither run nor start") {
    // ======================================================================
    //  A MUTATION WROTE THIS TEST. The first version of it used an unresolved
    //  script, and breaking the active check in ScriptComponent::FixedTick
    //  broke nothing - because an unresolved script returns at
    //  `m_behaviour == nullptr` before the check is reached. The test was
    //  passing for a reason that had nothing to do with what it claimed.
    //
    //  This one runs a real script. See the note above Probe.
    // ======================================================================
    Fixture world("active-scripts");

    const EntityId id     = world.scene.CreateEntity("Brain");
    Entity*        entity = world.scene.Get(id);
    Probe*         probe  = AttachScript<Probe>(world.scene, id, "Probe");
    REQUIRE(probe != nullptr);

    ScriptSystem system;

    // It runs, and OnStart fires exactly once however many ticks follow.
    system.Update(1.0f / 60.0f);
    system.Update(1.0f / 60.0f);
    CHECK(probe->starts == 1);
    CHECK(probe->fixedTicks == 2);

    // The accessors worked from inside OnStart, which is what
    // ScriptComponent.h promises and nothing previously checked.
    CHECK(probe->nameAtStart == "Brain");
    CHECK(probe->sawOwner);
    CHECK(probe->sawScene);
    CHECK(probe->sawTransform);

    // ---- SWITCHED OFF: no more ticks -------------------------------------
    entity->SetActive(false);
    system.Update(1.0f / 60.0f);
    system.Update(1.0f / 60.0f);
    CHECK(probe->fixedTicks == 2);   // unchanged

    ScriptSystem::FrameUpdate();
    CHECK(probe->frameTicks == 0);   // the per-frame half too

    // ---- AND BACK ON: it carries on, without starting again --------------
    entity->SetActive(true);
    system.Update(1.0f / 60.0f);
    CHECK(probe->fixedTicks == 3);
    CHECK(probe->starts == 1);

    ScriptSystem::FrameUpdate();
    CHECK(probe->frameTicks == 1);

    // STILL REGISTERED THROUGHOUT. Removing it from the ticking list while
    // switched off would mean switching back on needed more than the flag,
    // and whatever that extra step was would eventually be forgotten.
    CHECK(ScriptSystem::Count() == 1);
}

TEST_CASE("a script attached to a switched-off entity does not start until it is on") {
    // ======================================================================
    //  THE ORDER INSIDE FixedTick. The active check comes BEFORE
    //  EnsureStarted, so a script on something switched off gets its OnStart
    //  when the thing is switched ON - not when it was built.
    //
    //  The other order compiles and looks fine: the script simply has its
    //  OnStart run while its entity is not part of the world, looks around,
    //  finds whatever it finds, and never gets told. A spawner would fire once
    //  from inside a disabled menu.
    // ======================================================================
    Fixture world("inactive-start");

    const EntityId id     = world.scene.CreateEntity("Sleeper");
    Entity*        entity = world.scene.Get(id);
    entity->SetActive(false);

    Probe* probe = AttachScript<Probe>(world.scene, id, "Probe");
    REQUIRE(probe != nullptr);

    ScriptSystem system;
    system.Update(1.0f / 60.0f);
    system.Update(1.0f / 60.0f);
    CHECK(probe->starts == 0);
    CHECK(probe->fixedTicks == 0);

    entity->SetActive(true);
    system.Update(1.0f / 60.0f);
    CHECK(probe->starts == 1);
    CHECK(probe->fixedTicks == 1);
}

TEST_CASE("a script spawns a prefab through its own verb") {
    // The end-to-end version of the user's request: a script, a prefab
    // reference set by the Inspector, and Instantiate with a placement.
    Fixture world("script-spawns");
    world.Write("prefabs/bullet.oprefab", TwoEntityPrefab());

    const EntityId gun = world.scene.CreateEntity("Gun");
    Spawner* spawner   = AttachScript<Spawner>(world.scene, gun, "Spawner");
    REQUIRE(spawner != nullptr);

    ScriptSystem system;
    system.Update(1.0f / 60.0f);

    REQUIRE(spawner->attempts == 1);
    REQUIRE_FALSE(spawner->spawned.IsNull());

    Entity* bullet = world.scene.Get(spawner->spawned);
    REQUIRE(bullet != nullptr);
    CHECK(ApproxEqual(bullet->Transform().WorldPosition(), Vec2{42.0f, 0.0f}));

    // The gun, plus two entities per instance of the prefab.
    CHECK(world.scene.EntityCount() == 3);

    // AND AGAIN, which is the case that would break on a duplicate name.
    system.Update(1.0f / 60.0f);
    CHECK(world.scene.EntityCount() == 5);
    CHECK(world.scene.CountWith<SpinComponent>() == 2);

    // ======================================================================
    //  AN UNSET REFERENCE SPAWNS NOTHING AND REPORTS ONCE.
    //
    //  The "once" is the part worth checking, and checking it needs the log
    //  rather than the scene - because WITHOUT the guard the spawn still
    //  fails safely (an empty path simply does not load) and the only
    //  difference is one error line per frame for as long as the reference is
    //  unset. A mutation proved that: removing the guard broke no test until
    //  this assertion existed, because every other observable was identical.
    //
    //  A Console with four thousand copies of one message in it is a Console
    //  nobody reads, which makes this a real behaviour and not a nicety.
    // ======================================================================
    spawner->what.Clear();
    LogBuffer::Clear();

    for (int i = 0; i < 5; ++i) {
        system.Update(1.0f / 60.0f);
    }
    CHECK(spawner->spawned.IsNull());
    CHECK(world.scene.EntityCount() == 5);

    std::vector<LogRecord> records;
    LogBuffer::Snapshot(records);
    int complaints = 0;
    for (const LogRecord& record : records) {
        if (record.message.find("prefab variable has not been set") !=
            std::string::npos) {
            ++complaints;
        }
    }
    CHECK(complaints == 1);
}

TEST_CASE("a script can remove itself") {
    Fixture world("script-destroys-self");

    const EntityId id = world.scene.CreateEntity("Doomed");
    REQUIRE(AttachScript<Suicide>(world.scene, id, "Suicide") != nullptr);

    ScriptSystem system;
    system.Update(1.0f / 60.0f);

    // QUEUED, NOT GONE. The script finished the function it was in and the
    // entity survives the rest of the step - rule 1 in DeferredOps.h - which
    // is what stops this from being a use-after-free on the next line of
    // whatever called it.
    CHECK(DeferredOps::IsPendingDestroy(id));
    CHECK(world.scene.IsValid(id));

    DeferredOps::Apply(world.scene);
    CHECK_FALSE(world.scene.IsValid(id));
}

TEST_CASE("a script reaches other entities through the verbs") {
    Fixture world("script-finds");

    const EntityId target = world.scene.CreateEntity("Target");
    world.scene.Get(target)->SetTag("Target");

    const EntityId seeker = world.scene.CreateEntity("Seeker");
    REQUIRE(world.scene.Get(seeker)->Add<SpinComponent>() != nullptr);

    // A child with a collider on it, for FindInChildren.
    const EntityId child = world.scene.CreateEntity("Sensor");
    world.scene.Get(child)->Transform().SetParent(
        &world.scene.Get(seeker)->Transform());
    REQUIRE(world.scene.Get(child)->Add<BoxColliderComponent>() != nullptr);

    Seeker* script = AttachScript<Seeker>(world.scene, seeker, "Seeker");
    REQUIRE(script != nullptr);

    ScriptSystem system;
    system.Update(1.0f / 60.0f);

    CHECK(script->foundByTag);
    CHECK(script->foundByName);
    CHECK(script->foundOwnSpin);
    // THE ONE THAT WAS IMPOSSIBLE before the transform reverse lookup existed.
    CHECK(script->foundInChild);
}

// ===========================================================================
//  PART 3 - THE SHARP EDGES
// ===========================================================================

TEST_CASE("a fuse does not switch collisions off while it burns") {
    // ======================================================================
    //  THE TRAP THAT MADE QueueDestroyAfter NEED ITS OWN LIST.
    //
    //  The obvious implementation marks the entity pending-destroy and checks
    //  the clock later. CollisionSystem skips anything pending - rule 1 in
    //  DeferredOps.h - so that implementation produces a bullet which flies
    //  through everything for the whole of its two-second fuse and then
    //  vanishes. It reads as a collision-layer mistake, there is no message,
    //  and the fuse is the last thing anybody would suspect.
    //
    //  This is the test that fails if somebody ever simplifies it back.
    // ======================================================================
    Fixture world("fuse-collides");

    const EntityId bullet = world.WithCollider("Bullet", Vec2{0.0f, 0.0f});
    const EntityId wall   = world.WithCollider("Wall", Vec2{1.0f, 0.0f});

    DeferredOps::QueueDestroyAfter(bullet, 2.0f);

    CHECK_FALSE(DeferredOps::IsPendingDestroy(bullet));
    CHECK(DeferredOps::SecondsUntilDestroy(bullet) == doctest::Approx(2.0f));
    CHECK(DeferredOps::PendingFuseCount() == 1);

    CollisionSystem system;
    system.Update(1.0f / 60.0f);
    CHECK(CollisionSystem::ActivePairCount() > 0);

    // It is also still visible to a query, for the same reason.
    CHECK(Physics2D::OverlapPoint(Vec2{0.0f, 0.0f}) != nullptr);

    CHECK(wall != EntityId{});
}

TEST_CASE("a fuse runs out, and can be reset or called off") {
    Fixture world("fuse-timing");

    const EntityId id = world.scene.CreateEntity("Grenade");
    DeferredOps::QueueDestroyAfter(id, 1.0f);

    // Not yet.
    DeferredOps::TickFuses(0.5f);
    DeferredOps::Apply(world.scene);
    CHECK(world.scene.IsValid(id));

    // ASKING AGAIN REPLACES THE FUSE rather than queueing a second one, which
    // is what makes "reset the timer" just asking again.
    DeferredOps::QueueDestroyAfter(id, 1.0f);
    CHECK(DeferredOps::PendingFuseCount() == 1);
    CHECK(DeferredOps::SecondsUntilDestroy(id) == doctest::Approx(1.0f));

    DeferredOps::TickFuses(0.75f);
    DeferredOps::Apply(world.scene);
    CHECK(world.scene.IsValid(id));

    DeferredOps::TickFuses(0.75f);
    // It is in the destroy queue now, not gone - the queue is drained at one
    // point, which is what Apply is.
    CHECK(DeferredOps::IsPendingDestroy(id));
    DeferredOps::Apply(world.scene);
    CHECK_FALSE(world.scene.IsValid(id));

    // ---- calling one off --------------------------------------------------
    const EntityId saved = world.scene.CreateEntity("Shielded");
    DeferredOps::QueueDestroyAfter(saved, 1.0f);
    CHECK(DeferredOps::CancelQueuedDestroy(saved));
    CHECK(DeferredOps::PendingFuseCount() == 0);
    DeferredOps::TickFuses(10.0f);
    DeferredOps::Apply(world.scene);
    CHECK(world.scene.IsValid(saved));

    // And an already-queued destroy, which is the shield-absorbs-the-killing-
    // blow case the header says this exists for.
    DeferredOps::QueueDestroy(saved);
    CHECK(DeferredOps::IsPendingDestroy(saved));
    CHECK(DeferredOps::CancelQueuedDestroy(saved));
    CHECK_FALSE(DeferredOps::IsPendingDestroy(saved));
    DeferredOps::Apply(world.scene);
    CHECK(world.scene.IsValid(saved));

    // Nothing to call off answers false rather than pretending.
    CHECK_FALSE(DeferredOps::CancelQueuedDestroy(saved));

    // A delay of zero or less IS QueueDestroy, so there is no edge case.
    DeferredOps::QueueDestroyAfter(saved, 0.0f);
    CHECK(DeferredOps::IsPendingDestroy(saved));
    DeferredOps::Apply(world.scene);
    CHECK_FALSE(world.scene.IsValid(saved));
}

TEST_CASE("a fuse does not survive into the next scene") {
    // ======================================================================
    //  A fresh scene hands out slot indices from zero at generation 1, so a
    //  leftover fuse stands a real chance of matching a brand new entity - and
    //  would destroy something nobody asked to destroy, seconds into the next
    //  level, with no connection to its cause. The same reasoning
    //  Engine::ApplyPendingSceneRequest already applies to the destroy queue.
    // ======================================================================
    Fixture world("fuse-scene-change");

    const EntityId doomed = world.scene.CreateEntity("Doomed");
    DeferredOps::QueueDestroyAfter(doomed, 1.0f);
    CHECK(DeferredOps::PendingFuseCount() == 1);

    DeferredOps::Clear();
    CHECK(DeferredOps::PendingFuseCount() == 0);

    DeferredOps::TickFuses(10.0f);
    DeferredOps::Apply(world.scene);
    CHECK(world.scene.IsValid(doomed));
}

TEST_CASE("a raycast finds the nearest collider, not the nearest of the first few") {
    // ======================================================================
    //  A REAL BUG THIS PINS. Raycast used to call RaycastAll into a 64-entry
    //  buffer. RaycastAll fills in collider-registration order and sorts only
    //  what fitted, so past the buffer's end the hits were dropped BEFORE the
    //  sort - and the nearest could be among them. The result was a raycast
    //  that occasionally reported something further away than what it actually
    //  hit, in big scenes only, silently.
    //
    //  Seventy colliders, with the NEAREST ONE REGISTERED LAST so it would be
    //  the one dropped. The old implementation fails this; the running-minimum
    //  one has no buffer and no limit.
    // ======================================================================
    Fixture world("raycast-nearest");

    for (int i = 0; i < 70; ++i) {
        const float x = 500.0f + static_cast<float>(i) * 20.0f;
        world.WithCollider(("Far" + std::to_string(i)).c_str(), Vec2{x, 0.0f});
    }
    const EntityId nearest = world.WithCollider("Near", Vec2{20.0f, 0.0f});

    const RaycastHit2D hit =
        Physics2D::Raycast(Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 5000.0f);
    REQUIRE(static_cast<bool>(hit));
    CHECK(hit.entity == nearest);
    CHECK(hit.distance < 30.0f);
}

TEST_CASE("a camera on a switched-off entity cannot be the active camera") {
    // ======================================================================
    //  Written because a mutation showed this gate untested. Without it,
    //  switching off a whole menu or cut-scene subtree leaves its camera still
    //  framing the shot - the scene goes away and the view does not follow,
    //  which looks like a camera bug rather than an active-flag one.
    //
    //  CameraComponent CAN be attached in a test, which the prefab tests' note
    //  says it cannot: OnAttach reaches Engine::Get().GetCamera(), and that is
    //  a plain member of a static instance rather than anything Init creates.
    //  GetScene() is the one that would need an engine, and this does not
    //  touch it.
    // ======================================================================
    Fixture world("active-camera");

    const EntityId id     = world.scene.CreateEntity("MainCamera");
    Entity*        entity = world.scene.Get(id);

    // CONSTRUCTED DIRECTLY RATHER THAN THROUGH THE FACTORY. Every other
    // built-in type has a RegisterComponentTypes() that a test can call;
    // CameraComponent is registered inline in Engine.cpp instead, so
    // Add<CameraComponent>() comes back null here. Going round the factory is
    // honest for this test - what is being checked is the active gate, not
    // whether the name is registered.
    auto* camera = static_cast<CameraComponent*>(
        entity->AddComponent(std::make_unique<CameraComponent>()));
    REQUIRE(camera != nullptr);

    CHECK(CameraSystem::Active() == camera);

    entity->SetActive(false);
    CHECK(CameraSystem::Active() == nullptr);

    entity->SetActive(true);
    CHECK(CameraSystem::Active() == camera);

    // And through a parent.
    const EntityId rig = world.scene.CreateEntity("CameraRig");
    entity->Transform().SetParent(&world.scene.Get(rig)->Transform());
    world.scene.Get(rig)->SetActive(false);
    CHECK(CameraSystem::Active() == nullptr);
}

TEST_CASE("a switched-off script is not told about a contact") {
    // ======================================================================
    //  THE GAP BETWEEN STAGE 400 AND STAGE 500, which is where this one
    //  lives. A switched-off entity's collider does not collide, so normally
    //  no contact is ever generated for it - but a message is QUEUED at
    //  collision time and DELIVERED later, so something switched off in
    //  between would otherwise be told about a contact it is no longer part
    //  of. Found untested by a mutation.
    // ======================================================================
    Fixture world("active-contact");

    const EntityId id     = world.scene.CreateEntity("Sensor");
    Entity*        entity = world.scene.Get(id);
    Touchy*        probe  = AttachScript<Touchy>(world.scene, id, "Touchy");
    REQUIRE(probe != nullptr);

    const EntityId other = world.scene.CreateEntity("Partner");

    // It has to have STARTED before a contact reaches it - see Touchy.
    ScriptSystem system;
    system.Update(1.0f / 60.0f);

    auto* script = static_cast<ScriptComponent*>(
        entity->FindComponent(ScriptComponent::kTypeName));
    REQUIRE(script != nullptr);

    script->DispatchCollision(MessageTypes::kTriggerEnter, other);
    CHECK(probe->enters == 1);

    entity->SetActive(false);
    script->DispatchCollision(MessageTypes::kTriggerEnter, other);
    script->DispatchCollision(MessageTypes::kTriggerStay, other);
    CHECK(probe->enters == 1);   // unchanged
    CHECK(probe->stays == 0);

    entity->SetActive(true);
    script->DispatchCollision(MessageTypes::kTriggerStay, other);
    CHECK(probe->stays == 1);
}

TEST_CASE("a switched-off entity's interface is neither drawn nor clickable") {
    // ======================================================================
    //  THE NINTH SYSTEM, and the one a mutation caught as untested. It is the
    //  half most likely to be forgotten, because a UI rect already has its own
    //  `visible` flag - so "the panel disappeared" looks handled, and the
    //  invisible panel goes on SWALLOWING CLICKS meant for whatever is behind
    //  it. One gate in CollectSorted covers both Update and Render, which is
    //  what makes that impossible.
    // ======================================================================
    Fixture world("active-ui");

    const EntityId id     = world.scene.CreateEntity("Button");
    Entity*        entity = world.scene.Get(id);

    auto* rect = entity->Add<UIRectComponent>();
    REQUIRE(rect != nullptr);
    rect->space  = UISpace::Screen;
    rect->anchor = UIAnchor::TopLeft;
    rect->offset = Vec2{0.0f, 0.0f};
    rect->size   = Vec2{100.0f, 50.0f};
    rect->pivot  = Vec2{0.0f, 0.0f};

    auto* button = entity->Add<UIButtonComponent>();
    REQUIRE(button != nullptr);

    Camera     camera;
    const Vec2 viewport{640.0f, 480.0f};
    const Vec2 inside{50.0f, 25.0f};

    // A click is an EDGE, so it takes two steps: down, then up.
    const auto clickOnce = [&]() {
        UISystem::SetPointer(inside, viewport, /*down=*/true, /*active=*/true);
        UISystem::Update(world.scene, camera);
        UISystem::SetPointer(inside, viewport, /*down=*/false, /*active=*/true);
        UISystem::Update(world.scene, camera);
    };

    clickOnce();
    CHECK(button->WasClicked());

    // ---- switched off: the click does not land ---------------------------
    entity->SetActive(false);
    clickOnce();
    CHECK_FALSE(button->WasClicked());

    entity->SetActive(true);
    clickOnce();
    CHECK(button->WasClicked());

    // Through a parent, which is how a whole menu gets switched off.
    const EntityId panel = world.scene.CreateEntity("Panel");
    entity->Transform().SetParent(&world.scene.Get(panel)->Transform());
    world.scene.Get(panel)->SetActive(false);
    clickOnce();
    CHECK_FALSE(button->WasClicked());

    // AND `visible` IS A DIFFERENT THING, which is worth pinning so nobody
    // merges the two: it hides one rect without switching the entity off, and
    // a hidden rect is also not clickable.
    world.scene.Get(panel)->SetActive(true);
    rect->visible = false;
    clickOnce();
    CHECK_FALSE(button->WasClicked());
    CHECK(entity->IsActiveInHierarchy());   // still switched ON
}

TEST_CASE("the convenience destroy overloads reach the same queue") {
    Fixture world("destroy-overloads");

    const EntityId byId     = world.scene.CreateEntity("ById");
    const EntityId byEntity = world.scene.CreateEntity("ByEntity");

    DeferredOps::QueueDestroy(byId);
    DeferredOps::QueueDestroy(*world.scene.Get(byEntity));

    CHECK(DeferredOps::IsPendingDestroy(byId));
    CHECK(DeferredOps::IsPendingDestroy(byEntity));

    // RULE 3: twice is harmless and silent. Game code does this constantly.
    DeferredOps::QueueDestroy(byId);
    CHECK(DeferredOps::PendingDestroyCount() == 2);

    DeferredOps::Apply(world.scene);
    CHECK_FALSE(world.scene.IsValid(byId));
    CHECK_FALSE(world.scene.IsValid(byEntity));
}

TEST_CASE("spawning from inside a scene walk is safe") {
    // ======================================================================
    //  DeferredOps.h is explicit that creating is the SAFE half - it only ever
    //  appends - and ScriptBehaviour::Instantiate's documentation leans on
    //  that to say spawning mid-update needs no queue. Both claims rest on
    //  Scene::ForEach being written by index with the size re-read, and on an
    //  Entity never moving because each slot holds a unique_ptr.
    //
    //  Worth a test rather than a comment: this is the sort of thing that
    //  works until somebody rewrites a loop as a range-for, and then fails
    //  intermittently in a way nobody traces back to the loop.
    // ======================================================================
    Fixture world("spawn-during-walk");
    world.Write("prefabs/bullet.oprefab", TwoEntityPrefab());

    for (int i = 0; i < 5; ++i) {
        world.scene.CreateEntity("Gun" + std::to_string(i));
    }

    int         spawns = 0;
    std::string error;
    world.scene.ForEach([&](Entity& entity) {
        // Only the original five spawn, or this never finishes - which is
        // itself the thing rule 4 exists for.
        if (entity.Name().rfind("Gun", 0) != 0) {
            return;
        }
        if (!world.scene.InstantiatePrefab("prefabs/bullet.oprefab", error).IsNull()) {
            ++spawns;
        }
        // The reference handed to this callback must still be good after a
        // spawn moved the slot vector. It is: the Entity is held by a
        // unique_ptr, so the slot moving does not move the entity.
        CHECK(entity.IsAlive());
    });

    CHECK(spawns == 5);
    // Five guns plus five instances of a two-entity prefab.
    CHECK(world.scene.EntityCount() == 15);
}

TEST_CASE("a prefab member's tag comes from the asset") {
    Fixture world("prefab-tags");

    Json asset = TwoEntityPrefab();
    asset["entities"][0]["tag"] = "Bullet";
    world.Write("prefabs/bullet.oprefab", asset);

    std::string    error;
    const EntityId spawned =
        world.scene.InstantiatePrefab("prefabs/bullet.oprefab", error);
    REQUIRE_MESSAGE(!spawned.IsNull(), error);

    CHECK(world.scene.Get(spawned)->HasTag("Bullet"));
    CHECK(world.scene.CountWithTag("Bullet") == 1);

    // AND IT IS NOT AN OVERRIDE. It came from the asset, so the instance has
    // nothing to say about it - otherwise every spawned bullet would record a
    // difference from the thing it is a copy of. See Prefab.h.
    const PrefabInstance* record = world.scene.PrefabInstanceOf(spawned);
    REQUIRE(record != nullptr);
    CHECK(record->OverrideCount() == 0);
}

TEST_CASE("a script can be found anywhere in the scene by name") {
    // ======================================================================
    //  The lookup that had no spelling at all. Scene::FindFirstWith<T> cannot
    //  answer it - every script's component reports its type name as
    //  "ScriptComponent", so asking for a Score finds nothing and asking for a
    //  ScriptComponent finds an arbitrary one. Without this, the third-most-
    //  ordinary task in any game - "tell the scorekeeper" - has no spelling.
    // ======================================================================
    Fixture world("find-script-in");

    world.scene.CreateEntity("Decoy");

    const EntityId rules  = world.scene.CreateEntity("GameRules");
    Entity*        entity = world.scene.Get(rules);

    Json node      = Json::object();
    node["script"] = "ScoreKeeper";
    auto component = ComponentFactory::Create(ScriptComponent::kTypeName);
    REQUIRE(component != nullptr);
    std::string error;
    REQUIRE(component->Deserialize(node, error));
    entity->AddComponent(std::move(component));

    ScriptComponent* found = FindScriptNamedIn(world.scene, "ScoreKeeper");
    REQUIRE(found != nullptr);
    CHECK(found->ScriptName() == "ScoreKeeper");
    CHECK(found->Owner() == entity);

    CHECK(FindScriptNamedIn(world.scene, "NotThere") == nullptr);
    CHECK(FindScriptNamedIn(world.scene, "") == nullptr);

    // SOMETHING DYING THIS TICK IS NOT THE SCOREKEEPER.
    DeferredOps::QueueDestroy(rules);
    CHECK(FindScriptNamedIn(world.scene, "ScoreKeeper") == nullptr);
}

TEST_CASE("the entity-by-name lookup and the const accessors answer together") {
    Fixture world("find-entity");

    const EntityId id = world.scene.CreateEntity("Player");

    CHECK(world.scene.FindEntity("Player") == world.scene.Get(id));
    CHECK(world.scene.FindEntity("Nobody") == nullptr);

    const Scene& readOnly = world.scene;
    CHECK(readOnly.FindEntity("Player") != nullptr);
    CHECK(readOnly.Get(id) != nullptr);
    CHECK(readOnly.Get(EntityId{}) == nullptr);

    // A STALE ID ANSWERS NULL rather than the slot's next occupant. The
    // generation is what makes that possible, and m_freeIndices is LIFO - so
    // the slot freed here is the very next one handed out, which is the case
    // worth checking.
    world.scene.DestroyEntityImmediate(id);
    const EntityId reused = world.scene.CreateEntity("Someone Else");
    CHECK(reused.index == id.index);
    CHECK(world.scene.Get(id) == nullptr);
    CHECK(readOnly.Get(id) == nullptr);
}

TEST_CASE("the pointer reads back what the view gave it") {
    // ======================================================================
    //  SETS ITS OWN STATE RATHER THAN READING WHATEVER WAS LEFT.
    //
    //  The UI pointer is one file-static set, shared by the whole process, and
    //  the first version of this case asserted that nothing was over the view
    //  - which passed until the UI test above ran first and left a pointer
    //  there. A test whose result depends on which other tests ran is worse
    //  than no test, so this one states the state it wants.
    // ======================================================================
    const Vec2 viewport{640.0f, 480.0f};

    UISystem::SetPointer(Vec2{100.0f, 70.0f}, viewport, /*down=*/true, /*active=*/true);
    CHECK(Pointer::IsOverView());
    CHECK(ApproxEqual(Pointer::InView(), Vec2{100.0f, 70.0f}));
    CHECK(ApproxEqual(Pointer::ViewSize(), viewport));
    CHECK(Pointer::IsDown());

    // Pointer::InWorld goes through the live camera, and in a test there is no
    // engine to have one. The honest fallback is the view position unchanged,
    // rather than a plausible-looking wrong world position - see Pointer.cpp.
    CHECK(ApproxEqual(Pointer::InWorld(), Pointer::InView()));

    // NOT OVER THE VIEW AT ALL is a real answer and the one anything following
    // the cursor has to check, or it snaps to the last place the mouse was
    // seen and sits there.
    UISystem::SetPointer(Vec2{100.0f, 70.0f}, viewport, /*down=*/false, /*active=*/false);
    CHECK_FALSE(Pointer::IsOverView());
    CHECK_FALSE(Pointer::IsDown());
}
