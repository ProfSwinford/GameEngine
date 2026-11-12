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
#    * the SIGNATURE of a function pointer in a table a script fills in and
#      the engine calls - ScriptHooks, most of all. Strictly read, the bullets
#      above do not cover that: every function pointer is eight bytes, so
#      sizeof(ScriptHooks) and every member offset can be identical while the
#      CALLING CONVENTION of one of them has changed. Nothing compares sizeof,
#      the mangled name of ScriptRegistry::Register encodes only the class
#      name, and the editor's header-mtime rebuild hides it on the machine
#      that made the change - so this number really is the only thing between
#      a stale script DLL and reading one type as another.
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
#  History, so that a bump is not a bare number with no reason attached:
#    1  the first one.
#    2  ScriptBehaviour gained the bare-name handles - transform, gameObject,
#       scene, name, tag - which are data members, so sizeof and the layout of
#       the base of every script class changed. Nothing in ScriptAbi.h compares
#       sizeof or alignof; this number is the only thing standing between a
#       stale script DLL and reading the wrong offsets at run time.
#    3  the three trigger slots in ScriptHooks were retyped from
#       void(*)(ScriptBehaviour*, EntityId) to
#       void(*)(ScriptBehaviour*, const Collision2D&), so that OnTriggerEnter
#       and OnTriggerStay can take a collider and all three can take the whole
#       contact. The struct's size and every offset in it are UNCHANGED - only
#       what those three pointers may be called with - which is exactly the
#       case the new bullet above was added for. A script DLL built against
#       the old header installs a function expecting an EntityId where the
#       engine now passes a const Collision2D&: the id's index would be read
#       out of the low half of a pointer and its generation out of the high
#       half, so the hook would silently never match anything rather than
#       crashing. Nothing but this number catches it.
#    4 - ScriptComponent grew a list of running coroutines, and
#        ScriptBehaviour grew StartCoroutine. Both layouts cross the boundary
#        through inline and template code, so a script DLL built against the
#        old header would read every member after the new one at the wrong
#        offset. Nothing but this number catches that.
#
#        The same number also covers WaitForEndOfFrame, added before any of
#        this shipped: a fourth enumerator APPENDED to Coroutine's Wait, and a
#        resume-pass stamp on each list entry. Appending to that enum is safe
#        only because it IS appended - the frame it lives in is allocated in
#        the script's library and the engine reads promise().wait through the
#        handle, so inserting a value ahead of Unscaled would have a stale DLL
#        writing 2 for "unscaled" and the engine reading 3 for "end of frame",
#        and the coroutine would silently never resume. Append only.
set(ENGINE_SCRIPT_ABI 4)

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

# =============================================================================
#  game_scripts - THE PROJECT'S OWN SCRIPTS, TYPE-CHECKED AND NOTHING ELSE.
#
#  Read the note at the top of this file first: there was briefly a CMake
#  target for the scripts, and it was removed because two build systems
#  writing the same file disagree. One builder, one output.
#
#  THAT RULE IS INTACT. This target produces no library, no DLL and nothing
#  under assets/ or .build/ - only object files in the build tree, which
#  nothing loads and nothing looks for. The editor is still the only thing
#  that builds the scripts anybody runs.
#
#  So what is it for? Two things that cost nothing once the sources are named:
#
#  INTELLISENSE IN A SOURCE TREE. Visual Studio treats any folder holding a
#  CMakeLists.txt as a CMake project, configures IntelliSense from the CMake
#  project model, and ignores the CppProperties.json the editor can write for
#  a plain project folder - so in here the thing that makes
#  <engine/Engine.h> resolve inside a script is that some target names it as a
#  source. EXCLUDE_FROM_ALL does not hide a target from that model.
#
#  For clangd it is the exported compile database that matters instead, and
#  that is a NINJA feature: the Visual Studio generator ignores
#  CMAKE_EXPORT_COMPILE_COMMANDS, so a tree configured with it has no database
#  at all. The presets here name no generator, so which one you get depends on
#  how you configured.
#
#  A TYPECHECK WORTH HAVING. Nothing in `all` ever compiles assets/, so a
#  script can sit broken in the tree beside a completely green test suite -
#  which has happened. `cmake --build . --target game_scripts` is a one-line
#  answer to "do the scripts in this checkout still compile?".
#
#  EXCLUDE_FROM_ALL, so it is never built unless asked. That is deliberate:
#  these are game scripts, and a warning in somebody's half-written behaviour
#  must not fail the engine's build - least of all under the strict preset.
# =============================================================================
file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cc"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cxx")

