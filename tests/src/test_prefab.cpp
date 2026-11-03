// ============================================================================
//  test_prefab.cpp - prefabs: the link between an asset and its instances.
//
//  WHAT MAKES THIS WORTH TESTING RATHER THAN CLICKING
//
//  Every way a prefab can go wrong is silent. An instance that quietly stops
//  following its asset looks exactly like one that is following it; an
//  override that is quietly lost looks exactly like one nobody made; a
//  member written to the scene file twice loads as two entities on top of
//  each other and the second one is the one you see. None of those produce a
//  message, and all of them are discovered much later, in a scene somebody
//  has since done a day's work in.
//
//  So the properties the feature rests on are pinned here:
//
//    a fresh instance claims NO overrides      - the cornerstone. If diffing
//                                                an untouched instance finds
//                                                differences, every scene
//                                                fills up with fake ones.
//    members are written ONCE, as a record     - not as entities as well
//    an override survives save and load
//    editing the ASSET reaches every instance  - and leaves overrides alone
//    applying pushes to the asset              - and the others follow
//    placement is not applied or reverted      - or every enemy in the
//                                                project moves to this one
//    one nested variable overrides alone       - not the whole set
//    a missing prefab does not lose overrides
//
//  THERE IS A FILE SYSTEM UNDER THESE, unlike test_snapshot.cpp, because a
//  prefab IS a file: PrefabLibrary reads it through FileSystem, so there is no
//  version of this that runs without one.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/math/Transform2D.h>
#include <engine/physics/Collider.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Prefab.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/SpinComponent.h>

#include <cstdlib>
#include <filesystem>
#include <string>

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

// A temporary project with a real assets/ folder, a live FileSystem, an active
// scene, and an EMPTY prefab cache.
//
// THE CACHE MATTERS MORE THAN IT LOOKS. PrefabLibrary is keyed on a VIRTUAL
// path - "prefabs/enemy.oprefab" - so two tests using that name are using the
// same key for two different files in two different temporary folders. Without
// the Clear() at both ends, the second test would quietly be handed the first
// one's asset and pass or fail for reasons nothing on screen could explain.
struct Fixture {
    fs::path   root;
    FileSystem fileSystem;
    Scene      scene;
    Scene*     previous = nullptr;

    explicit Fixture(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-prefab-") + name);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "prefabs", ec);

        SetAssetRoot(root.string().c_str());
        REQUIRE(fileSystem.Init(BootConfig{}));

        // ==================================================================
        //  WITHOUT THESE, EVERY COMPONENT IN EVERY PREFAB IS SKIPPED, because
        //  a prefab is built by name through the factory exactly as a scene is
        //  - and until something has told the factory what "SpinComponent"
        //  means, that name means nothing.
        //
        //  Normally Engine::SceneSubsystem::Init does it. These tests have no
        //  Engine, so they do it themselves, for the types they use. A camera
        //  is deliberately not among them: CameraComponent::OnAttach reaches
        //  the engine's own camera through CameraSystem::Apply, which is not
        //  there to be reached.
        // ==================================================================
        ComponentFactory::RegisterBuiltins();
        CollisionSystem::RegisterComponentTypes();
        SpinSystem::RegisterComponentTypes();
        ScriptSystem::RegisterComponentTypes();

        PrefabLibrary::Clear();

        previous = Scene::Active();
        Scene::SetActive(&scene);
    }

