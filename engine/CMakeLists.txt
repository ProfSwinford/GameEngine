# =============================================================================
#  The engine library.
#
#  This is a STATIC LIBRARY, not an executable. It knows nothing about your
#  game. The Week 10 gate is built in `sandbox` without opening any file in
#  this directory; that test only means something if the boundary is real.
#
#  Every new .cpp is added to ENGINE_SOURCES below. Headers are listed too so
#  they show up in IDE project trees.
# =============================================================================

set(ENGINE_SOURCES
    # Week 1
    src/platform/Window.cpp
    # Week 2
    src/core/ByteBuffer.cpp
    src/core/MemUtil.cpp
    src/platform/EventPump.cpp
    # Week 3
    src/core/Log.cpp
    src/core/LogBuffer.cpp
    src/core/Assert.cpp
    src/platform/SdlHandles.cpp
    # Week 4
    src/debug/ScopedTimer.cpp
    # Week 6
    src/math/Mat3.cpp
    src/math/Transform2D.cpp
    src/math/Overlap.cpp
    src/math/Random.cpp
    src/debug/DebugDraw.cpp
    src/debug/Camera.cpp
    src/platform/Renderer.cpp
    # Week 7
    src/memory/StackAllocator.cpp
    src/memory/PoolAllocator.cpp
    src/memory/MemorySystem.cpp
    src/core/Subsystem.cpp
    src/concurrency/JobSystem.cpp
    # Week 8
    src/core/StringId.cpp
    src/core/CVar.cpp
    src/core/Config.cpp
    src/input/InputMap.cpp
    # Week 9
    src/fs/FileSystem.cpp
    src/resource/ResourceManager.cpp
    src/scene/Entity.cpp
    src/scene/Component.cpp
    src/scene/Scene.cpp
    src/scene/SpinComponent.cpp
    src/scene/ScriptComponent.cpp
    # Week 10
    src/core/GameClock.cpp
    src/scene/SystemOrder.cpp
    src/scene/DeferredOps.cpp
    src/scene/Messaging.cpp
    src/physics/Collider.cpp
)

set(ENGINE_HEADERS
    include/engine/Engine.h
    include/engine/core/Types.h
    include/engine/core/Assert.h
    include/engine/core/ByteBuffer.h
    include/engine/core/Config.h
    include/engine/core/CVar.h
    include/engine/core/GameClock.h
    include/engine/core/Log.h
    include/engine/core/LogBuffer.h
    include/engine/core/MemUtil.h
    include/engine/core/StringId.h
    include/engine/core/Subsystem.h
    include/engine/concurrency/JobSystem.h
    include/engine/concurrency/ThreadSafeQueue.h
    include/engine/debug/Camera.h
    include/engine/debug/DebugDraw.h
    include/engine/debug/ScopedTimer.h
    include/engine/fs/FileSystem.h
    include/engine/input/InputMap.h
    include/engine/math/Mat3.h
    include/engine/math/Overlap.h
    include/engine/math/Random.h
    include/engine/math/Transform2D.h
    include/engine/math/Vec2.h
    include/engine/memory/MemorySystem.h
    include/engine/memory/PoolAllocator.h
    include/engine/memory/StackAllocator.h
    include/engine/physics/Collider.h
    include/engine/platform/EventPump.h
    include/engine/platform/Renderer.h
    include/engine/platform/SdlHandles.h
    include/engine/platform/Window.h
    include/engine/resource/Handle.h
    include/engine/resource/ResourceManager.h
    include/engine/scene/Component.h
    include/engine/scene/DeferredOps.h
    include/engine/scene/Entity.h
    include/engine/scene/Messaging.h
    include/engine/scene/Scene.h
    include/engine/scene/ScriptComponent.h
    include/engine/scene/SpinComponent.h
    include/engine/scene/SystemOrder.h
    include/engine/tools/EditorGui.h
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
add_library(Engine2D::engine ALIAS engine)

# PUBLIC: anything linking engine also gets this include path, so the sandbox
# can write #include <engine/platform/Window.h>
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
#  WEEK 3: SDL became PRIVATE.
#
#  In Week 1 this line read PUBLIC, because Window.h exposed SDL types. The
#  RAII conversion in SdlHandles.h removed the last raw SDL handle from the
#  public headers, and this is where the engine acquired a real API boundary.
#  Neither sandbox, editor, nor tests can reach SDL through us any more.
# ---------------------------------------------------------------------------
target_link_libraries(engine PRIVATE SDL3::SDL3)

# Week 5: the queue header is part of the public interface, so anything
# including it needs the threading library too. PUBLIC is correct here.
target_link_libraries(engine PUBLIC Threads::Threads)

# Week 8: JSON is an implementation detail of how config and scenes are read.
# No public header mentions a JSON type, so this stays PRIVATE.
target_link_libraries(engine PRIVATE nlohmann_json::nlohmann_json)

# Week 2: the ImGui lifecycle lives in the engine; the panels live in the
# editor. PUBLIC because with the IDE feature on, ImGui *is* part of the
# engine's public surface - the editor calls ImGui:: functions directly.
# Turn the option off and both the link and the definition disappear, which
# is what a shipping build wants.
if(ENGINE_WITH_IMGUI)
    target_sources(engine PRIVATE src/tools/EditorGui.cpp)
    target_link_libraries(engine PUBLIC imgui::imgui)
    target_compile_definitions(engine PUBLIC ENGINE_WITH_IMGUI)
endif()

engine_set_warnings(engine)
