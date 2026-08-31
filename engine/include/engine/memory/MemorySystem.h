#pragma once

// =============================================================================
//  WEEK 7 - allocator instrumentation and the engine's own allocators.
//
//  Ch. 10.9 asks for bytes in use, peak bytes, allocation count and block
//  count on screen, updating as the engine runs. This is the registry that
//  makes that possible from OUTSIDE the engine: the Memory panel lives in the
//  editor and cannot see a private member.
//
//  *** KEEP THE INSTRUMENTATION SMALL. *** It is a handful of counters, not a
//  subsystem, and letting it expand is the single most common way Week 7
//  overruns. Every allocator registers itself on construction and unregisters
//  on destruction; the registry stores raw pointers and owns nothing.
//
//  ---------------------------------------------------------------------------
//  THE TWO ENGINE ALLOCATORS, and why each is the shape it is - answering
//  Week 7 evidence question 2 in terms of LIFETIME, not size:
//
//   FrameStack (a StackAllocator, cleared every frame)
//     For things whose lifetime is exactly one frame: the collision system's
//     candidate list, temporary strings built for a HUD line. They all die at
//     the same instant, which is precisely the case where "you cannot free
//     individually" costs nothing and "freeing a thousand costs the same as
//     freeing one" is free money.
//
//   EntityPool (a PoolAllocator)
//     For things with INDEPENDENT lifetimes and a uniform size: entity
//     records. A bullet spawned on frame 12 and destroyed on frame 40 has no
//     relationship to the enemy spawned on frame 13, so a stack is exactly
//     wrong and a pool is exactly right.
// =============================================================================

#include <engine/core/Types.h>

#include <functional>

namespace eng {

class StackAllocator;
class PoolAllocator;

class MemorySystem {
public:
    // Creates the engine's own allocators at the sizes the config file asked
    // for. Called by the memory subsystem at boot - which is where a big
    // first-touch cost belongs. See the page-fault note in StackAllocator.h.
    static bool Init(usize frameStackBytes, usize entityPoolBlocks, usize entityBlockSize);
    static void Shutdown();

    static StackAllocator* FrameStack();
    static PoolAllocator*  EntityPool();

    // Called at the top of every frame. This is the "sawtooth" the Memory
    // panel's plot shows for a healthy scratch allocator.
    static void BeginFrame();

    // --- the registry ------------------------------------------------------
    static void RegisterStack(StackAllocator* allocator);
    static void UnregisterStack(StackAllocator* allocator);
    static void RegisterPool(PoolAllocator* allocator);
    static void UnregisterPool(PoolAllocator* allocator);

    static void ForEachStack(const std::function<void(StackAllocator&)>& fn);
    static void ForEachPool(const std::function<void(PoolAllocator&)>& fn);

    static usize StackCount();
    static usize PoolCount();

    // Totals across every registered allocator, for the debug-text HUD in the
    // sandbox (which has no ImGui) and the summary line in the panel.
    static usize TotalBytesUsed();
    static usize TotalBytesCapacity();
    static usize TotalPeakBytes();
};

} // namespace eng