    ~Fixture() {
        Scene::SetActive(previous);
        scene.Unload();
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

    Json Read(const std::string& virtualPath) const {
        std::string text;
        std::string error;
        REQUIRE(FileSystem::ReadTextFile(virtualPath, text, error));
        const Json document = ParseJson(text, error);
        REQUIRE(error.empty());
        return document;
    }
};

// A prefab with a root and one child, using only components that need no
// images and no compiled scripts: a transform each, and a spin on the child.
//
//   1  "Enemy"   TransformComponent
//   2  "Muzzle"  TransformComponent, SpinComponent        (child of 1)
Json TwoEntityPrefab() {
    Json transform      = Json::object();
    transform["type"]   = "TransformComponent";
    transform["position"] = Json::array({0.0, 0.0});
    transform["rotation"] = 0.0;
    transform["scale"]    = Json::array({1.0, 1.0});

    Json childTransform        = transform;
    childTransform["position"] = Json::array({4.0, 0.0});

    Json spin                  = Json::object();
    spin["type"]               = "SpinComponent";
    spin["radiansPerSecond"]   = 1.5;

    Json rootNode            = Json::object();
    rootNode["id"]           = 1;
    rootNode["name"]         = "Enemy";
    rootNode["components"]   = Json::array({transform});

    Json childNode          = Json::object();
    childNode["id"]         = 2;
    childNode["parent"]     = 1;
    childNode["name"]       = "Muzzle";
    childNode["components"] = Json::array({childTransform, spin});

    Json document         = Json::object();
    document["prefab"]    = 1;
    document["name"]      = "Enemy";
    document["root"]      = 1;
    document["entities"]  = Json::array({rootNode, childNode});
    return document;
}

// The scene as a document, which is how every assertion about what was saved
// is made: Scene::SaveToString runs the whole diff on the way out, so looking
// at its output is looking at the real path rather than at a test-only one.
Json SavedDocument(Scene& scene) {
    std::string text;
    std::string error;
    REQUIRE(scene.SaveToString(text, error));
    REQUIRE(error.empty());
    const Json document = ParseJson(text, error);
    REQUIRE(error.empty());
    return document;
}

// BY VALUE, DELIBERATELY, and it was a reference first.
//
// Every call here reads `InstanceRecord(SavedDocument(scene))`, and
// SavedDocument returns a document by value. A function returning a reference
// INTO that temporary hands back a dangling one the moment the full expression
// ends - which showed up as an override count of 0 in one test and a segfault
// in another, both of them reading freed memory rather than failing.
//
// Returning by value is what makes the obvious spelling correct: a temporary
// bound straight to a const reference has its lifetime extended to match it.
// The copy is a handful of fields in a test.
Json InstanceRecord(const Json& document, std::size_t index = 0) {
    const auto it = document.find("prefabInstances");
    if (it == document.end() || !it->is_array() || it->size() <= index) {
        return Json::object();
    }
    return (*it)[index];
}

std::size_t OverrideCount(const Json& record) {
    const auto it = record.find("overrides");
    return (it == record.end() || !it->is_array()) ? 0 : it->size();
}

// Reaching a named entity, and a component on it, with a FAILURE rather than a
// crash when it is not there.
//
// Writing scene.Get(scene.Find("Muzzle"))->Find<SpinComponent>()->Set... reads
// well and segfaults with no message the moment a prefab fails to build - and
// a test that crashes tells you only which line, not which of the three
// lookups came back empty. Asking here means the report names the entity.
Entity& EntityNamed(Scene& scene, const char* name) {
    Entity* entity = scene.Get(scene.Find(name));
    REQUIRE_MESSAGE(entity != nullptr, "no entity called ", name);
    return *entity;
}

template <typename T>
T& ComponentOn(Scene& scene, const char* name) {
    T* component = EntityNamed(scene, name).Find<T>();
    REQUIRE_MESSAGE(component != nullptr, name, " has no ", T::kTypeName);
    return *component;
}

} // namespace

// ===========================================================================
//  THE CORNERSTONE
// ===========================================================================

TEST_CASE("a freshly placed instance claims no overrides at all") {
    // ======================================================================
    //  IF THIS FAILS NOTHING ELSE MATTERS. Overrides are worked out by
    //  COMPARING a live instance against its asset - see Prefab.h - so an
    //  untouched instance that compares as different would put a fake
    //  override in the scene file for every field of every component. The
    //  instance would then be pinned to those values and would stop following
    //  the asset entirely, which is the one thing a prefab is for.
    //
    //  It is also the test that catches a component whose Serialize and
    //  Deserialize disagree about a key, because that shows up here as a
    //  difference that nobody made.
    // ======================================================================
    Fixture world("fresh");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE(error.empty());
    REQUIRE_FALSE(root.IsNull());
    CHECK(world.scene.EntityCount() == 2);

    const Json  document = SavedDocument(world.scene);
    const Json& record   = InstanceRecord(document);

    CHECK(record.value("prefab", std::string{}) == "prefabs/enemy.oprefab");
    CHECK(OverrideCount(record) == 0);
    CHECK_FALSE(record.contains("addedComponents"));
    CHECK_FALSE(record.contains("removedComponents"));

    // And no names either: both members got exactly what the asset calls
    // them, because nothing else in the scene had taken those names.
    CHECK_FALSE(record.contains("names"));
}

TEST_CASE("an instance's members are written once, as a record, not as entities") {
    // Writing them as entities as well would not merely duplicate them. It
    // would BREAK THE LINK: the scene would hold a frozen copy, editing the
    // asset would reach nothing, and loading would build every member twice.
    Fixture world("record");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    world.scene.CreateEntity("Ground");
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());
    REQUIRE(world.scene.EntityCount() == 3);

    const Json document = SavedDocument(world.scene);

    // The plain entity, and only the plain entity.
    REQUIRE(document.at("entities").is_array());
    REQUIRE(document.at("entities").size() == 1);
    CHECK(document.at("entities")[0].at("name") == "Ground");

    REQUIRE(document.at("prefabInstances").is_array());
    CHECK(document.at("prefabInstances").size() == 1);

    // And reading it back gives three entities, not five.
    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));
    CHECK(world.scene.EntityCount() == 3);
    CHECK(world.scene.PrefabInstanceCount() == 1);
}

// ===========================================================================
//  OVERRIDES
// ===========================================================================

