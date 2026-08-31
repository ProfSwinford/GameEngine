# =============================================================================
#  2D Game Engine - the top-level build file.
#
#  CMake is the tool that turns this project into something your compiler can
#  build, on Windows, macOS or Linux, without anybody having to maintain three
#  separate project files. This file is where it starts.
#
#  Five things are built:
#
#      engine/       a SHARED library - the engine itself. Knows nothing about
#                    any particular game. Shared rather than static because a
#                    project's scripts link against it at run time; the reason
#                    is written out in engine/CMakeLists.txt.
#      sandbox/      the game, running on its own.
#      editor/       the development environment.
#      tests/        the unit tests.
#
#  ---------------------------------------------------------------------------
#  NOTE WHAT CMAKE DOES NOT BUILD: the project's scripts.
#
#  Everything in scripts/ is compiled by the EDITOR, while it is running, into
#  a library that both programs above load - see editor/src/ScriptBuild.h. That
#  is the whole point: somebody using the released editor has a compiler and a
#  project folder, not this source tree, and adding a script must not require
#  rebuilding anything here.
#
#  There was briefly a CMake target for it as well, "so a fresh clone works".
#  It was removed because two build systems writing the same file disagree: the
#  editor would rebuild the library, CMake would decide it was still up to
#  date, and a deleted script went on running. One builder, one output.
#
#  To build the scripts without opening the editor:  editor --build-scripts
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

# ---------------------------------------------------------------------------
#  On Windows, use the SHARED C runtime (/MD and /MDd) rather than the static
#  one. That is CMake's default, and it is written down here because it is now
#  load-bearing rather than incidental.
#
#  The engine, the editor and a project's compiled scripts are three separate
#  binaries that pass std::string and std::vector between them. They can only
#  do that safely if they share ONE copy of the C runtime, and therefore one
#  heap - otherwise memory allocated in one and freed in another corrupts
#  quietly. The editor compiles scripts with a matching flag for the same
#  reason; see editor/src/ScriptBuild.cpp.
# ---------------------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

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
add_subdirectory(sandbox)

if(ENGINE_WITH_IMGUI)
    add_subdirectory(editor)
endif()

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
