# =============================================================================
#  The unit test program.
#
#  Tests link against `engine` exactly the way the game does, and can see only
#  the engine's public headers. That is deliberate: if something turns out to
#  be hard to test, it is usually because it is hard to use.
#
#  doctest is the test framework. It is header-only, so there is nothing to
#  install, and it was chosen for how fast it compiles - a test suite that is
#  slow to build is a test suite that stops being run.
#
#  Run them with:  ctest --preset debug
# =============================================================================

set(TEST_SOURCES
    src/test_main.cpp        # tells doctest to generate a main() for us
    src/test_vec2.cpp
    src/test_overlap.cpp
    src/test_transform.cpp
    src/test_random.cpp
    src/test_assets.cpp
    src/test_import.cpp
    src/test_time.cpp
    src/test_physics.cpp
    src/test_tilemap.cpp
    src/test_scriptfields.cpp
    src/test_scriptvars.cpp
    src/test_project.cpp
    src/test_scriptabi.cpp
    src/test_snapshot.cpp
    src/test_prefab.cpp
    src/test_scriptapi.cpp
    src/test_spriteanimation.cpp
    src/test_debug.cpp
    src/test_inputactions.cpp
    src/test_inputactionasset.cpp
    src/test_inputactionlibrary.cpp
    src/test_playerinput.cpp
    src/test_inputdispatch.cpp
    src/test_assetrefs.cpp
    src/test_gameview.cpp
    src/test_persistence.cpp
    src/test_projectsettings.cpp

    # ==========================================================================
    #  ONE EDITOR FILE, COMPILED INTO THE TEST PROGRAM.
    #
    #  AssetRefs REWRITES THE USER'S FILES - every scene, prefab and tile in the
    #  project - to follow an asset that has been renamed. That is the last kind
    #  of code that should be shipped on the strength of "it compiled", and the
    #  editor is a windowed program with no test target of its own.
    #
    #  It is compiled here rather than linked from the editor because the editor
    #  is an executable, not a library. The file depends on nothing but the
    #  engine, which is what makes that possible; anything that reached for
    #  ImGui would not belong in this list.
    # ==========================================================================
    ../editor/src/AssetRefs.cpp

    # And the Game view's layout arithmetic, for the same reason: it decides
    # where a click lands, and it depends on nothing but two numbers and a
    # mode - no ImGui, no engine, no window. See GameViewLayout.h.
    ../editor/src/GameViewLayout.cpp

    # And the settings writer, which is the third one here that rewrites the
    # user's own files - config/engine.json, holding every key binding in the
    # project. See ProjectSettings.h.
    ../editor/src/ProjectSettings.cpp
)

add_executable(tests ${TEST_SOURCES})

target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../editor/src)

target_link_libraries(tests PRIVATE Engine2D::engine doctest::doctest)

engine_set_warnings(tests)

# Registers the whole program as one test as far as CMake is concerned;
# doctest's own output says which individual cases failed.
add_test(NAME unit_tests COMMAND tests)
