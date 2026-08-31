# =============================================================================
#  2D Game Engine Architecture - root build
#
#  Grown one week at a time, Weeks 1-10. Three targets plus a test binary:
#
#      engine/   static library   - knows nothing about any game or any tool
#      sandbox/  executable       - the standalone game runtime (the Gate)
#      editor/   executable       - the ImGui IDE, one panel per week
#      tests/    executable       - doctest unit tests
# =============================================================================
cmake_minimum_required(VERSION 3.28)

project(Engine2D
    VERSION 0.10.0
    DESCRIPTION "2D game engine, built one week at a time"
    LANGUAGES CXX)

# --- Language standard ------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Where build output goes ------------------------------------------------
# Everything lands in one place so the sandbox executable can find assets/
# by walking a known relative path. Week 9's FileSystem depends on this being
# predictable. 
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Options ----------------------------------------------------------------
option(ENGINE_BUILD_TESTS       "Build the unit test target"          ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"  OFF)
option(ENGINE_WITH_IMGUI        "Build the ImGui editor layer"        ON)   # Week 2

# Week 5: std::thread needs the platform thread library on most toolchains.
find_package(Threads REQUIRED)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

add_subdirectory(engine)
add_subdirectory(gamescripts)     # the behaviours the editor creates; linked by both targets below
add_subdirectory(sandbox)

if(ENGINE_WITH_IMGUI)
    add_subdirectory(editor)          # Week 2: the third target
endif()

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