TEST_CASE("changing one field of one member records exactly that") {
    Fixture world("one-field");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);

    const Json& record = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(OverrideCount(record) == 1);

    const Json& item = record.at("overrides")[0];
    CHECK(item.at("entity") == 2);
    CHECK(item.at("component") == "SpinComponent");
    CHECK(item.at("field") == "radiansPerSecond");
    CHECK(item.at("value").get<double>() == doctest::Approx(9.0));

    // The transform is untouched, so nothing about it is recorded - which is
    // what makes the record short and what makes the asset reach it later.
    CHECK_FALSE(item.contains("slot"));
}

TEST_CASE("an override survives being saved and loaded") {
    Fixture world("round-trip");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);
    EntityNamed(world.scene, "Muzzle").Transform().SetLocalPosition(Vec2{-7.0f, 2.0f});

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    Entity& restored = EntityNamed(world.scene, "Muzzle");
    CHECK(ComponentOn<SpinComponent>(world.scene, "Muzzle").RadiansPerSecond() ==
          doctest::Approx(9.0f));
    CHECK(restored.Transform().LocalPosition().x == doctest::Approx(-7.0f));
    CHECK(restored.Transform().LocalPosition().y == doctest::Approx(2.0f));

    // Still one instance, still two entities, still linked.
    CHECK(world.scene.PrefabInstanceCount() == 1);
    CHECK(world.scene.EntityCount() == 2);
    CHECK(world.scene.PrefabInstanceOf(restored.Id()) != nullptr);
    CHECK(world.scene.PrefabLocalIdOf(restored.Id()) == 2);
}

TEST_CASE("reverting an instance puts it back without moving it") {
    Fixture world("revert");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(root.IsNull());

    // Something to revert, and something that must NOT be reverted.
    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);

    world.scene.Get(root)->Transform().SetLocalPosition(Vec2{120.0f, 40.0f});

    REQUIRE(RevertPrefabInstance(world.scene, root, error));
    REQUIRE(error.empty());

    CHECK(ComponentOn<SpinComponent>(world.scene, "Muzzle").RadiansPerSecond() ==
          doctest::Approx(1.5f));

    // ======================================================================
    //  AND IT IS STILL WHERE IT WAS PUT. Reverting an instance to its asset
    //  must not teleport it to the asset's origin - that is where the asset
    //  sits, not where this copy of it belongs. See IsRootPlacement.
    // ======================================================================
    Entity& rebuiltRoot = EntityNamed(world.scene, "Enemy");
    CHECK(rebuiltRoot.Transform().LocalPosition().x == doctest::Approx(120.0f));
    CHECK(rebuiltRoot.Transform().LocalPosition().y == doctest::Approx(40.0f));
}

// ===========================================================================
//  THE LINK ITSELF - editing the asset
// ===========================================================================

TEST_CASE("editing the asset reaches every instance and leaves overrides alone") {
    // ======================================================================
    //  THIS IS THE FEATURE. Twenty copies of an enemy are not twenty enemies;
    //  they are twenty instances of one enemy, and fixing the enemy fixes all
    //  twenty - except where somebody has deliberately said otherwise about
    //  one of them.
    // ======================================================================
    Fixture world("asset-edit");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    const EntityId first  = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    const EntityId second = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(first.IsNull());
    REQUIRE_FALSE(second.IsNull());

    // One deliberate difference on the FIRST instance only.
    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);

    // Now the asset changes: the child moves and spins differently.
    Json edited = TwoEntityPrefab();
    edited.at("entities")[1].at("components")[0]["position"] = Json::array({40.0, 11.0});
    edited.at("entities")[1].at("components")[1]["radiansPerSecond"] = 3.0;

    // The two steps that have to happen in this order, and the reason
    // RebuildPrefabInstances does not do the first one itself: the deltas have
    // to be measured against the asset as it WAS.
    CHECK(CapturePrefabOverridesForAsset(world.scene, "prefabs/enemy.oprefab") == 2);
    world.Write("prefabs/enemy.oprefab", edited);
    CHECK(world.scene.RebuildPrefabInstances("prefabs/enemy.oprefab", error) == 2);

    // The instance that said nothing about the spin now has the asset's new
    // one, and has moved with it.
    CHECK(ComponentOn<SpinComponent>(world.scene, "Muzzle_1").RadiansPerSecond() ==
          doctest::Approx(3.0f));
    CHECK(EntityNamed(world.scene, "Muzzle_1").Transform().LocalPosition().x ==
          doctest::Approx(40.0f));

    // The instance that DID say something about the spin kept what it said -
    // and still picked up the move, because it never said anything about that.
    CHECK(ComponentOn<SpinComponent>(world.scene, "Muzzle").RadiansPerSecond() ==
          doctest::Approx(9.0f));
    CHECK(EntityNamed(world.scene, "Muzzle").Transform().LocalPosition().x ==
          doctest::Approx(40.0f));
}

