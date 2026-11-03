// ============================================================================
//  test_main.cpp - the one file that gives the test program its main().
//
//  Defining DOCTEST_CONFIG_IMPLEMENT before including doctest asks it for
//  everything EXCEPT main(), which is written below. It must be done in
//  exactly one file, which is why this file contains nothing else. Every other
//  test file just includes <doctest/doctest.h>.
//
//  ==========================================================================
//  WHY main() IS WRITTEN OUT RATHER THAN GENERATED
//
//  Because the component factory has to be told which type names mean which
//  classes BEFORE any test runs, and there is nowhere else to do it once.
//
//  Engine::Init does it for the editor and the player - see the block at the
//  top of Engine::Init, which this mirrors line for line. The test binary
//  never calls Engine::Init: there is no window and no renderer here, which is
//  the whole point of it. So until this existed, the factory was empty when
//  the first test ran and filled in only as a side effect of whichever test
//  fixture happened to register something.
//
//  WHAT THAT COST, and it was found by accident:
//
//    * `entity->Add<SpriteComponent>()` returned NULLPTR, so a fixture that
//      dereferenced it segfaulted - but only when its file was run on its own,
//      because in a whole-suite run some earlier file had already registered
//      the builtins.
//    * three tests in test_scriptfields.cpp and one in test_scriptapi.cpp
//      failed the same way for the same reason, and two in test_snapshot.cpp
//      restored a scene without the components it could not create.
//    * `tests.exe --order-by=rand` failed on some seeds and passed on others,
//      which is the clearest possible statement that the suite was not
//      testing what it claimed.
//
//  A test that passes only because of what ran before it is not a test of
//  anything. Registering here makes every case order-independent, and
//  `--order-by=rand` is now a thing worth running.
//
//  Fixtures that call RegisterComponentTypes themselves are left alone and
//  cost nothing: ComponentFactory::Register overwrites its map entry, so
//  registering a type twice is the same as registering it once.
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

// One include rather than nine. Engine.h is what a script includes too, and it
// reaches every system whose component types are registered below.
#include <engine/Engine.h>

int main(int argc, char** argv) {
    // THE SAME LIST, IN THE SAME ORDER, as the top of Engine::Init. If a
    // component type is added there it belongs here as well, or it will work
    // in the editor and be missing from every test that builds one by name.
    eng::ComponentFactory::RegisterBuiltins();
    eng::CollisionSystem::RegisterComponentTypes();
    eng::RigidbodySystem::RegisterComponentTypes();
    eng::TilemapRenderSystem::RegisterComponentTypes();
    eng::SpinSystem::RegisterComponentTypes();
    eng::SpriteAnimationSystem::RegisterComponentTypes();
    eng::ScriptSystem::RegisterComponentTypes();
    eng::UISystem::RegisterComponentTypes();

    // The camera is a component like any other - see CameraComponent.h - and
    // Engine::Init registers it by hand for the same reason.
    eng::ComponentFactory::Register(
        eng::CameraComponent::kTypeName,
        []() -> std::unique_ptr<eng::Component> {
            return std::make_unique<eng::CameraComponent>();
        });

    doctest::Context context(argc, argv);
    return context.run();
}
