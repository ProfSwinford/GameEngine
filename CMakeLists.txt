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
#      player/       a game, running on its own - the export template.
#      editor/       the development environment.
#      hub/          the launcher: a list of projects, a list of engine builds.
#                    What the editor opens when it was given no project, which
#                    is why `editor` depends on it - see editor/CMakeLists.txt.
#      tests/        the unit tests. 
#
#  ---------------------------------------------------------------------------
#  NOTE WHAT CMAKE DOES NOT BUILD: the project's scripts.
#
#  Every .cpp and .h under assets/ is compiled by the EDITOR, while it runs, into
#  a library that the editor and the player both load - see
#  editor/src/ScriptBuild.h. That
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

# =============================================================================
#  THE SCRIPT ABI NUMBER - bump this when a script would have to be recompiled.
#
#  The version above is for people: it names a release and goes in a window
#  title. This number is for the build, and it exists because the two change
#  at completely different rates.
#
#  A project's scripts compile into DLLs that are loaded into the editor and
#  pass std::string and std::vector across the boundary, so they have to be
#  built against the same headers as the engine that loads them. BUMP THIS
#  when something a script can see changes shape:
#
#    * a field added to or removed from a struct a script holds by value
#    * a virtual function added to ScriptBehaviour, or its order changed
#    * an enum's values reordered or renumbered
#    * anything else that changes the layout of something crossing the DLL
#      boundary
#
#  Do NOT bump it for a change inside a .cpp, or a new function on a class a
#  script only ever holds a pointer to.
#
#  WHAT IT PREVENTS: the editor records this number alongside the C runtime
#  flavour when it compiles a project's scripts, and recompiles them all when
#  it changes. Without it, replacing an engine build IN PLACE - which is what
#  installing a new version over an old one is - leaves the scripts looking
#  perfectly up to date while they were built against different headers. That
#  failure does not announce itself: the library loads, the Console says the
#  build succeeded, and the program dies on the first allocation that crosses
#  the heap boundary. See engine/include/engine/project/EngineBuild.h.
#
#  Keeping it separate from the version means a release with no layout change
#  does not force every project to recompile, and a layout change cannot be
#  forgotten just because the version was already bumped for something else.
# =============================================================================
set(ENGINE_SCRIPT_ABI 1)

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
add_subdirectory(player)

if(ENGINE_WITH_IMGUI)
    add_subdirectory(editor)

    # The launcher, under the same option as the editor because it exists to
    # start one and uses the same ImGui. See hub/CMakeLists.txt.
    add_subdirectory(hub)
endif()

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
