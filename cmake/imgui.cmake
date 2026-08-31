# =============================================================================
#  Dear ImGui - the IDE's widget layer.
#
#  ImGui ships NO CMakeLists.txt. FetchContent will download it but will not
#  create a target, so we build one ourselves. This surprises everyone once;
#  now you know.
#
#  TAG: v1.92.9b-docking
#
#  Note the "-docking" suffix and do not remove it. Docking and multi-viewports
#  live on a SEPARATE BRANCH from ImGui's master. Without it there is no
#  dockable layout - no dragging the Hierarchy panel next to the Inspector, no
#  tabs, no saved workspace. You would have a pile of floating windows instead
#  of an IDE.
# =============================================================================

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE)

# Populate only. There is nothing here for CMake to configure.
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp          # keep it - see note below
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)

# SYSTEM so ImGui's own headers do not trip your -Wall -Wextra settings.
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(imgui::imgui ALIAS imgui)

# ---------------------------------------------------------------------------
#  Why imgui_demo.cpp is in that list even though it is "just a demo":
#
#  It is the single best ImGui reference that exists. Call ImGui::ShowDemoWindow()
#  and every widget the library has is on screen, live, with a "show source
#  code" button next to each one. When you need a tree view or a two-column
#  property table for a panel, open the demo, find the thing that looks right,
#  read its source.
#
#  Do this instead of searching the web for ImGui tutorials. Most of them are
#  written against much older versions and will hand you obsoleted functions.
# ---------------------------------------------------------------------------
