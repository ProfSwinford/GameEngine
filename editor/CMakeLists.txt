# =============================================================================
#  The editor executable - the development environment.
#
#      engine/       a shared library. Knows nothing about any game or any tool.
#      editor/       this program. All the panels live here.
#      sandbox/      the game running on its own, with no editor attached.
#      assets/       the project's scripts, compiled into a library that both
#                    programs LOAD at run time.
#
#  ---------------------------------------------------------------------------
#  NOTE WHAT THE EDITOR NO LONGER LINKS: the project's scripts.
#
#  It loads them instead, from a library it compiles itself while running - see
#  src/ScriptBuild.h. That is what makes this a finished program: somebody
#  using the released editor can write a script and see it run without ever
#  rebuilding the editor, and without needing this source tree at all.
# =============================================================================

set(EDITOR_SOURCES
    src/main.cpp
    src/EditorApp.cpp                    # the window, the menu bar, the panel list
    src/EditorGui.cpp                    # starting and stopping Dear ImGui
    src/ScriptBuild.cpp                  # finds a compiler and builds the project
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

# ---------------------------------------------------------------------------
#  Dear ImGui lives HERE now, not in the engine.
#
#  It keeps some of its state in a global variable, and a global variable does
#  not survive being reached across a shared-library boundary the way a
#  function does. Keeping ImGui inside the one program that draws with it
#  sidesteps that completely. See src/EditorGui.h.
#
#  SDL comes with it, because ImGui's backends are written against SDL. The
#  editor is a TOOL, so that is fine - the boundary that matters is that a
#  GAME cannot reach SDL, and the sandbox still cannot.
# ---------------------------------------------------------------------------
target_link_libraries(editor PRIVATE
    Engine2D::engine
    imgui::imgui
    SDL3::SDL3)

# ---------------------------------------------------------------------------
#  What the editor needs to know in order to compile somebody's scripts.
#
#  These are baked in at build time because they have to MATCH the way the
#  engine itself was built. A script compiled with a different C runtime, or
#  against different headers, would load and then misbehave in ways that are
#  very hard to trace - so the values are taken from this build rather than
#  guessed at run time.
#
#  ENGINE_INCLUDE_DIR and ENGINE_JSON_INCLUDE_DIR are absolute paths into this
#  source tree. A released editor would ship its own copy of those headers
#  beside the executable and look there instead; the lookup order is in
#  ScriptBuild.cpp.
# ---------------------------------------------------------------------------
target_compile_definitions(editor PRIVATE
    ENGINE_SCRIPT_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/engine/include"
    ENGINE_SCRIPT_JSON_INCLUDE_DIR="$<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>"
    ENGINE_SCRIPT_IMPORT_LIB="$<TARGET_LINKER_FILE:engine>"
    ENGINE_SCRIPT_IS_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)

engine_set_warnings(editor)

if(WIN32)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