# A SCRIPT CAN BE WRITTEN ENTIRELY IN A HEADER, and the editor compiles those
# too - it pulls every .h under assets/ that contains ENGINE_REGISTER_SCRIPT
# through a generated unit of its own (ScriptBuild::Sources::headers). Taking
# only .cpp here would make "do the scripts in this checkout compile?" answer
# yes for a checkout where one of them does not, which is worse than not
# asking.
#
# The same content test the editor uses, so the two source sets agree. A
# header WITHOUT a registration is left alone, for the editor's reason: a
# helper written to be included in one particular place can easily fail to
# compile on its own.
#
# One honest limit: this test runs at CONFIGURE time, so adding
# ENGINE_REGISTER_SCRIPT to a header that already exists is picked up on the
# next configure rather than the next build. CONFIGURE_DEPENDS catches a
# header being added or removed, not one being edited.
file(GLOB_RECURSE GAME_SCRIPT_HEADERS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.hxx")

foreach(header IN LISTS GAME_SCRIPT_HEADERS)
    file(STRINGS "${header}" registers REGEX "ENGINE_REGISTER_SCRIPT" LIMIT_COUNT 1)
    if(registers)
        # LANGUAGE CXX, or CMake files a .h away as a header and compiles
        # nothing. It also gets /TP from the C++ rule, which is what stops a
        # .h being read as C.
        set_source_files_properties("${header}" PROPERTIES LANGUAGE CXX)
        list(APPEND GAME_SCRIPT_SOURCES "${header}")
    endif()
endforeach()

if(GAME_SCRIPT_SOURCES)
    # OBJECT, so there is no library to disagree with the editor's.
    add_library(game_scripts OBJECT EXCLUDE_FROM_ALL ${GAME_SCRIPT_SOURCES})

    # C++20, NOT the engine's C++23, because that is what the editor compiles
    # a project's scripts at - the reason every public header has to stay
    # C++20-clean. It has to be the target PROPERTY: CMAKE_CXX_STANDARD above
    # already made this target 23, and cxx_std_20 as a compile FEATURE would
    # be taken as a floor and lose to it.
    #
    # CXX_SCAN_FOR_MODULES is off because this target only ever type-checks.
    # Ninja scans every C++20-or-later target for module dependencies, and
    # there is nothing here to find.
    set_target_properties(game_scripts PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF)

    # The engine's headers and, through it, nlohmann/json's - the same two
    # directories the editor passes a script, and linking is the way to get
    # both without naming either. Nothing is linked: an OBJECT library stops
    # at the objects.
    target_link_libraries(game_scripts PRIVATE engine)

    # THE FORCED INCLUDE IS NOT OPTIONAL, and leaving it out is the one way
    # this target can be WRONG rather than merely narrow. The editor never
    # hands a project's .cpp to the compiler: it compiles a generated unit
    # whose first line is #include <engine/Engine.h> and whose last includes
    # the script. So a script naming no headers at all still compiles, and
    # anything claiming to describe that compile has to say so too - exactly
    # the argument CompileSettings::forcedInclude makes in
    # editor/src/ScriptBuild.h, and exactly what the editor writes into a
    # compile database as /FI. Without it this target, and the database CMake
    # exports for it, are both stricter than the build and report errors in
    # scripts that build perfectly.
    #
    # /W3 matches what the editor really uses on a project's scripts, and
    # engine_set_warnings is deliberately NOT called: it would add /W4
    # /permissive-, report warnings the real script build never shows, and
    # turn them into errors under the strict preset.
    # =========================================================================
    #  AND THE SECOND FORCED INCLUDE: THE SCRIPT NAMES.
    #
    #  A script refers to another script by its BARE NAME - `Door door;` - and
    #  nothing in the file says where that name comes from. In the real build
    #  it comes from the generated unit, which wraps every script in the
    #  project in a namespace of its own and aliases the names; a .cpp handed
    #  to a compiler on its own has never heard of them, so every such
    #  reference is an error here and in the IntelliSense this target feeds.
    #  The editor writes .build/script-scope.h for exactly this - see
    #  GenerateScriptScopeHeader in engine/tools/ScriptVars.h.
    #
    #  THROUGH A WRAPPER, AND __has_include, RATHER THAN NAMED DIRECTLY. That
    #  file is written by the EDITOR, so in a fresh checkout it does not exist
    #  yet - and /FI naming a missing file is C1083, fatal, on every script in
    #  the target. An if(EXISTS) here would instead bake the answer into the
    #  cache: configure before the first build and the references stay broken
    #  until somebody thinks to reconfigure, which is the same stale-cache
    #  trap docs/scripting-reference.md already has a section about.
    #
    #  __has_include is evaluated when the file is COMPILED, so this is
    #  self-healing: the moment the editor writes the header, the next parse
    #  picks it up, with no reconfigure and nothing to remember.
    # =========================================================================
    set(GAME_SCRIPT_SCOPE "${CMAKE_CURRENT_SOURCE_DIR}/.build/script-scope.h")
    set(GAME_SCRIPT_SCOPE_WRAPPER
        "${CMAKE_CURRENT_BINARY_DIR}/game_scripts_scope.h")
    file(CONFIGURE OUTPUT "${GAME_SCRIPT_SCOPE_WRAPPER}" CONTENT
"// Generated by CMake. See the game_scripts notes in the top-level
// CMakeLists.txt.
//
// The file this reaches for is written by the EDITOR, on every script build,
// and declares every script in the project so that one script can refer to
// another by its bare name. It is absent in a fresh checkout, which is why
// this is a __has_include and not a plain include: that question is asked when
// a script is compiled rather than when CMake ran, so nothing has to be
// reconfigured once the editor has written it.
#pragma once
#if __has_include(\"@GAME_SCRIPT_SCOPE@\")
#include \"@GAME_SCRIPT_SCOPE@\"
#endif
" @ONLY NEWLINE_STYLE LF)

    if(MSVC)
        target_compile_options(game_scripts PRIVATE /W3 /FIengine/Engine.h
                               "/FI${GAME_SCRIPT_SCOPE_WRAPPER}")
    else()
        target_compile_options(game_scripts PRIVATE -Wall -include engine/Engine.h
                               -include "${GAME_SCRIPT_SCOPE_WRAPPER}")
    endif()

    list(LENGTH GAME_SCRIPT_SOURCES GAME_SCRIPT_COUNT)
    message(STATUS
        "game_scripts: ${GAME_SCRIPT_COUNT} project script(s) available to type-check "
        "(not built by default)")
endif()
