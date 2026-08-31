# =============================================================================
#  2D Game Engine - the top-level build file.
#
#  CMake is the tool that turns this project into something your compiler can
#  build, on Windows, macOS or Linux, without anybody having to maintain three
#  separate project files. This file is where it starts.
#
#  Four things are built:
#
#      engine/       a static library - the engine itself. Knows nothing about
#                    any particular game.
#      gamescripts/  the behaviour scripts. Linked by BOTH programs below, so
#                    a script runs in the editor as well as in the game.
#      sandbox/      the game, running on its own.
#      editor/       the development environment.
#      tests/        the unit tests.
# =============================================================================
cmake_minimum_required(VERSION 3.28)

project(Engine2D
    VERSION 1.0.0
    DESCRIPTION "A 2D game engine and editor for learning"
    LANGUAGES CXX)

# --- which version of C++ ----------------------------------------------------
# C++23 is used for std::format (see core/Log.h) and a handful of other
# conveniences. REQUIRED means "fail now with a clear message" rather than
# quietly falling back to an older standard and producing confusing errors.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # standard C++ only, no compiler-specific extras

# --- where the built programs end up -----------------------------------------
# Everything lands in one folder, which is what lets the programs find the
# assets folder by walking up from their own location. See fs/FileSystem.h.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- switches ----------------------------------------------------------------
option(ENGINE_BUILD_TESTS        "Build the unit tests"                ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"   OFF)
option(ENGINE_WITH_IMGUI         "Build the editor"                    ON)

include(cmake/Dependencies.cmake)      # downloads SDL, ImGui, JSON and doctest
include(cmake/CompilerWarnings.cmake)  # what the compiler should complain about

add_subdirectory(engine)
add_subdirectory(gamescripts)
add_subdirectory(sandbox)

if(ENGINE_WITH_IMGUI)
    add_subdirectory(editor)
endif()

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
