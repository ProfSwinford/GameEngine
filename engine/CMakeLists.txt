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
# =============================================================================
 
set(ENGINE_SOURCES
    src/Engine.cpp                    # start-up order and the frame loop

    src/core/Config.cpp               # reading config/engine.json
    src/core/GameClock.cpp            # the fixed timestep
    src/core/Json.cpp                 # safe JSON readers
    src/core/Log.cpp                  # the message log
    src/core/LogBuffer.cpp            # the copy the Console window shows
    src/core/Subsystem.cpp            # ordered start-up and shutdown

    src/fs/FileSystem.cpp             # finding and reading files

    src/input/InputMap.cpp            # key presses -> named actions

    src/math/Mat3.cpp
    src/math/Overlap.cpp
    src/math/Random.cpp
    src/math/Transform2D.cpp

    src/physics/Collider.cpp          # collision shapes and events

    src/platform/EventPump.cpp        # reading input from the OS
    src/platform/SdlHandles.cpp       # automatic clean-up for SDL objects
    src/platform/Window.cpp           # the game window

    src/render/Camera.cpp
    src/render/Gizmos.cpp             # helper shapes drawn over the world
    src/render/Renderer.cpp           # lines, rectangles, text and sprites

    src/resource/ResourceManager.cpp  # loading images

    src/scene/Component.cpp           # components, the factory, sprites
    src/scene/DeferredOps.cpp         # safe create and destroy
    src/scene/Entity.cpp
    src/scene/Messaging.cpp           # entities talking to each other
    src/scene/Scene.cpp               # loading and saving levels
    src/scene/ScriptComponent.cpp     # your own behaviour classes
    src/scene/SpinComponent.cpp       # a small worked example component
    src/scene/SystemOrder.cpp         # what updates, in what order
    src/scene/ScriptLibrary.cpp       # loading the project's compiled scripts

    src/tools/GuiHooks.cpp            # lets the editor see input first
)

set(ENGINE_HEADERS
    include/engine/Engine.h
    include/engine/core/Config.h
    include/engine/core/GameClock.h
    include/engine/core/Json.h
    include/engine/core/Log.h
    include/engine/core/LogBuffer.h
    include/engine/core/Subsystem.h
    include/engine/fs/FileSystem.h
    include/engine/input/InputMap.h
    include/engine/math/Mat3.h
    include/engine/math/Overlap.h
    include/engine/math/Random.h
    include/engine/math/Transform2D.h
    include/engine/math/Vec2.h
    include/engine/physics/Collider.h
    include/engine/platform/EventPump.h
    include/engine/platform/SdlHandles.h
    include/engine/platform/Window.h
    include/engine/render/Camera.h
    include/engine/render/Gizmos.h
    include/engine/render/Renderer.h
    include/engine/render/Texture.h
    include/engine/resource/ResourceManager.h
    include/engine/scene/Component.h
    include/engine/scene/DeferredOps.h
    include/engine/scene/Entity.h
    include/engine/scene/EntityId.h
    include/engine/scene/Messaging.h
    include/engine/scene/Scene.h
    include/engine/scene/ScriptComponent.h
    include/engine/scene/ScriptHooks.h
    include/engine/scene/SpinComponent.h
    include/engine/scene/SystemOrder.h
    include/engine/scene/ScriptLibrary.h
    include/engine/tools/GuiHooks.h
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

# PUBLIC means "anything that links the engine also gets this include folder",
# which is what lets the game, the editor and a project's scripts all write
#     #include <engine/Engine.h>
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
