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

add_executable(sandbox
    src/main.cpp
    src/CollectorGame.cpp)     # the small sample game

target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# WHOLE_ARCHIVE on gamescripts, and it is not optional.
#
# A script registers itself using an object at file scope that nothing else
# refers to by name (see ENGINE_REGISTER_SCRIPT in ScriptComponent.h). A normal
# static-library link throws away anything that appears unused, which would
# silently discard every script - and the only symptom would be that nothing
# happens when you press Play. WHOLE_ARCHIVE tells the linker to keep the lot.
target_link_libraries(sandbox PRIVATE
    Engine2D::engine
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,Engine2D::gamescripts>")

engine_set_warnings(sandbox)

# On Windows, keep a console window open alongside the game so the log is
# visible while developing.
if(WIN32)
    set_target_properties(sandbox PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
