// ============================================================================
//  Tests for eng::PlayerInputComponent and eng::PlayerInputSystem.
//
//  THE ONE THAT MATTERS MOST IS "a run-time switch is not an authored
//  change". Every undo snapshot is a whole SaveToString, so a component that
//  wrote its live current map into the field Serialize reads would, after
//  enough ordinary editing, open the scene with the player already in the
//  pause menu. There
//  is no symptom until then and no way to tell what did it. SetTexture and
//  ShowTexture are two separate calls on SpriteComponent for exactly this
//  reason; these are two separate members for the same one.
//
//  SECOND IS THAT Serialize IS OVERRIDDEN AT ALL. Component's default
//  returns false, which the scene writer reads as "this cannot be saved" and
//  silently DROPS the component. A component that forgets the override works
//  perfectly - in the Inspector, in play mode, everywhere - right up until
//  the first Ctrl+Z, Play, Duplicate or prefab apply, where it disappears
//  with one warning in a console nobody is watching.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/core/Json.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputActionAsset.h>
#include <engine/input/InputActions.h>
#include <engine/input/PlayerInputComponent.h>
#include <engine/math/Vec2.h>
#include <engine/platform/EventPump.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

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

// A project folder with one .oinput in it, plus an active scene, for as long
// as it is in scope.
struct PlayerSandbox {
    fs::path        root;
    eng::FileSystem fileSystem;
    eng::Scene      scene;
    eng::Scene*     previous = nullptr;

    explicit PlayerSandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-player-test-") + name);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "input", ec);

        SetAssetRoot(root.string().c_str());
        fileSystem.Init(eng::BootConfig{});
        eng::InputActionLibrary::Clear();
        eng::PlayerInputSystem::Clear();
        eng::InputActions::ReleaseAll();

        Write("input/player.oinput", R"({
          "defaultMap": "Gameplay",
          "maps": [
            { "name": "Gameplay", "actions": [
                { "name": "Jump", "type": "Button", "bindings": ["Key.Space"] },
                { "name": "Move", "type": "Value",  "bindings": [
                    { "composite": "2DVector", "up": "Key.W", "down": "Key.S",
                      "left": "Key.A", "right": "Key.D" } ] } ] },
            { "name": "Menu", "actions": [
                { "name": "Back", "type": "Button", "bindings": ["Key.Escape"] } ] }
          ] })");

        previous = eng::Scene::Active();
        eng::Scene::SetActive(&scene);
    }

    ~PlayerSandbox() {
        eng::Scene::SetActive(previous);

        // =================================================================
        //  THE SCENE IS TORN DOWN FIRST, AND THE ORDER IS NOT COSMETIC.
        //
        //  `scene` is a MEMBER, so leaving it to unwind destroys it after
        //  this body has already run - which means after the library below
        //  has freed every asset. Detaching a PlayerInput cancels what it
        //  has in flight, and cancelling WALKS THE ACTION MAP
        //  (ActionMapState::CancelEverything dereferences m_map), so the
        //  asset has to still be alive at that moment. Getting it backwards
        //  is a use-after-free that only faults when the freed page has
        //  actually gone: measured at 20 crashes in 25 runs of one case in
        //  isolation, but only about one full-suite run in six, which is
        //  exactly the profile that reads as "a flaky test".
        //
        //  Unload() here is the same order Engine.cpp shuts down in - the
        //  scene at :168, the libraries at :191-193 - so this fixture now
        //  matches the only sequence the engine itself ever produces.
        // =================================================================
        scene.Unload();

        eng::PlayerInputSystem::Clear();
        eng::InputActionLibrary::Clear();
        eng::InputActions::ReleaseAll();
        fileSystem.Shutdown();
        SetAssetRoot("");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path  real{eng::FileSystem::Resolve(virtualPath)};
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }

    // An entity carrying a PlayerInput pointed at the file above.
    eng::PlayerInputComponent* MakePlayer(const char* mapName = "Gameplay") {
        // UNIQUELY NAMED, because a case with three players in it would
        // otherwise make three called "Player" - and the scene says so
        // loudly, with an Error per collision, since a name is an identity
        // here. Nothing in these cases looks an entity up by name.
        const eng::EntityId id     = scene.CreateEntityUniquelyNamed("Player");
        eng::Entity*       entity = scene.Get(id);
        REQUIRE(entity != nullptr);

        eng::Json node;
        node["actions"] = "input/player.oinput";
        if (mapName != nullptr) {
            node["defaultMap"] = mapName;
        }

        auto component = eng::ComponentFactory::Create(
            eng::PlayerInputComponent::kTypeName);
        REQUIRE(component != nullptr);     // the factory knows the type name

        std::string error;
        REQUIRE(component->Deserialize(node, error));
        entity->AddComponent(std::move(component));
        return entity->Find<eng::PlayerInputComponent>();
    }
};

eng::RawEvent Key(const char* name, bool down) {
    eng::RawEvent event{};
    event.code = eng::EventPump::KeyCodeFromName(name);
    event.kind = down ? eng::RawEventKind::KeyDown : eng::RawEventKind::KeyUp;
    return event;
}

void Feed(std::initializer_list<eng::RawEvent> events) {
    eng::InputActions::Sample(std::span<const eng::RawEvent>{events.begin(), events.size()});
}

} // namespace

TEST_CASE("the factory knows the type, and an empty path is a usable component") {
    PlayerSandbox box("empty");

    // EXACTLY WHAT "Add Component" PRODUCES. A component that refused this
    // would be unusable from the moment it was added.
    auto component =
        eng::ComponentFactory::Create(eng::PlayerInputComponent::kTypeName);
    REQUIRE(component != nullptr);

    eng::Json   node;
    std::string error;
    CHECK(component->Deserialize(node, error));
    CHECK(error.empty());

    auto* input = static_cast<eng::PlayerInputComponent*>(component.get());
    CHECK(input->ActionsPath().empty());
    CHECK(input->Asset() == nullptr);
    CHECK(input->CurrentActionMapName().empty());

    // And asking it anything is zero rather than a crash.
    CHECK(input->Value("Jump") == doctest::Approx(0.0f));
    CHECK_FALSE(input->IsDown("Jump"));
    input->Dispatch();
}

TEST_CASE("a component built and never attached destructs cleanly") {
    PlayerSandbox box("unattached");

    // The scene loader creates and deserializes BEFORE it attaches, so a
    // scene that fails partway through leaves exactly this. The destructor
    // unregisters as a safety net; without it the system would be left
    // holding a dangling pointer.
    CHECK(eng::PlayerInputSystem::Count() == 0);
    {
        auto component =
            eng::ComponentFactory::Create(eng::PlayerInputComponent::kTypeName);
        eng::Json   node;
        std::string error;
        component->Deserialize(node, error);
        CHECK(eng::PlayerInputSystem::Count() == 0);   // not registered until attached
    }
    CHECK(eng::PlayerInputSystem::Count() == 0);
}

TEST_CASE("attaching registers it and detaching takes it away") {
    PlayerSandbox box("register");
    CHECK(eng::PlayerInputSystem::Count() == 0);

    eng::PlayerInputComponent* input = box.MakePlayer();
    REQUIRE(input != nullptr);
    CHECK(eng::PlayerInputSystem::Count() == 1);

    CHECK(input->ActionsPath() == "input/player.oinput");
    REQUIRE(input->Asset() != nullptr);
    CHECK(input->CurrentActionMapName() == "Gameplay");

    box.scene.DestroyEntityImmediate(input->Owner()->Id());
    CHECK(eng::PlayerInputSystem::Count() == 0);
}

TEST_CASE("a run-time map switch is not an authored change") {
    // THE TRAP THIS COMPONENT IS SHAPED AROUND.
    PlayerSandbox box("authored");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    CHECK(input->DefaultMapName() == "Gameplay");
    CHECK(input->CurrentActionMapName() == "Gameplay");

    REQUIRE(input->SwitchCurrentActionMap("Menu"));
    CHECK(input->CurrentActionMapName() == "Menu");

    // The LIVE map moved and the AUTHORED one did not.
    CHECK(input->DefaultMapName() == "Gameplay");

    eng::Json written;
    REQUIRE(input->Serialize(written));
    CHECK(written["defaultMap"] == "Gameplay");
    CHECK(written["actions"] == "input/player.oinput");

    // And the component does NOT write its own "type" - the scene does that,
    // so no component can get its own name wrong.
    CHECK_FALSE(written.contains("type"));
}