TEST_CASE("the children of two instances get names that come back on reload") {
    // Names are unique in a scene and are what parents are recorded by - see
    // Prefab.h on local ids - so the second instance's child cannot also be
    // called "Muzzle". The name it ends up with has to be remembered, or a
    // plain entity parented under it loses its parent on the next load.
    Fixture world("names");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    CHECK_FALSE(world.scene.Find("Enemy").IsNull());
    CHECK_FALSE(world.scene.Find("Enemy_1").IsNull());
    CHECK_FALSE(world.scene.Find("Muzzle").IsNull());
    CHECK_FALSE(world.scene.Find("Muzzle_1").IsNull());

    // The second instance records the name it had to settle for; the first
    // one got what the asset said and records nothing.
    const Json document = SavedDocument(world.scene);
    CHECK_FALSE(InstanceRecord(document, 0).contains("names"));
    REQUIRE(InstanceRecord(document, 1).contains("names"));
    CHECK(InstanceRecord(document, 1).at("names").at("2") == "Muzzle_1");

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    CHECK_FALSE(world.scene.Find("Muzzle").IsNull());
    CHECK_FALSE(world.scene.Find("Muzzle_1").IsNull());
    CHECK(world.scene.EntityCount() == 4);
}

TEST_CASE("renaming and reparenting an instance survives a save, and is not an override") {
    // ======================================================================
    //  WHERE AN INSTANCE SITS AND WHAT IT IS CALLED BELONG TO THE SCENE, not
    //  to the prefab - so the record FOLLOWS them rather than recording a
    //  difference. RefreshPrefabPlacement is what does that, and it is called
    //  by every capture.
    //
    //  WITHOUT IT BOTH ARE SILENTLY LOST. The record still holds the name and
    //  parent the instance was built with, and the record is what gets
    //  written: drag a prefab instance onto a new parent, save, reload, and it
    //  is back at the top level under its old name. Nothing would say so,
    //  which is why this is a test rather than a comment.
    // ======================================================================
    Fixture world("placement-record");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    const EntityId holder = world.scene.CreateEntity("Spawners");

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(root.IsNull());

    // Both of the things the scene owns about an instance.
    REQUIRE(world.scene.RenameEntity(root, "Boss"));
    world.scene.Get(root)->Transform().SetParent(&world.scene.Get(holder)->Transform());

    // NEITHER IS AN OVERRIDE. They say nothing about the prefab, so pushing
    // them into it - or reverting them - would both be wrong.
    const Json record = InstanceRecord(SavedDocument(world.scene));
    CHECK(record.at("name") == "Boss");
    CHECK(record.at("parent") == "Spawners");
    CHECK(OverrideCount(record) == 0);

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    Entity& restored = EntityNamed(world.scene, "Boss");
    REQUIRE(restored.Transform().Parent() != nullptr);
    CHECK(restored.Transform().Parent() ==
          &EntityNamed(world.scene, "Spawners").Transform());
    CHECK(world.scene.IsPrefabRoot(restored.Id()));

    // The child came back too, under the renamed root.
    Entity& muzzle = EntityNamed(world.scene, "Muzzle");
    CHECK(muzzle.Transform().Parent() == &restored.Transform());
    CHECK(world.scene.EntityCount() == 3);
}

// ===========================================================================
//  APPLYING
// ===========================================================================

TEST_CASE("applying writes the change into the asset and the others follow") {
    Fixture world("apply");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    const EntityId first = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(first.IsNull());
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);

    REQUIRE(ApplyPrefabInstance(world.scene, first, error));
    REQUIRE(error.empty());

    // The file itself says so now.
    const Json saved = world.Read("prefabs/enemy.oprefab");
    CHECK(saved.at("entities")[1].at("components")[1].at("radiansPerSecond").get<double>() ==
          doctest::Approx(9.0));

    // And the instance that was never touched has it too.
    CHECK(ComponentOn<SpinComponent>(world.scene, "Muzzle_1").RadiansPerSecond() ==
          doctest::Approx(9.0f));

    // The instance that was applied no longer claims an override, because
    // there is no longer any difference to claim.
    CHECK(OverrideCount(InstanceRecord(SavedDocument(world.scene))) == 0);
}

