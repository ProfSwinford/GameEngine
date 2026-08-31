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
)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE Engine2D::engine doctest::doctest)

engine_set_warnings(tests)

# Registers the whole program as one test as far as CMake is concerned;
# doctest's own output says which individual cases failed.
add_test(NAME unit_tests COMMAND tests)
