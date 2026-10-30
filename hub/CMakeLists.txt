# =============================================================================
#  hub - the launcher: pick a project, pick an engine, start the editor.
#
#  A FOURTH PROGRAM rather than a screen inside the editor, for one reason
#  that is not negotiable: it chooses WHICH ENGINE to run. An editor is built
#  against one engine.dll and has already loaded it by the time it could
#  offer a choice. See hub/src/main.cpp for the rest of the reasoning.
#
#  It links the engine, but it never calls Engine::Init - it brings up a log,
#  a window and a renderer by hand, because it has NO PROJECT and most of the
#  engine's twelve subsystems exist to run a game out of a project folder.
#  That is also why it needs no FileSystem.
#
#  ImGui and SDL come with it for the same reason the editor gets them: it is
#  a TOOL. The boundary that matters is that a GAME cannot reach SDL, and the
#  player still cannot.
# =============================================================================

add_executable(hub src/main.cpp)

target_link_libraries(hub PRIVATE
    Engine2D::engine
    imgui::imgui
    SDL3::SDL3)

engine_set_warnings(hub)

# Beside the editor and the engine it launches, and that is load-bearing
# rather than tidiness: the Hub finds the engine it shipped with by looking in
# its OWN folder, so that its list of engines is never empty on a first run.
set_target_properties(hub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(WIN32)
    # ---------------------------------------------------------------------
    #  A GUI PROGRAM, with mainCRTStartup.
    #
    #  WIN32_EXECUTABLE stops a console window appearing behind the launcher
    #  every time it is opened. /ENTRY:mainCRTStartup then lets it keep an
    #  ordinary main() rather than WinMain, so the file reads like every
    #  other entry point in this project. The editor does exactly this; see
    #  editor/CMakeLists.txt.
    #
    #  Output that would have gone to that console is not lost: main()
    #  attaches to the parent's console when there is one, which is what
    #  makes running it from a terminal still useful.
    # ---------------------------------------------------------------------
    set_target_properties(hub PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(hub PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