TEST_CASE("applying does not push where this instance happens to stand") {
    // ======================================================================
    //  Without this, pressing Apply after nudging an enemy into place would
    //  move every other enemy in every other scene in the project to the same
    //  spot. The root's position and rotation are WHERE THIS COPY IS, not
    //  something about the enemy. See IsRootPlacement.
    // ======================================================================
    Fixture world("placement");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    const EntityId first = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(first.IsNull());
    const EntityId second = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(second.IsNull());

    world.scene.Get(second)->Transform().SetLocalPosition(Vec2{900.0f, 900.0f});

    world.scene.Get(first)->Transform().SetLocalPosition(Vec2{120.0f, 40.0f});
    world.scene.Get(first)->Transform().SetLocalRotation(2.0f);

    // Something real to apply, so the apply is not a no-op.
    ComponentOn<SpinComponent>(world.scene, "Muzzle").SetRadiansPerSecond(9.0f);

    REQUIRE(ApplyPrefabInstance(world.scene, first, error));

    const Json saved = world.Read("prefabs/enemy.oprefab");
    const Json& rootTransform = saved.at("entities")[0].at("components")[0];
    CHECK(rootTransform.at("position")[0].get<double>() == doctest::Approx(0.0));
    CHECK(rootTransform.at("position")[1].get<double>() == doctest::Approx(0.0));
    CHECK(rootTransform.at("rotation").get<double>() == doctest::Approx(0.0));

    // ======================================================================
    //  FOUND BY NAME, BECAUSE APPLYING INVALIDATES EVERY ID IN THE INSTANCE.
    //
    //  Applying rebuilds the members from the new asset - that is how the
    //  other instances pick the change up - so every entity in every instance
    //  of this prefab is destroyed and created again, and `first` and
    //  `second` now refer to slots whose generation has moved on.
    //
    //  Pinned rather than glossed over, because this is exactly the sharp
    //  edge anything holding a selection has to cope with. The editor's
    //  selection is a list of EntityIds, so it re-finds its member through
    //  Scene::PrefabMember afterwards; see ApplyPrefabInstance in Prefab.h.
    // ======================================================================
    CHECK(world.scene.Get(first) == nullptr);
    CHECK(world.scene.Get(second) == nullptr);

    // Both instances are still exactly where they were put.
    CHECK(EntityNamed(world.scene, "Enemy").Transform().LocalPosition().x ==
          doctest::Approx(120.0f));
    CHECK(EntityNamed(world.scene, "Enemy_1").Transform().LocalPosition().x ==
          doctest::Approx(900.0f));
}

// ===========================================================================
//  ADDING AND REMOVING COMPONENTS ON AN INSTANCE
// ===========================================================================

TEST_CASE("a component added to one instance is recorded and comes back") {
    Fixture world("added");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    REQUIRE(EntityNamed(world.scene, "Muzzle")
                .AddComponent(BoxColliderComponent::kTypeName) != nullptr);

    const Json& record = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(record.contains("addedComponents"));
    REQUIRE(record.at("addedComponents").size() == 1);
    CHECK(record.at("addedComponents")[0].at("entity") == 2);
    CHECK(record.at("addedComponents")[0].at("component").at("type") ==
          "BoxColliderComponent");

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    Entity& restored = EntityNamed(world.scene, "Muzzle");
    CHECK(restored.Find<BoxColliderComponent>() != nullptr);
    CHECK(restored.Find<SpinComponent>() != nullptr);   // still has the asset's
}

TEST_CASE("a component removed from one instance stays removed") {
    Fixture world("removed");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error).IsNull());

    REQUIRE(EntityNamed(world.scene, "Muzzle").RemoveComponent(SpinComponent::kTypeName));

    const Json& record = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(record.contains("removedComponents"));
    REQUIRE(record.at("removedComponents").size() == 1);
    CHECK(record.at("removedComponents")[0].at("entity") == 2);
    CHECK(record.at("removedComponents")[0].at("component") == "SpinComponent");

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    Entity& restored = EntityNamed(world.scene, "Muzzle");
    CHECK(restored.Find<SpinComponent>() == nullptr);
    CHECK(restored.Find<TransformComponent>() != nullptr);
}

// ===========================================================================
//  NESTED FIELDS - one variable at a time
// ===========================================================================

TEST_CASE("overriding one nested variable leaves the others following the asset") {
    // ======================================================================
    //  A script's variables arrive nested: "vars": { "damage": 5, "speed": 2 }.
    //  If the diff treated that object as ONE value, changing damage on an
    //  instance would override speed as well - and a later change to speed in
    //  the asset would never arrive. Hence dotted paths; see Prefab.h.
    // ======================================================================
    Fixture world("nested");

    Json script              = Json::object();
    script["type"]           = "ScriptComponent";
    script["script"]         = "Patrol";
    script["vars"]           = Json::object({{"damage", 5}, {"speed", 2}});

    Json transform        = Json::object();
    transform["type"]     = "TransformComponent";
    transform["position"] = Json::array({0.0, 0.0});
    transform["rotation"] = 0.0;
    transform["scale"]    = Json::array({1.0, 1.0});

    Json rootNode          = Json::object();
    rootNode["id"]         = 1;
    rootNode["name"]       = "Walker";
    rootNode["components"] = Json::array({transform, script});

    Json document        = Json::object();
    document["prefab"]   = 1;
    document["name"]     = "Walker";
    document["root"]     = 1;
    document["entities"] = Json::array({rootNode});
    world.Write("prefabs/walker.oprefab", document);

    // A scene naming one overridden variable, written by hand because nothing
    // outside the editor can set a script's saved values.
    Json record            = Json::object();
    record["prefab"]       = "prefabs/walker.oprefab";
    record["name"]         = "Walker";
    Json item              = Json::object();
    item["entity"]         = 1;
    item["component"]      = "ScriptComponent";
    item["field"]          = "vars.damage";
    item["value"]          = 99;
    record["overrides"]    = Json::array({item});

    Json sceneDocument              = Json::object();
    sceneDocument["name"]           = "Level";
    sceneDocument["entities"]       = Json::array();
    sceneDocument["prefabInstances"] = Json::array({record});

    std::string error;
    REQUIRE(world.scene.LoadFromString(sceneDocument.dump(), error));

    const ScriptComponent& bound = ComponentOn<ScriptComponent>(world.scene, "Walker");

    // The one that was overridden, and the one that was not.
    CHECK(bound.SavedValues().at("damage").get<int>() == 99);
    CHECK(bound.SavedValues().at("speed").get<int>() == 2);

    // And saving records that ONE field, not the whole set.
    const Json& written = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(OverrideCount(written) == 1);
    CHECK(written.at("overrides")[0].at("field") == "vars.damage");
}

