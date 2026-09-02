# =============================================================================
#  The sandbox executable - the game, running on its own without the editor.
#
#  This is the only target allowed to look like a game. Everything in here uses
#  the engine's public headers and nothing else; it cannot reach SDL, because
#  the engine links SDL privately.
#
#  That constraint is the point. If a game can be written entirely against the
#  engine's public interface, the interface is complete.
# =============================================================================

# Globbed for the same reason the engine is - see engine/CMakeLists.txt. Adding
# a file to the game never needs an edit here.
file(GLOB_RECURSE SANDBOX_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE SANDBOX_HEADERS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(sandbox ${SANDBOX_SOURCES} ${SANDBOX_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SANDBOX_SOURCES} ${SANDBOX_HEADERS})

target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The engine, and nothing else.
#
# The project's scripts are deliberately NOT linked here. They are LOADED at
# run time from .build/, exactly as the editor loads them - see
# engine/include/engine/scene/ScriptLibrary.h. One mechanism, used by both
# programs, so a script cannot work in one and not the other.
target_link_libraries(sandbox PRIVATE Engine2D::engine)


engine_set_warnings(sandbox)

# On Windows, keep a console window open alongside the game so the log is
# visible while developing.
if(WIN32)
    set_target_properties(sandbox PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
