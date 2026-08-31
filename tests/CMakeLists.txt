# =============================================================================
#  WEEK 2 - the unit test target.
#
#  Tests link against `engine`, exactly like the sandbox does, and can see
#  only the engine's PUBLIC headers. That is a feature: if something is hard
#  to test, it is usually because it is hard to use.
# =============================================================================

set(TEST_SOURCES
    src/test_main.cpp
    src/test_bytebuffer.cpp        # Week 2, provided
    src/test_memutil.cpp           # Week 2
    src/test_queue.cpp             # Week 5, provided
    src/test_vec2.cpp              # Week 6
    src/test_overlap.cpp           # Week 6, partially provided
    src/test_transform.cpp         # Week 6, partially provided
    src/test_random.cpp            # Week 6
    src/test_allocators.cpp        # Week 7, provided - do not edit
    src/test_stringid.cpp          # Week 8, partially provided
    src/test_handle.cpp            # Week 9, provided
)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE Engine2D::engine doctest::doctest)

engine_set_warnings(tests)

# Registers the whole binary as one CTest test. Good enough all semester;
# doctest reports which individual cases failed in its own output.
add_test(NAME unit_tests COMMAND tests)
