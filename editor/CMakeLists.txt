# =============================================================================
#  The editor executable - the IDE.
#
#      engine/   static library   - knows nothing about any game or any tool
#      editor/   executable       - the IDE. Panels live here.
#      sandbox/  executable       - the standalone game runtime.
#
#  The editor links `engine` exactly the way the sandbox does and sees only
#  the engine's PUBLIC headers. That constraint is the point: every panel
#  forces the question "can I observe this subsystem from outside the engine?"
#  and when the answer is no, it surfaces that week rather than at the gate.
#
#  Ch. 1.6.5 is candid about the trade-off - sharing structures between tools
#  and runtime avoids two representations of everything and makes live editing
#  far easier, but when the engine is crashing the tools are down too.
# =============================================================================

set(EDITOR_SOURCES
    src/main.cpp
    src/EditorApp.cpp
    src/AssetDragDrop.cpp                # drag-and-drop payloads and what they do
    src/ScriptTemplate.cpp               # the default script the browser writes
    src/panels/AssetBrowserPanel.cpp     # files on disk
    src/panels/ScenePanel.cpp            # the Scene view - edit the world
    src/panels/GamePanel.cpp             # the Game view  - play the world
    src/panels/EventInspectorPanel.cpp   # Week 2
    src/panels/LogPanel.cpp              # Week 3
    src/panels/ProfilerPanel.cpp         # Week 4
    src/panels/JobPanel.cpp              # Week 5
    src/panels/ViewportPanel.cpp         # Week 6 - viewport + debug draw
    src/panels/MemoryPanel.cpp           # Week 7
    src/panels/CVarPanel.cpp             # Week 8
    src/panels/HierarchyPanel.cpp        # Week 9
    src/panels/ResourcePanel.cpp         # Week 9
    src/panels/InspectorPanel.cpp        # Week 10
    src/panels/ToolbarPanel.cpp          # Week 10
)

add_executable(editor ${EDITOR_SOURCES})

target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# WHOLE_ARCHIVE - see the note in sandbox/CMakeLists.txt. Linking the scripts
# into the EDITOR too is what lets Play run real behaviour in the Game view.
target_link_libraries(editor PRIVATE
    Engine2D::engine
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,Engine2D::gamescripts>")

engine_set_warnings(editor)

if(WIN32)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