// ===========================================================================
//  SEVERAL COMPONENTS OF ONE TYPE ON ONE ENTITY
// ===========================================================================

namespace {

// A prefab whose root carries TWO SpinComponents, which is the shape that
// makes `slot` mean something. An entity with three scripts on it is the case
// this really stands for; two spins need no compiler.
Json TwoSpinsPrefab() {
    Json transform        = Json::object();
    transform["type"]     = "TransformComponent";
    transform["position"] = Json::array({0.0, 0.0});
    transform["rotation"] = 0.0;
    transform["scale"]    = Json::array({1.0, 1.0});

    Json slow                = Json::object();
    slow["type"]             = "SpinComponent";
    slow["radiansPerSecond"] = 1.0;

    Json fast                = Json::object();
    fast["type"]             = "SpinComponent";
    fast["radiansPerSecond"] = 2.0;

    Json rootNode          = Json::object();
    rootNode["id"]         = 1;
    rootNode["name"]       = "Wheel";
    rootNode["components"] = Json::array({transform, slow, fast});

    Json document         = Json::object();
    document["prefab"]    = 1;
    document["name"]      = "Wheel";
    document["root"]      = 1;
    document["entities"]  = Json::array({rootNode});
    return document;
}

// The nth component of a type on an entity, which is what a slot names.
SpinComponent& SpinAt(Entity& entity, int slot) {
    int seen = 0;
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        Component* component = entity.ComponentAt(i);
        if (component == nullptr ||
            std::string_view(component->TypeName()) != SpinComponent::kTypeName) {
            continue;
        }
        if (seen == slot) {
            return *static_cast<SpinComponent*>(component);
        }
        ++seen;
    }
    REQUIRE_MESSAGE(false, "no SpinComponent at slot ", slot);
    return *static_cast<SpinComponent*>(entity.ComponentAt(0));   // never reached
}

} // namespace

