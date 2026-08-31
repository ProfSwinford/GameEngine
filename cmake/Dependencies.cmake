# =============================================================================
#  Third-party dependencies, fetched at configure time.
#
#  Nothing here is installed on your machine. CMake downloads each dependency
#  into the build directory and builds it as part of your project. That is why
#  a fresh clone works on a machine that has never seen SDL.
#
#  Every tag below is pinned. Do not change them without telling the class - a
#  version skew between two students is a debugging session neither of you
#  will enjoy.
# =============================================================================
include(FetchContent)

# Show download progress. Silence is indistinguishable from a hang.
set(FETCHCONTENT_QUIET OFF)

# CMake 4 removed compatibility with projects declaring a minimum below 3.5.
# doctest 2.4.11 and nlohmann_json 3.11.3 both predate that. Rather than
# unpinning two dependencies, tell CMake to treat their old declarations as 3.5.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

# --- SDL3 -------------------------------------------------------------------
# Static link: the executable has no DLL or .so to locate at runtime, which
# removes an entire category of "works on my machine" failure.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.14
    GIT_SHALLOW    TRUE
    SYSTEM)                       # SYSTEM: do not warn about SDL's own headers

FetchContent_MakeAvailable(SDL3)

# --- doctest (Week 2) -------------------------------------------------------
# Header-only unit test framework. Chosen for compile speed: a test suite that
# is slow to build is a test suite that stops getting run.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(doctest)

# --- nlohmann_json (Week 8) -------------------------------------------------
# Header-only JSON. Serves Week 8 config AND Week 9 scene files - one parser,
# two schemas. Linked PRIVATE to `engine`: which parser we use is an
# implementation detail, and no public header mentions a JSON type.
set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(nlohmann_json)

# --- Dear ImGui (Week 2) ----------------------------------------------------
if(ENGINE_WITH_IMGUI)
    include(cmake/imgui.cmake)
endif()
