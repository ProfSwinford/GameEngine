# =============================================================================
#  gamescripts - the behaviours the editor creates and the game runs.
#
#  A separate target, linked by BOTH `editor` and `sandbox`, and that is the
#  whole point of it existing. A script compiled only into the sandbox could
#  never run in the editor's Game view, so pressing Play in the IDE would show
#  a scene with nothing happening in it - which is exactly the limitation the
#  editor had before this directory existed.
#
#  ---------------------------------------------------------------------------
#  THIS IS THE ONE PLACE THE PROJECT USES file(GLOB), AND IT IS DELIBERATE.
#
#  The usual advice is right and applies everywhere else in this build: an
#  explicit source list means a new file is a reviewed change rather than
#  something a stale cache decides. Every other target here lists its sources
#  by hand for that reason.
#
#  Scripts are the exception, because the editor CREATES them. A workflow where
#  "New Script" writes a file and then requires hand-editing a CMakeLists
#  before it can possibly compile is a workflow nobody would use twice.
#
#  CONFIGURE_DEPENDS is what makes the glob safe: the generator re-checks this
#  directory on every build and reconfigures when its contents change, so a new
#  script is picked up by the next build with no manual step. It costs one
#  directory stat per build - the "Re-checking globbed directories..." line
#  Ninja prints.
# =============================================================================

file(GLOB GAMESCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# An empty gamescripts/ directory must still produce a linkable target, or deleting
# the last script would break the build of two other targets. A tiny always-
# present source guarantees the library is never empty.
add_library(gamescripts STATIC
    ${GAMESCRIPT_SOURCES}
    ScriptsAnchor.cpp
)

add_library(Engine2D::gamescripts ALIAS gamescripts)

target_include_directories(gamescripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# PUBLIC, not PRIVATE: a script's header includes engine headers, and anything
# that includes a script header needs them too.
target_link_libraries(gamescripts PUBLIC Engine2D::engine)

engine_set_warnings(gamescripts)

list(LENGTH GAMESCRIPT_SOURCES GAMESCRIPT_COUNT)
message(STATUS "gamescripts: ${GAMESCRIPT_COUNT} script(s) in gamescripts/")