TEST_CASE("switching to a map that is not there keeps the one that works") {
    PlayerSandbox box("nosuchmap");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    CHECK_FALSE(input->SwitchCurrentActionMap("Vehicle"));

    // ENDING UP WITH NO CONTROLS AT ALL is worse than carrying on with the
    // ones that were working.
    CHECK(input->CurrentActionMapName() == "Gameplay");
}

TEST_CASE("the component survives a scene round trip") {
    PlayerSandbox box("roundtrip");

    eng::PlayerInputComponent* input = box.MakePlayer("Menu");
    REQUIRE(input != nullptr);
    REQUIRE(input->SwitchCurrentActionMap("Gameplay"));   // live, must not be saved

    std::string text, error;
    REQUIRE(box.scene.SaveToString(text, error));

    // THIS IS WHAT UNDO, PLAY-MODE RESTORE AND DUPLICATE ALL DO. A component
    // whose Serialize returns false is dropped here with one warning.
    REQUIRE(box.scene.LoadFromString(text, error));

    eng::Entity* entity = box.scene.FindEntity("Player");
    REQUIRE(entity != nullptr);
    eng::PlayerInputComponent* restored = entity->Find<eng::PlayerInputComponent>();
    REQUIRE(restored != nullptr);

    CHECK(restored->ActionsPath() == "input/player.oinput");
    CHECK(restored->DefaultMapName() == "Menu");          // the authored one
    CHECK(restored->CurrentActionMapName() == "Menu");    // starts there, not Gameplay
    CHECK(eng::PlayerInputSystem::Count() == 1);
}

TEST_CASE("events reach the component and the system dispatches it") {
    PlayerSandbox box("dispatch");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    std::vector<std::string> log;
    input->Subscribe([&log](const eng::ActionEvent& event) {
        log.emplace_back(std::string(event.action) + ":" + eng::ToString(event.phase));
    });

    // FRAME ONE, which is where the asset is actually resolved - the load is
    // lazy, so the map is not set until something asks. This matters for the
    // line after it rather than for itself.
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.empty());

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log == std::vector<std::string>{"Jump:Started", "Jump:Performed"});

    Feed({Key("W", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(input->Vector("Move").y == doctest::Approx(1.0f));
    CHECK(input->IsDown("Move"));

    Feed({Key("Space", false), Key("W", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.back() == "Move:Canceled");
}

TEST_CASE("a key already held when the map first resolves is Unity's two answers") {
    // A player holding keys as the level loads. The map is resolved lazily,
    // on the first dispatch, so everything already down at that moment counts
    // as "held from before" - and Unity's initial state check gives the two
    // action types opposite answers, which is what this pins.
    PlayerSandbox box("heldatload");

    Feed({Key("Space", true), Key("W", true)});

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    std::vector<std::string> log;
    input->Subscribe([&log](const eng::ActionEvent& event) {
        log.emplace_back(std::string(event.action) + ":" + eng::ToString(event.phase));
    });

    eng::PlayerInputSystem::FrameDispatch();

    // A VALUE action adopts it, so holding a direction while the level loads
    // walks you forward, which is what a player expects.
    CHECK(input->Vector("Move").y == doctest::Approx(1.0f));
    CHECK(log == std::vector<std::string>{"Move:Started", "Move:Performed"});

    // A BUTTON does not, so resting a finger on the jump key while a level
    // loads does not jump the instant it finishes.
    CHECK_FALSE(input->IsDown("Jump"));

    log.clear();
    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.empty());                       // no phantom cancel for a press never seen

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log == std::vector<std::string>{"Jump:Started", "Jump:Performed"});
}

TEST_CASE("a switched-off entity is skipped") {
    PlayerSandbox box("inactive");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    std::vector<std::string> log;
    input->Subscribe([&log](const eng::ActionEvent& event) {
        log.emplace_back(std::string(event.action));
    });

    input->Owner()->SetActive(false);
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.empty());

    // And switching it back on does not replay the press it missed - the
    // button was already held when the entity woke up, so it waits for a
    // genuine one. Same rule as switching maps with a key held.
    input->Owner()->SetActive(true);
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.empty());
}