TEST_CASE("changing the second of two components of a type leaves the first alone") {
    // ======================================================================
    //  WHAT `slot` IS FOR. Three scripts on one entity all serialise under
    //  the name "ScriptComponent", so an override has to say WHICH - and it
    //  cannot say "the third component", because the position in the list
    //  moves the moment anything before it is added or removed.
    //
    //  Without this test the whole slot mechanism is only ever exercised at
    //  slot 0, where every off-by-one looks correct.
    // ======================================================================
    Fixture world("slots");
    world.Write("prefabs/wheel.oprefab", TwoSpinsPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/wheel.oprefab", error).IsNull());

    Entity& wheel = EntityNamed(world.scene, "Wheel");
    CHECK(SpinAt(wheel, 0).RadiansPerSecond() == doctest::Approx(1.0f));
    CHECK(SpinAt(wheel, 1).RadiansPerSecond() == doctest::Approx(2.0f));

    SpinAt(wheel, 1).SetRadiansPerSecond(7.0f);

    // ONE override, naming slot 1 - and the slot is written down, which it is
    // not for slot 0.
    const Json record = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(OverrideCount(record) == 1);
    CHECK(record.at("overrides")[0].at("component") == "SpinComponent");
    CHECK(record.at("overrides")[0].at("slot") == 1);
    CHECK(record.at("overrides")[0].at("value").get<double>() == doctest::Approx(7.0));

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    Entity& restored = EntityNamed(world.scene, "Wheel");
    CHECK(SpinAt(restored, 0).RadiansPerSecond() == doctest::Approx(1.0f));
    CHECK(SpinAt(restored, 1).RadiansPerSecond() == doctest::Approx(7.0f));
}

TEST_CASE("removing the first of two components of a type still gives one survivor") {
    // ======================================================================
    //  THE ONE KNOWN WART, pinned here so that it is a decision rather than a
    //  surprise. Components are matched to the asset's by POSITION among those
    //  of their type, so removing the FIRST of two spins is not recorded as
    //  "slot 0 removed". It is recorded as "slot 0 now holds the second one's
    //  value" plus "slot 1 removed".
    //
    //  THE OUTCOME IS RIGHT - one spin, at the surviving rate - and that is
    //  what this checks. The representation is odd, and an override list that
    //  reads oddly is a far smaller price than matching components by identity
    //  would be, which would mean giving every component a stable id of its
    //  own in every scene file in the project.
    // ======================================================================
    Fixture world("slot-removal");
    world.Write("prefabs/wheel.oprefab", TwoSpinsPrefab());

    std::string error;
    REQUIRE_FALSE(world.scene.InstantiatePrefab("prefabs/wheel.oprefab", error).IsNull());

    Entity& wheel = EntityNamed(world.scene, "Wheel");
    REQUIRE(wheel.RemoveComponent(SpinComponent::kTypeName));   // takes the first
    CHECK(SpinAt(wheel, 0).RadiansPerSecond() == doctest::Approx(2.0f));

    const Json record = InstanceRecord(SavedDocument(world.scene));
    REQUIRE(record.contains("removedComponents"));
    REQUIRE(record.at("removedComponents").size() == 1);
    CHECK(record.at("removedComponents")[0].at("slot") == 1);

    std::string text;
    REQUIRE(world.scene.SaveToString(text, error));
    REQUIRE(world.scene.LoadFromString(text, error));

    // ONE spin, spinning at the rate the survivor had. That is the whole
    // contract; which record said so is an implementation detail.
    Entity& restored = EntityNamed(world.scene, "Wheel");
    int     spins    = 0;
    for (std::size_t i = 0; i < restored.ComponentCount(); ++i) {
        if (const Component* component = restored.ComponentAt(i);
            component != nullptr &&
            std::string_view(component->TypeName()) == SpinComponent::kTypeName) {
            ++spins;
        }
    }
    CHECK(spins == 1);
    CHECK(SpinAt(restored, 0).RadiansPerSecond() == doctest::Approx(2.0f));
}

// ===========================================================================
//  UNPACKING, DESTROYING, AND A MISSING ASSET
// ===========================================================================

TEST_CASE("unpacking keeps every entity and drops only the link") {
    Fixture world("unpack");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(root.IsNull());

    REQUIRE(world.scene.UnpackPrefabInstance(root));

    CHECK(world.scene.EntityCount() == 2);
    CHECK(world.scene.PrefabInstanceCount() == 0);
    CHECK(world.scene.PrefabInstanceOf(root) == nullptr);

    // They are ordinary entities now, so they are written as entities - and
    // the stale instance list is gone rather than left to grow them back.
    const Json document = SavedDocument(world.scene);
    CHECK(document.at("entities").size() == 2);
    CHECK_FALSE(document.contains("prefabInstances"));
}

TEST_CASE("destroying an instance's root destroys the whole instance") {
    // DestroyEntityImmediate hands an entity's children back to the WORLD, so
    // without the special case a destroyed root would leave its members
    // scattered at the top level still claiming to belong to an instance.
    Fixture world("destroy-root");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(root.IsNull());
    REQUIRE(world.scene.EntityCount() == 2);

    world.scene.DestroyEntityImmediate(root);

    CHECK(world.scene.EntityCount() == 0);
    CHECK(world.scene.PrefabInstanceCount() == 0);
}

TEST_CASE("a plain entity parented under an instance survives the instance") {
    Fixture world("added-child");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId root = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(root.IsNull());

    const EntityId extra = world.scene.CreateEntity("Flag");
    world.scene.Get(extra)->Transform().SetParent(
        &EntityNamed(world.scene, "Muzzle").Transform());

    // It is an ordinary entity that happens to sit under a prefab child - see
    // Prefab.h on added children - so destroying the instance hands it back
    // to the world rather than taking it along.
    world.scene.DestroyEntityImmediate(root);

    CHECK(world.scene.EntityCount() == 1);
    CHECK(EntityNamed(world.scene, "Flag").Transform().Parent() == nullptr);
}

TEST_CASE("an instance whose prefab has gone keeps the overrides it had") {
    // ======================================================================
    //  The usual reason a prefab cannot be read is that somebody moved it.
    //  Dropping the record would mean opening the scene, noticing the missing
    //  enemy, fixing the path - and finding every change ever made to that
    //  instance gone, because a save in between wrote the scene without it.
    // ======================================================================
    Fixture world("missing");

    Json item           = Json::object();
    item["entity"]      = 2;
    item["component"]   = "SpinComponent";
    item["field"]       = "radiansPerSecond";
    item["value"]       = 9.0;

    Json record         = Json::object();
    record["prefab"]    = "prefabs/gone.oprefab";
    record["name"]      = "Enemy";
    record["overrides"] = Json::array({item});

    Json sceneDocument               = Json::object();
    sceneDocument["name"]            = "Level";
    sceneDocument["entities"]        = Json::array();
    sceneDocument["prefabInstances"] = Json::array({record});

    std::string error;
    REQUIRE(world.scene.LoadFromString(sceneDocument.dump(), error));

    // Nothing was built, because there was nothing to build it from.
    CHECK(world.scene.EntityCount() == 0);
    CHECK(world.scene.PrefabInstanceCount() == 1);

    // But the record, and the work in it, is still there to be saved.
    const Json& written = InstanceRecord(SavedDocument(world.scene));
    CHECK(written.at("prefab") == "prefabs/gone.oprefab");
    REQUIRE(OverrideCount(written) == 1);
    CHECK(written.at("overrides")[0].at("field") == "radiansPerSecond");
}

// ===========================================================================
//  MAKING ONE
// ===========================================================================

TEST_CASE("making a prefab out of an entity replaces it with an instance of itself") {
    // ======================================================================
    //  This is the difference between a prefab system and an export button.
    //  What you were working on is now LINKED to the asset, so the next change
    //  you make to it can be pushed to every copy. Unity does the same thing
    //  when you drag a GameObject into the Project window.
    // ======================================================================
    Fixture world("create");

    const EntityId root = world.scene.CreateEntity("Turret");
    world.scene.Get(root)->Transform().SetLocalPosition(Vec2{30.0f, 12.0f});
    REQUIRE(world.scene.Get(root)->AddComponent(SpinComponent::kTypeName) != nullptr);

    const EntityId child = world.scene.CreateEntity("Barrel");
    world.scene.Get(child)->Transform().SetParent(&world.scene.Get(root)->Transform());
    world.scene.Get(child)->Transform().SetLocalPosition(Vec2{0.0f, 5.0f});

    EntityId    instanceRoot;
    std::string error;
    REQUIRE(CreatePrefabFromEntity(world.scene, root, "prefabs/turret.oprefab",
                                   instanceRoot, error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(instanceRoot.IsNull());

    // The file exists and describes both entities, numbered from the root.
    const Json saved = world.Read("prefabs/turret.oprefab");
    CHECK(saved.at("root") == 1);
    REQUIRE(saved.at("entities").size() == 2);
    CHECK(saved.at("entities")[0].at("name") == "Turret");
    CHECK(saved.at("entities")[1].at("name") == "Barrel");
    CHECK(saved.at("entities")[1].at("parent") == 1);

    // The scene has the same two entities, by the same names, in the same
    // place - and they are now an instance.
    CHECK(world.scene.EntityCount() == 2);
    Entity& turret = EntityNamed(world.scene, "Turret");
    CHECK(turret.Id() == instanceRoot);
    CHECK(turret.Transform().LocalPosition().x == doctest::Approx(30.0f));
    CHECK(turret.Find<SpinComponent>() != nullptr);
    CHECK(world.scene.PrefabInstanceOf(instanceRoot) != nullptr);
    CHECK(world.scene.IsPrefabRoot(instanceRoot));

    // And with nothing overridden, because it IS the asset.
    CHECK(OverrideCount(InstanceRecord(SavedDocument(world.scene))) == 0);

    // Refuses a second time rather than reaching into every scene that uses
    // it. See CreatePrefabFromEntity.
    EntityId again;
    CHECK_FALSE(CreatePrefabFromEntity(world.scene, instanceRoot,
                                       "prefabs/turret.oprefab", again, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("a prefab cannot be made out of something already inside a prefab") {
    // ======================================================================
    //  NESTED PREFABS ARE NOT SUPPORTED, so the question is what happens when
    //  somebody asks for one. The tempting answer is to flatten: write plain
    //  copies of the inner instance's entities into the new file. That is the
    //  wrong answer, because the result LOOKS like prefab content and is not -
    //  it would sit there ignoring every later change to the prefab it was
    //  taken from, and nothing would have said so.
    //
    //  Checked at three depths, because the obvious implementation of this
    //  guard only looks at the entity it was handed.
    // ======================================================================
    Fixture world("nested-refusal");
    world.Write("prefabs/enemy.oprefab", TwoEntityPrefab());

    std::string    error;
    const EntityId instance = world.scene.InstantiatePrefab("prefabs/enemy.oprefab", error);
    REQUIRE_FALSE(instance.IsNull());

    EntityId made;

    // ---- the instance root itself -----------------------------------------
    CHECK_FALSE(CreatePrefabFromEntity(world.scene, instance, "prefabs/a.oprefab", made,
                                       error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(FileSystem::Exists("prefabs/a.oprefab"));

    // ---- one of its children ----------------------------------------------
    const EntityId child = world.scene.Find("Muzzle");
    REQUIRE_FALSE(child.IsNull());
    CHECK_FALSE(CreatePrefabFromEntity(world.scene, child, "prefabs/b.oprefab", made,
                                       error));
    CHECK_FALSE(FileSystem::Exists("prefabs/b.oprefab"));

    // ---- AND A PLAIN ENTITY WITH THE INSTANCE UNDERNEATH IT ----------------
    //
    // The one the guard has to walk the tree to find. An ordinary entity that
    // happens to be the instance's parent is not itself part of a prefab, so
    // a check that only looked at the entity it was given would say yes.
    const EntityId holder = world.scene.CreateEntity("Holder");
    world.scene.Get(instance)->Transform().SetParent(
        &world.scene.Get(holder)->Transform());

    CHECK_FALSE(CreatePrefabFromEntity(world.scene, holder, "prefabs/c.oprefab", made,
                                       error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(FileSystem::Exists("prefabs/c.oprefab"));

    // And nothing was destroyed on the way to refusing.
    CHECK(world.scene.EntityCount() == 3);
    CHECK(world.scene.PrefabInstanceCount() == 1);
}
