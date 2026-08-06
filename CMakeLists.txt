# Define the minimum CMake version (3.25+ is highly recommended for stable C++23 support)
cmake_minimum_required(VERSION 3.25)

# Set the project name, version, and language
project(Name VERSION 1.0.0 LANGUAGES CXX)

# Enforce the C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disables compiler-specific extensions for pure C++23

# Enable FetchContent module to import SDL3 version 3.4.14 directly from its Git repository
include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.14  # Targets the exact 3.4.14 release tag
    GIT_SHALLOW    TRUE            # Downloads only the tag without full history (faster)
)
set(SDL_TESTS OFF CACHE BOOL "" FORCE) # Disables building SDL3 tests to reduce build time and dependencies
FetchContent_MakeAvailable(SDL3) # Ensures SDL3 is built and available for linking

# SDL3 resources/examples are available @ https://examples.libsdl.org/SDL3/

# Define the executable target and its main entry point
add_executable(build src/main.cpp)

# Map the "Name" project name to the target for clean organization
add_executable(Name::Name ALIAS build)

# Configure include directories for header files
target_include_directories(build PRIVATE include)

target_link_libraries(build PRIVATE SDL3::SDL3)

add_custom_command(TARGET build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:build>
)

# Recommended: Enable standard compiler warnings to catch errors early
if(MSVC)
    target_compile_options(build PRIVATE /W4)
else()
    target_compile_options(build PRIVATE -Wall -Wextra -Wpedantic)
endif()