TEST_CASE("Disable cancels what is running and Enable puts the same map back") {
    PlayerSandbox box("disable");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    REQUIRE(input->SwitchCurrentActionMap("Menu"));

    std::vector<std::string> log;
    input->Subscribe([&log](const eng::ActionEvent& event) {
        log.emplace_back(std::string(event.action) + ":" + eng::ToString(event.phase));
    });

    Feed({Key("Escape", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log == std::vector<std::string>{"Back:Started", "Back:Performed"});
    log.clear();

    // A script that started something on the press is told to stop it, rather
    // than being left holding it forever.
    input->Disable();
    CHECK_FALSE(input->IsEnabled());
    CHECK(log == std::vector<std::string>{"Back:Canceled"});
    log.clear();

    eng::PlayerInputSystem::FrameDispatch();
    CHECK(log.empty());

    // THE SAME MAP, not the file's default. The name is kept precisely so
    // Enable does not quietly put the player back in Gameplay.
    input->Enable();
    CHECK(input->CurrentActionMapName() == "Menu");
}

TEST_CASE("pointing it at a different file starts on that file's own default") {
    PlayerSandbox box("setactions");
    box.Write("input/vehicle.oinput", R"({
      "defaultMap": "Driving",
      "maps": [ { "name": "Driving", "actions": [
          { "name": "Honk", "type": "Button", "bindings": ["Key.H"] } ] } ] })");

    eng::PlayerInputComponent* input = box.MakePlayer("Menu");
    REQUIRE(input != nullptr);
    CHECK(input->CurrentActionMapName() == "Menu");

    std::string error;
    REQUIRE(input->SetActions("input/vehicle.oinput", error));

    // NOT "Menu" - the two files need not share a single map name, so
    // carrying the old current map across would usually land on nothing.
    CHECK(input->CurrentActionMapName() == "Driving");
    CHECK(input->ActionsPath() == "input/vehicle.oinput");

    Feed({Key("H", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(input->IsDown("Honk"));
}

TEST_CASE("a path that names no file is reported and leaves the component usable") {
    PlayerSandbox box("badpath");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    std::string error;
    CHECK_FALSE(input->SetActions("input/ghost.oinput", error));
    CHECK_FALSE(error.empty());

    CHECK(input->Asset() == nullptr);
    CHECK(input->CurrentActionMapName().empty());
    CHECK_FALSE(input->SwitchCurrentActionMap("Gameplay"));

    // Still answers everything, still dispatches, still saves.
    input->Dispatch();
    CHECK(input->Value("Jump") == doctest::Approx(0.0f));

    eng::Json written;
    CHECK(input->Serialize(written));
    CHECK(written["actions"] == "input/ghost.oinput");
}

// ===========================================================================
//  SAVING THE FILE UNDERNEATH A LIVE COMPONENT.
//
//  InputActionLibrary::Reload refills the asset's map vector IN PLACE, so the
//  asset object survives but every ActionMap* taken out of it beforehand does
//  not - and a PlayerInput caches exactly one of those, resolved once on the
//  first ask and never re-read. ReloadActions was written for this and had no
//  caller anywhere in the tree, which made "edit a binding, Save, press Play"
//  drive freed or renumbered storage.
//
//  WHAT THIS PINS IS ReloadAll's BEHAVIOUR, not the editor's call to it: the
//  test program links the engine and not the editor, so the call site in
//  InputActionsPanel::Save is covered by the build and not by this file.
//  The negative half cannot be written at all - observing the stale map means
//  dereferencing freed storage, which is undefined rather than merely wrong.
// ===========================================================================

TEST_CASE("a library reload plus ReloadAll puts a live component on the new contents") {
    PlayerSandbox box("reloadall");

    // ONE map, ONE action.
    box.Write("input/player.oinput", R"({
      "defaultMap": "Gameplay",
      "maps": [
        { "name": "Gameplay", "actions": [
            { "name": "Jump", "type": "Button", "bindings": ["Key.Space"] }
        ]}
      ]
    })");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    CHECK(eng::PlayerInputSystem::Count() == 1);

    // Resolve it, so there is a cached asset and a cached map to invalidate.
    CHECK(input->CurrentActionMapName() == "Gameplay");
    const eng::InputActionAsset* const asset = input->Asset();
    REQUIRE(asset != nullptr);

    // THE WINDOW SAVES A LONGER FILE. More maps than before, so the vector
    // reallocates rather than being refilled where it stood - the case where
    // the old pointer is freed outright rather than merely renumbered.
    box.Write("input/player.oinput", R"({
      "defaultMap": "Gameplay",
      "maps": [
        { "name": "Gameplay", "actions": [
            { "name": "Jump", "type": "Button", "bindings": ["Key.Space"] },
            { "name": "Fire", "type": "Button", "bindings": ["Key.Return"] }
        ]},
        { "name": "Menu", "actions": [
            { "name": "Back", "type": "Button", "bindings": ["Key.Escape"] }
        ]}
      ]
    })");

    std::string reloadError;
    REQUIRE(eng::InputActionLibrary::Reload("input/player.oinput", reloadError));
    CHECK(reloadError.empty());

    eng::PlayerInputSystem::ReloadAll("input/player.oinput");

    // The asset OBJECT is the same one - that is what makes identity a safe
    // way for ReloadAll to find the components that care.
    CHECK(input->Asset() == asset);

    // The map is re-resolved BY NAME, which is the whole reason the name is
    // what gets kept across a reload rather than the pointer.
    CHECK(input->CurrentActionMapName() == "Gameplay");

    // AND IT IS ON THE NEW CONTENTS. Fire exists only in the file as saved;
    // a component still looking at the old map has no such action and reads
    // zero for it forever.
    eng::InputActions::ReleaseAll();
    Feed({});
    input->Dispatch();

    Feed({Key("Return", true)});
    input->Dispatch();
    CHECK(input->IsDown("Fire"));

    Feed({Key("Return", false)});
    input->Dispatch();
    CHECK_FALSE(input->IsDown("Fire"));

    eng::InputActions::ReleaseAll();
}

TEST_CASE("ReloadAll leaves components bound to other files alone") {
    PlayerSandbox box("reloadall-other");

    box.Write("input/player.oinput", R"({
      "defaultMap": "Gameplay",
      "maps": [
        { "name": "Gameplay", "actions": [
            { "name": "Jump", "type": "Button", "bindings": ["Key.Space"] }
        ]}
      ]
    })");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    CHECK(input->CurrentActionMapName() == "Gameplay");

    // RE-RESOLVING CANCELS WHAT IS IN FLIGHT, so a save of somebody else's
    // file must not reach this component. Hold Space, then save a different
    // path: the press has to still be down afterwards.
    eng::InputActions::ReleaseAll();
    Feed({});
    input->Dispatch();
    Feed({Key("Space", true)});
    input->Dispatch();
    REQUIRE(input->IsDown("Jump"));

    eng::PlayerInputSystem::ReloadAll("input/somebody-else.oinput");

    CHECK(input->IsDown("Jump"));
    CHECK(input->CurrentActionMapName() == "Gameplay");

    eng::InputActions::ReleaseAll();
}

// ---------------------------------------------------------------------------
//  A HANDLER THAT CHANGES THE LIST IT IS BEING WALKED FROM.
//
//  Everything below is script code's privilege taken to its limit. A handler
//  reached from a dispatch may remove a Player Input - its own, or somebody
//  else's - and RemoveComponent is IMMEDIATE (Entity.cpp: OnDetach, then
//  erase, which destroys it), so the component is gone before the handler
//  returns and while the walk that reached it is still on the stack.
//
//  A PLAIN SUBSCRIBER STANDS IN FOR THE SCRIPT, which is not a weaker test
//  than registering one: PlayerInputComponent::Route is itself nothing but a
//  subscriber on the same list, so a subscriber that removes a component
//  reaches the identical code by the identical path. The generated-script path
//  on top of it has its own cases in test_inputdispatch.cpp.
// ---------------------------------------------------------------------------

TEST_CASE("a handler that removes another player does not cost the one after it") {
    PlayerSandbox box("midwalkunregister");

    eng::PlayerInputComponent* first  = box.MakePlayer();
    eng::PlayerInputComponent* second = box.MakePlayer();
    eng::PlayerInputComponent* third  = box.MakePlayer();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(eng::PlayerInputSystem::Count() == 3);

    // FRAME ONE RESOLVES THE ASSETS and raises nothing, the load being lazy.
    // Pressing the key first would make it a key held from before the map
    // existed, which a Button deliberately ignores - see the initial-state
    // case above - and the walk under test would have nothing to deliver.
    eng::PlayerInputSystem::FrameDispatch();

    // THE ID AND NOT THE POINTER, because the pointer is the thing about to
    // be destroyed.
    REQUIRE(first->Owner() != nullptr);
    const eng::EntityId firstEntity = first->Owner()->Id();

    // The middle player's handler removes the FIRST one - at or before the
    // walk's cursor, which is the half that matters. An erase there pulls a
    // later entry past the cursor; a tombstone does not.
    bool        removed       = false;
    std::size_t countInsideTheWalk = 99;
    second->Subscribe([&](const eng::ActionEvent&) {
        if (removed) {
            return;   // the cancel on the way out is not another go
        }
        removed = true;
        eng::Entity* owner = box.scene.Get(firstEntity);
        REQUIRE(owner != nullptr);
        CHECK(owner->RemoveComponent(eng::PlayerInputComponent::kTypeName));

        // ASKED FROM INSIDE THE WALK, where the list still holds the
        // tombstone. Count() is a count_if for exactly this moment: size()
        // would answer 3 and name a component that no longer exists.
        countInsideTheWalk = eng::PlayerInputSystem::Count();
    });

    int thirdHeard = 0;
    third->Subscribe([&thirdHeard](const eng::ActionEvent&) { ++thirdHeard; });

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(removed);

    // THE WHOLE POINT. Before the walk was guarded, erasing the first entry
    // moved the third player into the second's slot, the cursor had already
    // passed it, and Jump was never dispatched to it at all - not a frame
    // late, but never, because the next frame compares two equal readings and
    // sees nothing to report.
    CHECK(thirdHeard > 0);

    CHECK(countInsideTheWalk == 2);
    CHECK(eng::PlayerInputSystem::Count() == 2);

    // And the tombstone did not outlive the walk.
    CHECK(third->IsDown("Jump"));
    eng::InputActions::ReleaseAll();
}

TEST_CASE("a handler that removes the player being dispatched stops that dispatch") {
    PlayerSandbox box("selfremove");

    eng::PlayerInputComponent* player = box.MakePlayer();
    REQUIRE(player != nullptr);
    REQUIRE(player->Owner() != nullptr);
    const eng::EntityId id = player->Owner()->Id();

    // Frame one resolves the asset and raises nothing. See the case above.
    eng::PlayerInputSystem::FrameDispatch();

    // THE RECORDER IS SUBSCRIBED AFTER THE REMOVER, which is what makes it
    // an observation rather than a hope. Deliver works from a snapshot, so
    // the recorder is still reachable after the component has been destroyed
    // - and the point of the fix is that it is not reached. What it hears is
    // therefore exactly the events raised while the component was alive:
    //
    //   1. nothing for the Started, which is delivered to the remover first
    //      and never gets as far as the recorder,
    //   2. one Canceled, raised INSIDE the removal - OnDetach drops the map
    //      while Jump is still actuated, which cancels it - and delivered to
    //      a component that is, at that instant, still whole,
    //   3. and nothing at all afterwards. A Button raises Started and
    //      Performed back to back, and that Performed must not be attempted:
    //      the state it would read no longer exists.
    std::vector<std::string> heard;
    bool                     removed = false;

    player->Subscribe([&](const eng::ActionEvent&) {
        if (removed) {
            return;
        }
        removed = true;
        eng::Entity* owner = box.scene.Get(id);
        REQUIRE(owner != nullptr);
        CHECK(owner->RemoveComponent(eng::PlayerInputComponent::kTypeName));
    });
    player->Subscribe([&heard](const eng::ActionEvent& event) {
        heard.push_back(std::string(eng::ToString(event.phase)));
    });

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(removed);
    CHECK(eng::PlayerInputSystem::Count() == 0);
    REQUIRE(box.scene.Get(id) != nullptr);
    CHECK(box.scene.Get(id)->Find<eng::PlayerInputComponent>() == nullptr);

    // One event, and it is the teardown's own cancel. Nothing is delivered
    // after the component ceases to exist - not the Started still in flight,
    // and above all not the Performed that would have come next.
    REQUIRE(heard.size() == 1);
    CHECK(heard[0] == "Canceled");
    CHECK(std::find(heard.begin(), heard.end(), "Performed") == heard.end());

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a subscriber ahead of the routing one cannot hand the dead player on") {
    PlayerSandbox box("killbeforerouting");

    eng::PlayerInputComponent* player = box.MakePlayer();
    REQUIRE(player != nullptr);
    REQUIRE(player->Owner() != nullptr);
    const eng::EntityId id = player->Owner()->Id();

    // SUBSCRIBED BEFORE THE FIRST DISPATCH, which is the whole point of this
    // case. The routing lambda is taken out lazily by EnsureResolved, so
    // anything subscribed before then sits AHEAD of it in the list - and
    // every other case in this file subscribes in exactly that order, so it
    // is the ordinary arrangement and not a contrived one.
    //
    // Deliver works from a snapshot, so the entries after this one survive
    // the component being destroyed - but what they CAPTURE does not. The
    // routing lambda captures `this`, and calling it would be Route() on
    // freed storage.
    // ON Performed AND NOT ON Started, which is what makes this case bite
    // rather than merely look alarming. Route returns without touching a
    // single member when the phase is Started - it only sends on Performed,
    // and on Canceled for a non-Button - so a dangling Route() during the
    // Started delivery is harmless by luck. The Performed that follows it in
    // the same breath is the one that goes on to read Owner() and GetScene().
    bool removed = false;
    player->Subscribe([&](const eng::ActionEvent& event) {
        if (removed || event.phase != eng::ActionPhase::Performed) {
            return;
        }
        removed = true;
        eng::Entity* owner = box.scene.Get(id);
        REQUIRE(owner != nullptr);
        CHECK(owner->RemoveComponent(eng::PlayerInputComponent::kTypeName));
    });

    eng::PlayerInputSystem::FrameDispatch();   // resolves, and routes nothing
    REQUIRE_FALSE(removed);

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(removed);
    CHECK(eng::PlayerInputSystem::Count() == 0);

    eng::InputActions::ReleaseAll();
}

// ===========================================================================
//  THE DISPATCH PATH, END TO END: A KEY PRESS CALLING A REAL void OnJump().
//
//  PlayerInputComponent::Route is the whole promise of this system - "an
//  action called Jump calls a method named void OnJump() on any script on the
//  entity" - and until these cases it executed in no test at all. What looked
//  like its coverage, test_inputdispatch.cpp's three phase cases, asserted
//  against a hand COPY of the rule living in that file; the rule now has a
//  name in the engine and that file calls it, which is the other half of this
//  fix.
//
//  ==========================================================================
//  WHY THIS NEEDS A HAND-WRITTEN HANDLER TABLE
//
//  ENGINE_REGISTER_SCRIPT installs LIFECYCLE hooks and nothing else. The
//  action-handler table - the method names, which of them really bind, and
//  the dispatcher that calls one by index - is installed by the unit the
//  EDITOR GENERATES, from a text scan of the script's source. There is no
//  generator here, so this file emits the same three pieces by hand, in the
//  shape ScriptVars.cpp's GenerateScriptUnit emits them.
//
//  That is not a weaker test than running the generator. It is the only way
//  to test the CONSUMER - Route - without the producer, and the producer has
//  its own string-level cases in test_scriptvars.cpp. What is deliberately
//  NOT covered here is whether the generator would produce this table for
//  this script; the assets/scripts/PlayerActions.cpp that ships with the
//  project is what exercises that, through the editor's own script build.
//
//  THE TABLES MUST BE AT NAMESPACE SCOPE. ScriptRegistry::Entry stores the
//  raw pointers rather than copying, so arrays local to a case would dangle
//  the moment it returned.
// ===========================================================================

namespace {

// Four handlers, one of each shape the engine can bind - and one it cannot.
class ActionProbe final : public eng::ScriptBehaviour {
public:
    int       jumps = 0;
    int       moves = 0;
    int       leans = 0;
    eng::Vec2 lastMove{};
    float     lastLean = 0.0f;

    void OnStart() {}

    // The three shapes, in the order ENGINE_CALL_ACTION_HANDLER tries them.
    void OnMove(eng::Vec2 direction) {
        ++moves;
        lastMove = direction;
    }
    void OnLean(float amount) {
        ++leans;
        lastLean = amount;
    }
    void OnJump() { ++jumps; }

    // ---- the per-action Subscribe, which is NOT the On- spelling --------
    //
    //  The whole point of input->Subscribe("Jump", this, &Probe::Fire) is a
    //  method that could never have been called OnJump - it already has a
    //  name, or one method is to serve several actions. None of these four
    //  is in the handler table above, so nothing but a subscription can
    //  reach them, which is what makes the counts below mean something.
    int              fires        = 0;
    int              aims         = 0;
    int              tilts        = 0;
    int              raws         = 0;
    eng::Vec2        lastAim{};
    float            lastTilt     = 0.0f;
    eng::ActionPhase lastRawPhase = eng::ActionPhase::Started;

    void Fire() { ++fires; }
    void Aim(eng::Vec2 direction) {
        ++aims;
        lastAim = direction;
    }
    void Tilt(float amount) {
        ++tilts;
        lastTilt = amount;
    }
    void Raw(const eng::ActionEvent& event) {
        ++raws;
        lastRawPhase = event.phase;
    }

    int Quits() const { return m_quits; }

private:
    // PRIVATE ON PURPOSE. The scanner is a text scan, so it picks this up as
    // a candidate exactly as it picks up the three above - and the engine
    // then has to work out that it can never be called. This is the case
    // ENGINE_ACTION_HANDLER_BINDS exists for.
    void OnQuit() { ++m_quits; }

    int m_quits = 0;
};

} // namespace

// At file scope, after the namespace closes - the idiom test_scriptapi.cpp
// uses for all thirteen of its behaviours.
ENGINE_REGISTER_SCRIPT(ActionProbe)

namespace {

// What the generator would emit beside the registration above. The names are
// METHOD names, not action names - Route applies ActionHandlerMethodName to
// the action and compares the result against these.
constexpr const char* kProbeHandlerNames[] = {"OnMove", "OnLean", "OnJump", "OnQuit"};

// Which of them the compiler can actually reach. Built by the engine's own
// macro rather than written out, which is the point: a candidate the scanner
// guessed wrong about must cost nothing.
constexpr bool kProbeHandlerBound[] = {
    ENGINE_ACTION_HANDLER_BINDS(ActionProbe, OnMove),
    ENGINE_ACTION_HANDLER_BINDS(ActionProbe, OnLean),
    ENGINE_ACTION_HANDLER_BINDS(ActionProbe, OnJump),
    ENGINE_ACTION_HANDLER_BINDS(ActionProbe, OnQuit),
};

// A TEMPLATE, because the requires-ladder inside ENGINE_CALL_ACTION_HANDLER
// has to sit in a dependent context - InputActions.h says why. The generated
// one is a template for the same reason.
template <class T>
void DispatchProbeAction(eng::ScriptBehaviour& behaviour, std::size_t index,
                         const eng::ActionEvent& event) {
    T& self = static_cast<T&>(behaviour);
    (void)self;
    (void)event;
    switch (index) {
        case 0: ENGINE_CALL_ACTION_HANDLER(self, OnMove, event); break;
        case 1: ENGINE_CALL_ACTION_HANDLER(self, OnLean, event); break;
        case 2: ENGINE_CALL_ACTION_HANDLER(self, OnJump, event); break;
        case 3: ENGINE_CALL_ACTION_HANDLER(self, OnQuit, event); break;
        default: break;
    }
}

// Installs the table. Idempotent - SetActionHandlers overwrites and the
// registry outlives every case - so each case calls it rather than depending
// on another having run.
void InstallProbeHandlers() {
    REQUIRE(eng::ScriptRegistry::Find("ActionProbe") != nullptr);
    REQUIRE(eng::ScriptRegistry::SetActionHandlers(
        "ActionProbe", kProbeHandlerNames, kProbeHandlerBound, 4,
        &DispatchProbeAction<ActionProbe>));
}

// An actions file with one of every handler shape in it, written beside the
// fixture's own rather than replacing it: the existing cases assert what
// Gameplay contains, and a Lean action added there would be their business.
const char* kProbeActions = R"({
  "defaultMap": "Gameplay",
  "maps": [
    { "name": "Gameplay", "actions": [
        { "name": "Jump", "type": "Button", "bindings": ["Key.Space"] },
        { "name": "Move", "type": "Value",  "bindings": [
            { "composite": "2DVector", "up": "Key.W", "down": "Key.S",
              "left": "Key.A", "right": "Key.D" } ] },
        { "name": "Lean", "type": "Value",  "bindings": [
            { "composite": "1DAxis", "positive": "Key.E", "negative": "Key.Q" } ] },
        { "name": "Quit", "type": "Button", "bindings": ["Key.X"] }
    ] }
  ] })";

