# =============================================================================
#  The four libraries this project uses, and where they come from.
#
#  NOTHING HERE IS INSTALLED ON YOUR MACHINE. CMake downloads each library into
#  the build folder and builds it as part of this project. That is why a fresh
#  copy of the project works on a computer that has never seen SDL, and why
#  there is no list of things to install before you start.
#
#  Every version below is pinned to an exact tag. Do not change one without
#  telling everybody else - two people on different versions of the same
#  library is a debugging session neither of them will enjoy.
# =============================================================================
include(FetchContent)

# Show download progress. Silence is indistinguishable from a program that has
# hung, and the first download takes several minutes.
set(FETCHCONTENT_QUIET OFF)

# CMake 4 stopped accepting projects that declare a minimum version below 3.5.
# doctest and nlohmann/json both predate that change. Rather than moving to
# untested versions of two libraries, this tells CMake to treat their old
# declarations as 3.5.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

# --- SDL3 --------------------------------------------------------------------
#  Opens the window, reads the keyboard and the mouse, and draws pixels. Doing
#  those things directly means writing different code for Windows, macOS and
#  Linux; SDL does that part so the rest of this project is one set of files.
#
#  Linked as a SHARED library, so there is exactly ONE copy of SDL in the
#  running program. That matters now that the engine is itself a shared
#  library: if the engine and the editor each linked their own private copy of
#  SDL, there would be two sets of SDL's internal state - two event queues, two
#  ideas of which window exists - and nothing would work.
#
#  The cost is that SDL3.dll has to sit next to the programs. CMake puts it
#  there automatically, because shared libraries and executables share one
#  output folder (see the top-level CMakeLists.txt).
set(SDL_SHARED       ON  CACHE BOOL "" FORCE)
set(SDL_STATIC       OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.14
    GIT_SHALLOW    TRUE
    SYSTEM)         # SYSTEM: do not report warnings from SDL's own headers,
                    # because they are not ours to fix

FetchContent_MakeAvailable(SDL3)

# --- doctest -----------------------------------------------------------------
#  The unit test framework. Header-only, so there is nothing to install, and it
#  was chosen for how fast it compiles - a test suite that is slow to build is
#  a test suite that stops being run.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(doctest)

# --- nlohmann/json -----------------------------------------------------------
#  Reads and writes the .json files: the settings file and every scene. Also
#  header-only. It is the most widely used JSON library for C++ and its whole
#  interface is one type that behaves like the containers you already know.
#
#  Unlike SDL, this one IS part of the engine's public interface - components
#  read and write their own settings through it. See engine/core/Json.h for why
#  that is a deliberate choice rather than an oversight.
set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(nlohmann_json)

# --- Dear ImGui --------------------------------------------------------------
#  The library the entire editor interface is drawn with. Only fetched when the
#  editor is being built - see cmake/imgui.cmake.
if(ENGINE_WITH_IMGUI)
    include(cmake/imgui.cmake)
endif()
