# =============================================================================
#  The sandbox executable.
#
#  This is the only target that is allowed to look like a game. Everything in
#  here uses the engine's public headers and nothing else - it cannot reach
#  SDL, because Week 3 made that link PRIVATE.
#
#  This is what the Week 10 gate is built in, and what proves the engine can
#  ship without its tools attached.
# =============================================================================

add_executable(sandbox
    src/main.cpp
    src/LayoutBench.cpp        # Week 4 - AoS vs SoA experiment
    src/PlatformBench.cpp      # Week 5 - the three OS measurements
    src/CollectorGame.cpp)     # Week 10 gate - Spec A

target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# WHOLE_ARCHIVE on gamescripts. Script registrars are file-scope objects that
# nothing references by name, so a normal static-library link discards them and
# every script silently fails to register. See gamescripts/ScriptsAnchor.cpp.
target_link_libraries(sandbox PRIVATE
    Engine2D::engine
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,Engine2D::gamescripts>")

engine_set_warnings(sandbox)

# SDL supplies its own entry point on some platforms (notably Windows, where
# the real entry point is WinMain). On Windows we also want a console during
# development so the log is visible.
if(WIN32)
    set_target_properties(sandbox PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# Week 8 added a post-build copy of config/ next to the executable.
# WEEK 9 DELETED IT. The FileSystem layer resolves paths at runtime now:
# copying works for one directory, does not scale to an asset tree, and
# silently hands you a stale copy the moment you edit the source one.