// Attaches an ActionProbe and hands back the live behaviour. The shape
// test_scriptapi.cpp's AttachScript uses, inlined because that one lives in
// another translation unit's anonymous namespace.
ActionProbe* AttachProbe(eng::Entity& entity) {
    eng::Json node = eng::Json::object();
    node["script"] = "ActionProbe";

    auto component = eng::ComponentFactory::Create(eng::ScriptComponent::kTypeName);
    REQUIRE(component != nullptr);
    std::string error;
    REQUIRE(component->Deserialize(node, error));

    auto* script =
        static_cast<eng::ScriptComponent*>(entity.AddComponent(std::move(component)));
    REQUIRE(script != nullptr);

    // The behaviour exists the moment it is attached - Bind runs from
    // OnAttach - so nothing has to be ticked to get hold of one.
    REQUIRE_MESSAGE(script->IsResolved(), "'ActionProbe' is not in this build");
    return static_cast<ActionProbe*>(script->Behaviour());
}

// An entity carrying a PlayerInput on the file above and an ActionProbe
// script, which is the arrangement SendMessages is about: scripts on THIS
// entity.
struct ProbeRig {
    eng::PlayerInputComponent* input   = nullptr;
    ActionProbe*               probe   = nullptr;
    eng::Entity*               entity  = nullptr;
};

