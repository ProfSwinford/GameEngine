# =============================================================================
#  The engine library.
#
#  This builds a STATIC LIBRARY, not a program you can run. It knows nothing
#  about any particular game: no entity names, no level layouts, no rules.
#  The `sandbox` and `editor` targets are the programs, and both are built on
#  top of this.
#
#  Every new .cpp file has to be added to ENGINE_SOURCES below or it simply
#  will not be compiled. The headers are listed too, purely so they show up in
#  the project tree in Visual Studio.
#
#  WHY THE FOLDER IS src/engine/ AND NOT JUST src/
#  Each module is two files - Math.h next to Math.cpp - and they are kept in
#  one folder so that reading a module means opening two files side by side
#  rather than hunting through two parallel trees.
#
#  The inner `engine/` exists for one reason: `src/` is what goes on the
#  compiler's include path, so a path written as
#      #include <engine/modules/Math.h>
#  finds src/engine/modules/Math.h. That prefix is what keeps the engine's
#  headers from colliding with anybody else's.
# =============================================================================
 
set(ENGINE_SOURCES
    src/engine/Engine.cpp                    # start-up order and the frame loop 

    src/engine/modules/Subsystem.cpp         # ordered start-up and shutdown
    src/engine/modules/Diagnostics.cpp       # the message log, and the Console's copy
    src/engine/modules/Settings.cpp          # reading JSON, and config/engine.json
    src/engine/modules/Files.cpp             # finding and reading files
    src/engine/modules/Time.cpp              # the fixed timestep
    src/engine/modules/Math.cpp              # vectors, matrices, transforms, overlap
    src/engine/modules/Platform.cpp          # the window, and the events the OS sends
    src/engine/modules/Input.cpp             # key presses -> named actions
    src/engine/modules/Rendering.cpp         # textures, the camera, drawing, gizmos
    src/engine/modules/Physics.cpp           # collision shapes and events
    src/engine/modules/Assets.cpp            # loading images
    src/engine/modules/Entities.cpp          # entities, and safe create and destroy
    src/engine/modules/Scene.cpp             # loading and saving levels
    src/engine/modules/Components.cpp        # components, the factory, system order
    src/engine/modules/Scripting.cpp         # your own behaviour classes
    src/engine/modules/Messaging.cpp         # entities talking to each other
)

set(ENGINE_HEADERS
    src/engine/Engine.h           # the one header a game includes

    src/engine/modules/Assets.h
    src/engine/modules/Components.h
    src/engine/modules/Diagnostics.h
    src/engine/modules/Entities.h
    src/engine/modules/Files.h
    src/engine/modules/Input.h
    src/engine/modules/Math.h
    src/engine/modules/Messaging.h
    src/engine/modules/Physics.h
    src/engine/modules/Platform.h
    src/engine/modules/Rendering.h
    src/engine/modules/Scene.h
    src/engine/modules/ScriptHooks.h
    src/engine/modules/Scripting.h
    src/engine/modules/Settings.h
    src/engine/modules/Subsystem.h
    src/engine/modules/Time.h
)

add_library(engine SHARED ${ENGINE_SOURCES} ${ENGINE_HEADERS})
add_library(Engine2D::engine ALIAS engine)

# ---------------------------------------------------------------------------
#  WINDOWS_EXPORT_ALL_SYMBOLS
#
#  On Windows, a function inside a DLL is invisible from outside unless it is
#  explicitly exported. Normally that means writing __declspec(dllexport) on
#  everything, which is hundreds of annotations to add and to keep correct.
#
#  This property tells CMake to scan the compiled object files and generate
#  that export list automatically. It works here because the engine only needs
#  its FUNCTIONS exported: the property does not handle exported DATA, and the
#  engine deliberately has none - every piece of state lives in an anonymous
#  namespace inside a .cpp and is reached through a static member function.
#
#  Ignored on macOS and Linux, where everything is visible by default.
# ---------------------------------------------------------------------------
set_target_properties(engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# PUBLIC means "anything that links the engine also gets this folder on its
# include path", which is what lets the game, the editor and a project's
# scripts all write
#     #include <engine/Engine.h>
# The .cpp files sitting in the same folders are simply never included by
# anybody, so having them there costs nothing.
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
#  SDL is linked PRIVATE, and that is a deliberate boundary rather than a
#  detail. PRIVATE means the engine can use SDL but nothing that links the
#  engine can reach it - so a game written on top of this CANNOT accidentally
#  start calling SDL directly and tie itself to one library.
#
#  It only works because no public header of the engine mentions an SDL type;
#  see platform/SdlHandles.h for how that is arranged.
# ---------------------------------------------------------------------------
target_link_libraries(engine PRIVATE SDL3::SDL3)

# nlohmann/json IS part of the public interface: core/Json.h includes it, and
# every component's Deserialize takes a Json. That is a deliberate choice -
# reading and writing scene files is something students do directly, and
# hiding the JSON library behind a wrapper of our own would mean learning two
# APIs instead of one.
#
# It is header-only, so nothing is actually linked; this passes the include
# folder along to everything that uses the engine.
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

# Note what is NOT here any more: Dear ImGui. It used to be part of the engine,
# and it now lives in the editor - see editor/src/EditorGui.h for why. The
# engine's only remaining connection to a tool layer is three function pointers
# in tools/GuiHooks.h.

engine_set_warnings(engine)
