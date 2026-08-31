# =============================================================================
#  The editor executable - the development environment.
#
#      engine/   a static library. Knows nothing about any game or any tool.
#      editor/   this program. All the panels live here.
#      sandbox/  the game running on its own, with no editor attached.
#
#  The editor links the engine exactly the way the game does and sees only the
#  engine's public headers. That constraint is worth keeping: every panel makes
#  you ask "can this be observed from outside the engine?", and when the answer
#  is no, the engine gains the missing accessor rather than the panel reaching
#  inside.
# =============================================================================

set(EDITOR_SOURCES
    src/main.cpp
    src/EditorApp.cpp                    # the window, the menu bar, the panel list
    src/AssetDragDrop.cpp                # what happens when you drag a file onto something
    src/ScriptTemplate.cpp               # the starter script the Assets panel writes
    src/panels/AssetBrowserPanel.cpp     # Assets   - the files on disk
    src/panels/ConsolePanel.cpp          # Console  - the log window
    src/panels/GamePanel.cpp             # Game     - what a player would see
    src/panels/HierarchyPanel.cpp        # Hierarchy- every entity in the scene
    src/panels/InspectorPanel.cpp        # Inspector- the selected entity's components
    src/panels/ScenePanel.cpp            # Scene    - the view you edit in
    src/panels/ToolbarPanel.cpp          # Toolbar  - Play, Pause, Step
)

add_executable(editor ${EDITOR_SOURCES})

target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# WHOLE_ARCHIVE - see the explanation in sandbox/CMakeLists.txt. Linking the
# scripts into the EDITOR as well is what lets Play actually run behaviour in
# the Game view.
target_link_libraries(editor PRIVATE
    Engine2D::engine
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,Engine2D::gamescripts>")

engine_set_warnings(editor)

if(WIN32)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