ProbeRig MakeProbeRig(PlayerSandbox& box) {
    InstallProbeHandlers();
    box.Write("input/probe.oinput", kProbeActions);

    ProbeRig rig;
    rig.input = box.MakePlayer("Gameplay");
    REQUIRE(rig.input != nullptr);

    std::string error;
    REQUIRE_MESSAGE(rig.input->SetActions("input/probe.oinput", error), error);

    rig.entity = rig.input->Owner();
    REQUIRE(rig.entity != nullptr);
    rig.probe = AttachProbe(*rig.entity);
    return rig;
}

} // namespace

TEST_CASE("the handler table knows which candidates can actually be called") {
    // The macro's contract, and the reason a text scan is allowed to guess:
    // a candidate it was wrong about has to cost nothing rather than break
    // the build. All three reachable shapes bind; the private one does not.
    CHECK(kProbeHandlerBound[0]);     // OnMove(eng::Vec2)
    CHECK(kProbeHandlerBound[1]);     // OnLean(float)
    CHECK(kProbeHandlerBound[2]);     // OnJump()
    CHECK_FALSE(kProbeHandlerBound[3]);   // private OnQuit()
}

TEST_CASE("a key press calls the script's OnJump, once, on the press") {
    PlayerSandbox box("routejump");
    ProbeRig      rig = MakeProbeRig(box);

    // Frame one resolves; nothing is held, so nothing is raised.
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->jumps == 0);

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    // ONCE, and this is the regression the whole file's header is about.
    // Started and Performed arrive in the same breath, so a Route that sent
    // on both called every handler exactly twice.
    CHECK(rig.probe->jumps == 1);

    // Holding it raises nothing further - a Button has only two values, so
    // there is no "still going" event to send.
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->jumps == 1);

    // AND NO SECOND CALL ON THE RELEASE. Unity does not send one either; a
    // script that wants the release polls IsDown or uses a Value action.
    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->jumps == 1);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a Value action hands the script its direction, and the final zero") {
    PlayerSandbox box("routemove");
    ProbeRig      rig = MakeProbeRig(box);

    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->moves == 0);

    Feed({Key("W", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->moves == 1);
    CHECK(rig.probe->lastMove.y == doctest::Approx(1.0f));
    CHECK(rig.probe->lastMove.x == doctest::Approx(0.0f));

    // A change of direction is another call, with the new value.
    Feed({Key("W", false), Key("D", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->moves == 2);
    CHECK(rig.probe->lastMove.x == doctest::Approx(1.0f));
    CHECK(rig.probe->lastMove.y == doctest::Approx(0.0f));

    // THE ONE THAT MATTERS. Letting go sends Canceled, and a Value action
    // DOES send on Canceled - so the script that started walking is told to
    // stop. Without it, it walks into a wall forever.
    Feed({Key("D", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->moves == 3);
    CHECK(rig.probe->lastMove.x == doctest::Approx(0.0f));
    CHECK(rig.probe->lastMove.y == doctest::Approx(0.0f));

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a one-number action is handed a float rather than a vector") {
    PlayerSandbox box("routelean");
    ProbeRig      rig = MakeProbeRig(box);

    eng::PlayerInputSystem::FrameDispatch();

    // THE MIDDLE ARM OF THE LADDER. eng::Vec2 has no constructor taking one
    // float and no conversion to one, so a handler wanting a float cannot be
    // fed a vector and vice versa - the three shapes are mutually exclusive
    // rather than merely ordered.
    Feed({Key("E", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->leans == 1);
    CHECK(rig.probe->lastLean == doctest::Approx(1.0f));

    Feed({Key("E", false), Key("Q", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->leans == 2);
    CHECK(rig.probe->lastLean == doctest::Approx(-1.0f));

    // And the Move handler was never confused for this one.
    CHECK(rig.probe->moves == 0);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("an action whose handler cannot be called is simply not called") {
    PlayerSandbox box("routeunbound");
    ProbeRig      rig = MakeProbeRig(box);

    eng::PlayerInputSystem::FrameDispatch();

    // Quit is bound to X and OnQuit is private, so the scanner offered a
    // candidate the compiler cannot reach. Pressing it must do nothing at
    // all - not call it, not fall through to another handler, not fail.
    Feed({Key("X", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(rig.probe->Quits() == 0);
    CHECK(rig.probe->jumps == 0);
    CHECK(rig.probe->moves == 0);
    CHECK(rig.probe->leans == 0);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("SendMessages stops at the entity, and BroadcastMessages does not") {
    PlayerSandbox box("routebroadcast");

    InstallProbeHandlers();
    box.Write("input/probe.oinput", kProbeActions);

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    std::string error;
    REQUIRE_MESSAGE(input->SetActions("input/probe.oinput", error), error);

    eng::Entity* parent = input->Owner();
    REQUIRE(parent != nullptr);

    // THE SCRIPT IS ON THE CHILD AND THE PLAYER INPUT IS ON THE PARENT,
    // which is the arrangement the two modes differ about - and the ordinary
    // shape for a player rig whose legs and gun are separate objects.
    eng::Entity* child = box.scene.Get(box.scene.CreateEntityUniquelyNamed("Legs"));
    REQUIRE(child != nullptr);
    child->Transform().SetParent(&parent->Transform());
    ActionProbe* legs = AttachProbe(*child);
    REQUIRE(legs != nullptr);
    REQUIRE(parent->ChildCount() == 1);

    eng::PlayerInputSystem::FrameDispatch();

    // SendMessages is the default, and it means THIS ENTITY. The child hears
    // nothing, which is Unity's behaviour and the reason the other mode
    // exists.
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(legs->jumps == 0);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();

    // BroadcastMessages includes the object itself AND everything under it.
    // Unity's BroadcastMessage does too, which "broadcasts down the
    // hierarchy" does not obviously say.
    input->SetNotifications(eng::InputNotification::BroadcastMessages);

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(legs->jumps == 1);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("Callbacks sends no messages at all, and subscribers still hear") {
    PlayerSandbox box("routecallbacks");
    ProbeRig      rig = MakeProbeRig(box);

    // Unity's "Invoke C# Events": the events and NOTHING else. Subscribers
    // have already been told by the time Route runs, so there is simply
    // nothing left for it to do.
    rig.input->SetNotifications(eng::InputNotification::Callbacks);

    int heard = 0;
    rig.input->Subscribe([&heard](const eng::ActionEvent&) { ++heard; });

    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(rig.probe->jumps == 0);   // no message
    CHECK(heard > 0);               // but the subscription is untouched

    eng::InputActions::ReleaseAll();
}

TEST_CASE("the behaviour the Inspector sets is the behaviour that comes back") {
    // THE FIELD IS OLD AND THE WAY TO SET IT IS NEW, which is exactly the
    // shape that goes untested: Deserialize has read "behaviour" and
    // Serialize has written it since this system landed, but nothing in the
    // editor could produce a non-default value, so no round trip ever
    // carried one. The Inspector's Behaviour row is that producer, and this
    // is the case it makes reachable - see the note in InspectorPanel's
    // DrawPlayerInput.
    PlayerSandbox box("behaviourroundtrip");

    eng::InputNotification mode     = eng::InputNotification::SendMessages;
    const char*            expected = nullptr;   // nullptr = no key is written

    SUBCASE("the default writes no key at all") {
        mode     = eng::InputNotification::SendMessages;
        expected = nullptr;
    }
    SUBCASE("BroadcastMessages survives the trip") {
        mode     = eng::InputNotification::BroadcastMessages;
        expected = "BroadcastMessages";
    }
    SUBCASE("Callbacks survives the trip") {
        mode     = eng::InputNotification::Callbacks;
        expected = "Callbacks";
    }

    CAPTURE(eng::ToString(mode));

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    input->SetNotifications(mode);
    CHECK(input->Notifications() == mode);

    // THE DEFAULT MUST STAY SILENT. Every scene file in the project would
    // otherwise grow a key the first time it was saved after this field
    // existed, which is a diff nobody asked for.
    eng::Json written;
    REQUIRE(input->Serialize(written));
    if (expected == nullptr) {
        CHECK_FALSE(written.contains("behaviour"));
    } else {
        CHECK(written["behaviour"] == expected);
    }

    // WHAT UNDO, PLAY-MODE RESTORE AND DUPLICATE ALL DO.
    std::string text, error;
    REQUIRE(box.scene.SaveToString(text, error));
    REQUIRE(box.scene.LoadFromString(text, error));

    eng::Entity* entity = box.scene.FindEntity("Player");
    REQUIRE(entity != nullptr);
    eng::PlayerInputComponent* restored = entity->Find<eng::PlayerInputComponent>();
    REQUIRE(restored != nullptr);
    CHECK(restored->Notifications() == mode);
}

TEST_CASE("an unknown behaviour is reported rather than refused") {
    PlayerSandbox box("behaviourunknown");

    auto component =
        eng::ComponentFactory::Create(eng::PlayerInputComponent::kTypeName);
    REQUIRE(component != nullptr);

    eng::Json node;
    node["actions"]   = "input/player.oinput";
    // UNITY'S FOURTH OPTION, which this engine deliberately has not got -
    // "Invoke Unity Events" is a per-action method picked from a dropdown,
    // and reproducing it needs run-time reflection over method names. It is
    // therefore the likeliest thing somebody types into this key by hand.
    node["behaviour"] = "InvokeUnityEvents";

    std::string error;
    CHECK_FALSE(component->Deserialize(node, error));

    // THE MESSAGE NAMES BOTH HALVES: what was written, and what would have
    // worked. Either one on its own sends somebody to the source.
    CHECK(error.find("InvokeUnityEvents") != std::string::npos);
    CHECK(error.find("SendMessages") != std::string::npos);
    CHECK(error.find("BroadcastMessages") != std::string::npos);
    CHECK(error.find("Callbacks") != std::string::npos);

    // AND IT FELL BACK TO THE DEFAULT rather than being left in some fourth
    // state the switch in Route has no case for.
    auto* input = static_cast<eng::PlayerInputComponent*>(component.get());
    CHECK(input->Notifications() == eng::InputNotification::SendMessages);
}

TEST_CASE("a subscriber sees every phase, and ActionSendsMessage is the filter") {
    // ======================================================================
    //  THE SHAPE A SCRIPT USES WHEN THE METHOD CANNOT BE CALLED OnJump.
    //
    //  Unity's "Invoke C# Events": one callback sees everything and tells the
    //  events apart itself. That is exactly where the double call waits -
    //  Started and Performed arrive in the same breath, because this engine
    //  has no interactions to put a delay between them, so a subscriber that
    //  filters on the action NAME alone runs its body twice for one press.
    //
    //  eng::ActionSendsMessage is the engine's own answer to "does this one
    //  count", and is the rule the generated OnJump() dispatch uses. A
    //  subscriber that applies it fires exactly when a handler would.
    // ======================================================================
    PlayerSandbox box("subscribefilter");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    int everyPhase = 0;
    int filtered   = 0;

    input->Subscribe([&everyPhase](const eng::ActionEvent& event) {
        if (event.action == "Jump") {
            ++everyPhase;
        }
    });
    input->Subscribe([&filtered](const eng::ActionEvent& event) {
        if (event.action == "Jump" && eng::ActionSendsMessage(event)) {
            ++filtered;
        }
    });

    eng::PlayerInputSystem::FrameDispatch();   // settle before the press

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    // THE TRAP: one press, more than one event through the raw subscription.
    CHECK(everyPhase > 1);

    // THE FIX: exactly one, which is what a Button means.
    CHECK(filtered == 1);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();

    // A BUTTON SENDS NOTHING ON THE RELEASE, so the filtered count holds
    // while the raw one has seen the Canceled as well.
    CHECK(filtered == 1);
    CHECK(everyPhase > 2);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("unsubscribing stops the callback, which is what OnDestroy is for") {
    // A SUBSCRIPTION OUTLIVES THE SCRIPT THAT MADE IT. The lambda captures
    // the behaviour, and a script rebuild or a scene change destroys that
    // object while the component can live on - so a script that subscribes
    // has to take it back, or the next event calls into freed memory.
    PlayerSandbox box("unsubscribe");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    int calls = 0;
    const eng::PlayerInputComponent::Token token =
        input->Subscribe([&calls](const eng::ActionEvent& event) {
            if (event.action == "Jump" && eng::ActionSendsMessage(event)) {
                ++calls;
            }
        });
    CHECK(token != 0);   // 0 is the "never subscribed" token

    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();

    input->Unsubscribe(token);

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);   // unchanged: the callback is gone

    eng::InputActions::ReleaseAll();
}

TEST_CASE("Subscribe names an action and a method - no lambda, no token") {
    // ======================================================================
    //  THE PER-ACTION SUBSCRIPTION, which is what people reach for when they
    //  first meet the collective one above.
    //
    //      input->Subscribe("Fire", this, &Gun::FireGun);
    //
    //  THREE THINGS ARE BEING CHECKED AT ONCE and each one is a thing the
    //  hand-written lambda above had to get right for itself:
    //
    //    THE NAME FILTERS. Pressing Jump does not reach the method that
    //    asked for Move, which is the whole of what "subscribed to Fire"
    //    means to the person writing it.
    //
    //    THE PHASE FILTERS. One press is ONE call. Started arrives in the
    //    same breath as Performed, so the raw subscription sees both - this
    //    one applies ActionSendsMessage, exactly as the OnJump() dispatch
    //    does, so it cannot be got wrong per subscriber.
    //
    //    THE PARAMETER IS CHOSEN BY THE METHOD'S OWN SHAPE. Aim takes a
    //    Vec2 and is handed the vector; Tilt takes a float and is handed the
    //    value; Fire takes nothing and is simply called; Raw takes the whole
    //    event. The same ladder, and the same mutual exclusivity, as the
    //    generated handler dispatch.
    // ======================================================================
    PlayerSandbox box("submethod");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);
    rig.input->Subscribe("Move", rig.probe, &ActionProbe::Aim);
    rig.input->Subscribe("Lean", rig.probe, &ActionProbe::Tilt);
    rig.input->Subscribe("Quit", rig.probe, &ActionProbe::Raw);
    CHECK(rig.input->BindingCount() == 4);

    eng::PlayerInputSystem::FrameDispatch();   // settle before the first press
    CHECK(rig.probe->fires == 0);

    // ---- a Button: once, on the press, and nothing on the release -------
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);
    CHECK(rig.probe->aims == 0);     // the other three heard nothing
    CHECK(rig.probe->tilts == 0);
    CHECK(rig.probe->raws == 0);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);

    // ---- a Vector2: the vector, and the final zero ----------------------
    Feed({Key("W", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->aims == 1);
    CHECK(rig.probe->lastAim.y == doctest::Approx(1.0f));
    CHECK(rig.probe->lastAim.x == doctest::Approx(0.0f));
    CHECK(rig.probe->fires == 1);    // and Jump was not disturbed

    Feed({Key("W", false)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->aims == 2);
    CHECK(rig.probe->lastAim.y == doctest::Approx(0.0f));

    // ---- an Axis: one number, never the vector --------------------------
    Feed({Key("E", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->tilts == 1);
    CHECK(rig.probe->lastTilt == doctest::Approx(1.0f));
    CHECK(rig.probe->aims == 2);     // Move was not fed the Lean event

    // ---- the whole event, for a method that wants the phase -------------
    Feed({Key("E", false), Key("X", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->raws == 1);
    CHECK(rig.probe->lastRawPhase == eng::ActionPhase::Performed);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("the same method may serve two actions, which is why it has a name") {
    // The case the On- spelling cannot express at all: OnFire and OnAltFire
    // are two methods by construction. A subscription is a call, so one
    // method may answer to as many names as it likes.
    PlayerSandbox box("subshared");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);
    rig.input->Subscribe("Quit", rig.probe, &ActionProbe::Fire);
    CHECK(rig.input->BindingCount() == 2);

    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);

    Feed({Key("X", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 2);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("Unsubscribe(this) takes back everything that object asked for") {
    // ONE CALL AND NO TOKENS. The component remembers the subscription
    // against the owner, so the owner is the only thing the caller has to
    // still be holding - and it is holding itself.
    PlayerSandbox box("subunsub");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);
    rig.input->Subscribe("Move", rig.probe, &ActionProbe::Aim);
    CHECK(rig.input->BindingCount() == 2);

    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);

    rig.input->Unsubscribe(rig.probe);
    CHECK(rig.input->BindingCount() == 0);

    // BOTH went, not just the one for the action that had fired.
    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true), Key("W", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);
    CHECK(rig.probe->aims == 0);

    // IDEMPOTENT. A script that unsubscribes in OnDestroy and is then
    // destroyed has the destructor do it a second time.
    rig.input->Unsubscribe(rig.probe);
    CHECK(rig.input->BindingCount() == 0);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("destroying the behaviour takes its subscriptions with it") {
    // ======================================================================
    //  THE CASE THAT MAKES THE API SAFE TO USE WITHOUT AN OnDestroy, and the
    //  reason ~ScriptBehaviour has a body at all.
    //
    //  SetScriptName destroys the behaviour and builds another one WHILE THE
    //  PLAYER INPUT COMPONENT BESIDE IT LIVES ON. That is not a contrived
    //  arrangement - it is what a script rebuild, a rename and "Remove
    //  Component" all do. A subscription holding the old `this` would be
    //  called on the next press, through freed storage.
    //
    //  WITHOUT THE PURGE THIS CASE DOES NOT FAIL CLEANLY, which is worth
    //  knowing when reading it: it reads a destroyed object, so it either
    //  passes by luck or crashes the runner. BindingCount is checked first
    //  precisely so that the failure is an assertion rather than a fault.
    // ======================================================================
    PlayerSandbox box("subdeath");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);
    rig.input->Subscribe("Move", rig.probe, &ActionProbe::Aim);
    CHECK(rig.input->BindingCount() == 2);

    eng::PlayerInputSystem::FrameDispatch();

    auto* script = rig.entity->Find<eng::ScriptComponent>();
    REQUIRE(script != nullptr);
    REQUIRE(script->Behaviour() == rig.probe);

    script->SetScriptName("");            // the behaviour is destroyed here
    CHECK(script->Behaviour() == nullptr);
    CHECK(rig.input->BindingCount() == 0);

    // AND THE PRESS AFTERWARDS REACHES NOBODY rather than reaching a corpse.
    Feed({Key("Space", true), Key("W", true)});
    eng::PlayerInputSystem::FrameDispatch();

    eng::InputActions::ReleaseAll();
}

TEST_CASE("Callbacks silences the messages and leaves the subscriptions") {
    // THE MODE THIS API MAKES USABLE. Callbacks is per COMPONENT, so a script
    // that switches to it stops OnJump() being called on every script on the
    // entity - including its own. A per-action subscription is not a message,
    // so it keeps running, which is what lets one script opt out of the
    // automatic dispatch without disarming itself.
    PlayerSandbox box("subcallbacks");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);
    rig.input->SetNotifications(eng::InputNotification::Callbacks);

    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    CHECK(rig.probe->fires == 1);    // the subscription still runs
    CHECK(rig.probe->jumps == 0);    // and OnJump() does not

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a subscribed method may unsubscribe itself mid-dispatch") {
    // A ONE-SHOT, which is the obvious thing to write and the obvious way to
    // destroy the std::function whose body you are standing in. Removal is
    // deferred while the walk is up, so this is safe rather than lucky.
    PlayerSandbox box("subselfremove");
    ProbeRig      rig = MakeProbeRig(box);

    int calls = 0;
    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);

    struct OneShot {
        eng::PlayerInputComponent* input = nullptr;
        int*                       calls = nullptr;
        void Fire() {
            ++*calls;
            input->Unsubscribe(this);
        }
    };
    OneShot shot{rig.input, &calls};
    rig.input->Subscribe("Jump", &shot, &OneShot::Fire);
    CHECK(rig.input->BindingCount() == 2);

    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);
    CHECK(rig.probe->fires == 1);        // its neighbour was still called
    CHECK(rig.input->BindingCount() == 1);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);                   // and it is gone for good
    CHECK(rig.probe->fires == 2);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a subscribed method that removes the player stops that dispatch") {
    // THE SAME HAZARD ActionMapState::Deliver answers, one level up. A
    // subscribed method is script code, and RemoveComponent is immediate - so
    // the walk over the bindings is standing in a component that has just
    // been destroyed. DeliverBindings answers whether it survived, exactly as
    // Deliver does, and Route stops when it says no.
    PlayerSandbox box("subsuicide");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    eng::Entity* entity = input->Owner();
    REQUIRE(entity != nullptr);

    int after = 0;

    struct Remover {
        eng::Entity*               entity = nullptr;
        eng::PlayerInputComponent* input  = nullptr;
        void Fire() { entity->RemoveComponent(eng::PlayerInputComponent::kTypeName); }
    };
    Remover remover{entity, input};

    struct Counter {
        int* seen = nullptr;
        void Fire() { ++*seen; }
    };
    Counter counter{&after};

    input->Subscribe("Jump", &remover, &Remover::Fire);
    input->Subscribe("Jump", &counter, &Counter::Fire);

    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    // The component is gone, and the binding behind the one that killed it
    // was never called - which is the right answer rather than a compromise:
    // the thing the event was about has ceased to exist.
    CHECK(entity->Find<eng::PlayerInputComponent>() == nullptr);
    CHECK(after == 0);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a subscription is keyed on the name, so a reload cannot dangle it") {
    // WHY THE API TAKES A STRING AND NOT AN InputAction*. Reload re-fills the
    // asset's map vector in place, so every ActionMap* and InputAction* handed
    // out before it dies - InputActionAsset.h says so at length. A name
    // survives that, and so does a subscription keyed on one.
    PlayerSandbox box("subreload");
    ProbeRig      rig = MakeProbeRig(box);

    rig.input->Subscribe("Jump", rig.probe, &ActionProbe::Fire);

    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 1);
    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();

    // What the Input Actions window does on Save.
    std::string reloadError;
    REQUIRE_MESSAGE(eng::InputActionLibrary::Reload("input/probe.oinput", reloadError), reloadError);
    eng::PlayerInputSystem::ReloadAll("input/probe.oinput");
    CHECK(rig.input->BindingCount() == 1);

    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(rig.probe->fires == 2);

    eng::InputActions::ReleaseAll();
}

TEST_CASE("a const `this`, and a script with a second base, still get withdrawn") {
    // ======================================================================
    //  WHAT OwnerKey IS FOR, AND THE ONLY ARRANGEMENT THAT CAN TELL.
    //
    //  ~ScriptBehaviour reports a death with the only address it has: the
    //  BASE one. A script subscribes with the DERIVED one. Under single
    //  inheritance those are the same address, so a normalisation that
    //  quietly stopped happening would go unnoticed by every case above -
    //  and the binding would outlive the object only for somebody whose
    //  script has a second base.
    //
    //  THE CONST IS THE OTHER HALF. `Obj` is deduced from the argument, so
    //  `this` inside a const method deduces `const T`; a convertibility test
    //  written against a non-const `ScriptBehaviour*` answers NO for that,
    //  because the conversion would drop const, and the raw derived address
    //  is stored instead. Both halves are exercised here at once.
    // ======================================================================
    PlayerSandbox box("subconstbase");

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);

    // A POLYMORPHIC FIRST BASE, deliberately: it is what stops the compiler
    // putting the polymorphic ScriptBehaviour at offset 0 and handing this
    // case the very coincidence it exists to rule out.
    struct Padding {
        virtual ~Padding() = default;
        double a = 0.0;
        double b = 0.0;
    };

    struct Listener final : Padding, eng::ScriptBehaviour {
        eng::PlayerInputComponent* player = nullptr;
        int*                       calls  = nullptr;

        // CONST, and so is the method it subscribes.
        void Arm() const { player->Subscribe("Jump", this, &Listener::Fire); }
        void Fire() const { ++*calls; }
    };

    int  calls = 0;
    auto listener    = std::make_unique<Listener>();
    listener->player = input;
    listener->calls  = &calls;

    // THE TWO ADDRESSES REALLY DO DIFFER, which is the premise of the case.
    const void* derived = static_cast<const void*>(listener.get());
    const void* base =
        static_cast<const void*>(static_cast<const eng::ScriptBehaviour*>(listener.get()));
    REQUIRE(derived != base);

    listener->Arm();
    CHECK(input->BindingCount() == 1);

    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);

    // WHAT ScriptComponent DOES ON A REBUILD: destroyed through the base,
    // while this component lives on.
    std::unique_ptr<eng::ScriptBehaviour> owned(listener.release());
    owned.reset();
    CHECK(input->BindingCount() == 0);

    Feed({Key("Space", false)});
    eng::PlayerInputSystem::FrameDispatch();
    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();
    CHECK(calls == 1);   // and nothing reached the corpse

    eng::InputActions::ReleaseAll();
}

// ===========================================================================
//  A HANDLER THAT REMOVES A LATER SCRIPT ON THE SAME ENTITY.
//
//  The cases above are about the PLAYER list: a handler that removes a
//  PlayerInputComponent, and the tombstone that keeps the walk over
//  g_players honest. This one is about the OTHER list Route holds while
//  script code runs - the ScriptComponents of one entity.
//
//  THE TWO ARE NOT THE SAME HAZARD. g_players is a vector of pointers, so an
//  erase moves its SURVIVORS and the symptom is a player silently skipped.
//  An entity's components live behind unique_ptr, so an erase moves nobody -
//  it DESTROYS the one that went, and a snapshot taken before the handler ran
//  is left holding that one freed pointer. The next turn of the loop then
//  reads Behaviour() and ScriptName() out of freed storage and hands the
//  result to a script DLL by reference.
//
//  WHY A LATER SIBLING AND NOT THE FIRST MATCH. RemoveComponent(name) removes
//  the first ScriptComponent, which is usually the one being dispatched - and
//  that one's slot has already been read, so nothing reads it again.
//  Reproducing it needs the removed component to be one the walk has NOT
//  reached yet, which is what RemoveComponentAt on a later index gives.
// ===========================================================================

namespace {

// The removal the first script performs, as data rather than a captured
// lambda: a script is reached through the registry's dispatcher, which hands
// the behaviour nothing of the test's own.
eng::Entity* g_removeFrom  = nullptr;
std::size_t  g_removeIndex = 0;
bool         g_removeDone  = false;

// Set by the SECOND script's handler. A GLOBAL and not a member on purpose:
// after the removal that object is freed, so the only evidence it can leave
// that does not itself read destroyed storage is a flag somewhere else.
bool g_laterScriptHeard = false;

// The first script on the entity. Its OnJump destroys a LATER sibling.
class RemoverProbe final : public eng::ScriptBehaviour {
public:
    int jumps = 0;

    void OnStart() {}

    void OnJump() {
        ++jumps;
        if (g_removeFrom != nullptr && !g_removeDone) {
            g_removeDone = true;
            g_removeFrom->RemoveComponentAt(g_removeIndex);
        }
    }
};

// The later script - the one that is destroyed mid-walk.
class SiblingProbe final : public eng::ScriptBehaviour {
public:
    void OnStart() {}

    void OnJump() { g_laterScriptHeard = true; }
};

} // namespace

// At file scope, after the namespace closes - the same idiom ActionProbe uses.
ENGINE_REGISTER_SCRIPT(RemoverProbe)
ENGINE_REGISTER_SCRIPT(SiblingProbe)

namespace {

constexpr const char* kOneJumpHandler[] = {"OnJump"};

constexpr bool kRemoverHandlerBound[]  = {ENGINE_ACTION_HANDLER_BINDS(RemoverProbe, OnJump)};
constexpr bool kSiblingHandlerBound[]  = {ENGINE_ACTION_HANDLER_BINDS(SiblingProbe, OnJump)};

template <class T>
void DispatchOneJump(eng::ScriptBehaviour& behaviour, std::size_t index,
                     const eng::ActionEvent& event) {
    T& self = static_cast<T&>(behaviour);
    (void)self;
    (void)event;
    switch (index) {
        case 0: ENGINE_CALL_ACTION_HANDLER(self, OnJump, event); break;
        default: break;
    }
}

// Attaches a named script and hands back its component, in the shape
// AttachProbe above uses.
eng::ScriptComponent* AttachNamedScript(eng::Entity& entity, const char* name) {
    eng::Json node = eng::Json::object();
    node["script"] = name;

    auto component = eng::ComponentFactory::Create(eng::ScriptComponent::kTypeName);
    REQUIRE(component != nullptr);
    std::string error;
    REQUIRE(component->Deserialize(node, error));

    auto* script =
        static_cast<eng::ScriptComponent*>(entity.AddComponent(std::move(component)));
    REQUIRE(script != nullptr);
    REQUIRE_MESSAGE(script->IsResolved(), "that script is not in this build");
    return script;
}

// Where a component sits in its entity's list, which is what
// RemoveComponentAt takes.
std::size_t IndexOf(eng::Entity& entity, const eng::Component* wanted) {
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        if (entity.ComponentAt(i) == wanted) {
            return i;
        }
    }
    return entity.ComponentCount();
}

std::size_t ScriptCount(eng::Entity& entity) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        const eng::Component* component = entity.ComponentAt(i);
        if (component != nullptr &&
            std::string_view(component->TypeName()) == eng::ScriptComponent::kTypeName) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("a handler that removes a later script on the same entity is not read after") {
    PlayerSandbox box("routeremovesibling");

    g_removeFrom       = nullptr;
    g_removeIndex      = 0;
    g_removeDone       = false;
    g_laterScriptHeard = false;

    REQUIRE(eng::ScriptRegistry::SetActionHandlers(
        "RemoverProbe", kOneJumpHandler, kRemoverHandlerBound, 1,
        &DispatchOneJump<RemoverProbe>));
    REQUIRE(eng::ScriptRegistry::SetActionHandlers(
        "SiblingProbe", kOneJumpHandler, kSiblingHandlerBound, 1,
        &DispatchOneJump<SiblingProbe>));

    box.Write("input/probe.oinput", kProbeActions);

    eng::PlayerInputComponent* input = box.MakePlayer("Gameplay");
    REQUIRE(input != nullptr);
    std::string error;
    REQUIRE_MESSAGE(input->SetActions("input/probe.oinput", error), error);

    eng::Entity* entity = input->Owner();
    REQUIRE(entity != nullptr);

    // ORDER IS THE WHOLE TEST. The remover goes on first, so the walk is
    // standing on it when the sibling behind it is destroyed.
    eng::ScriptComponent* remover = AttachNamedScript(*entity, "RemoverProbe");
    eng::ScriptComponent* sibling = AttachNamedScript(*entity, "SiblingProbe");
    REQUIRE(remover != nullptr);
    REQUIRE(sibling != nullptr);
    REQUIRE(ScriptCount(*entity) == 2);

    const std::size_t removerIndex = IndexOf(*entity, remover);
    const std::size_t siblingIndex = IndexOf(*entity, sibling);
    REQUIRE(removerIndex < entity->ComponentCount());
    REQUIRE(siblingIndex < entity->ComponentCount());
    REQUIRE(removerIndex < siblingIndex);

    g_removeFrom  = entity;
    g_removeIndex = siblingIndex;

    // Frame one resolves the asset and raises nothing; see the cases above.
    eng::PlayerInputSystem::FrameDispatch();

    Feed({Key("Space", true)});
    eng::PlayerInputSystem::FrameDispatch();

    // The removal happened, and from inside the dispatch.
    CHECK(g_removeDone);
    CHECK(ScriptCount(*entity) == 1);

    // THE WHOLE POINT. Before the fix, Route held a snapshot of
    // ScriptComponent* taken before any handler ran, so the entry behind the
    // remover was a freed pointer by the time the loop reached it - read for
    // its Behaviour and its ScriptName, and then dispatched into.
    //
    // WHERE THAT SHOWS, measured rather than assumed. A DEBUG build fails
    // this case outright: the freed block is scrubbed, ScriptName() hands
    // back a std::string whose length is now garbage, and the case ends as
    //
    //     ERROR: test case THREW exception: string too long
    //
    // before any CHECK below is reached. A RELEASE build does NOT, and for
    // a reason worth writing down rather than leaving as luck: the freed
    // block is not scrubbed, but ~ScriptComponent has already nulled the
    // unique_ptr holding the behaviour, so Behaviour() reads back null out
    // of the dead object and Route's own null check skips it. The read is
    // still a read of freed storage - it is simply one that happens to see
    // a zero. So this case is a genuine regression test in Debug and a
    // no-regression check in Release, which is the usual shape for a
    // use-after-free without a sanitizer.
    //
    // The flag is what a release build WOULD catch if that block were ever
    // reused between the erase and this loop, and it costs nothing to keep.
    CHECK_FALSE(g_laterScriptHeard);

    // And the script that did the removing was dispatched exactly once.
    CHECK(static_cast<RemoverProbe*>(remover->Behaviour())->jumps == 1);

    g_removeFrom = nullptr;
    eng::InputActions::ReleaseAll();
}
