// WEEK 7 - the allocator registry. See MemorySystem.h.
//
// Deliberately small: two vectors of raw pointers and a mutex. It owns nothing
// and it does no bookkeeping the allocators are not already doing.

#include <engine/core/Log.h>
#include <engine/memory/MemorySystem.h>
#include <engine/memory/PoolAllocator.h>
#include <engine/memory/StackAllocator.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace eng {
namespace {

// Function-local statics, constructed on first use. An allocator can be
// constructed by a static object before main() runs (nothing in this engine
// does, but a test binary might), and a namespace-scope vector would then be
// registered into before it existed - the static initialization order fiasco,
// which is the thing Week 7 opens with.
struct Registry {
    std::mutex                   mutex;
    std::vector<StackAllocator*> stacks;
    std::vector<PoolAllocator*>  pools;
};

Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

std::unique_ptr<StackAllocator> g_frameStack;
std::unique_ptr<PoolAllocator>  g_entityPool;

} // namespace

bool MemorySystem::Init(usize frameStackBytes, usize entityPoolBlocks,
                        usize entityBlockSize) {
    g_frameStack = std::make_unique<StackAllocator>(frameStackBytes, "FrameStack");
    g_entityPool = std::make_unique<PoolAllocator>(entityBlockSize, entityPoolBlocks,
                                                   alignof(std::max_align_t),
                                                   "EntityPool");

    ENGINE_LOG_INFO(Channels::kMemory,
                    "MemorySystem up: frame stack {} bytes, entity pool {} x {} bytes",
                    frameStackBytes, entityPoolBlocks, g_entityPool->BlockSize());
    return true;
}

void MemorySystem::Shutdown() {
    // Reverse of construction, and it matters: these own heap blocks, and
    // "everything passes but the engine leaks at exit" is almost always
    // something that owns a slab not being torn down at all.
    if (g_frameStack != nullptr) {
        ENGINE_LOG_INFO(Channels::kMemory, "FrameStack peak was {} of {} bytes",
                        g_frameStack->PeakBytes(), g_frameStack->BytesCapacity());
    }
    if (g_entityPool != nullptr) {
        ENGINE_LOG_INFO(Channels::kMemory, "EntityPool peak was {} of {} blocks",
                        g_entityPool->PeakBlocksInUse(), g_entityPool->BlockCount());
    }
    g_entityPool.reset();
    g_frameStack.reset();
    ENGINE_LOG_INFO(Channels::kMemory, "MemorySystem down");
}

StackAllocator* MemorySystem::FrameStack() { return g_frameStack.get(); }
PoolAllocator*  MemorySystem::EntityPool() { return g_entityPool.get(); }

void MemorySystem::BeginFrame() {
    if (g_frameStack != nullptr) {
        g_frameStack->Clear();
    }
}

void MemorySystem::RegisterStack(StackAllocator* allocator) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.stacks.push_back(allocator);
}

void MemorySystem::UnregisterStack(StackAllocator* allocator) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::erase(registry.stacks, allocator);
}

void MemorySystem::RegisterPool(PoolAllocator* allocator) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.pools.push_back(allocator);
}

void MemorySystem::UnregisterPool(PoolAllocator* allocator) {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::erase(registry.pools, allocator);
}

void MemorySystem::ForEachStack(const std::function<void(StackAllocator&)>& fn) {
    // Copy the pointer list under the lock, call outside it. Panel code runs
    // in the callback and must not be holding a registry lock while it does.
    std::vector<StackAllocator*> copy;
    {
        Registry& registry = GetRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        copy = registry.stacks;
    }
    for (StackAllocator* allocator : copy) {
        fn(*allocator);
    }
}

void MemorySystem::ForEachPool(const std::function<void(PoolAllocator&)>& fn) {
    std::vector<PoolAllocator*> copy;
    {
        Registry& registry = GetRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        copy = registry.pools;
    }
    for (PoolAllocator* allocator : copy) {
        fn(*allocator);
    }
}

usize MemorySystem::StackCount() {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.stacks.size();
}

usize MemorySystem::PoolCount() {
    Registry& registry = GetRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.pools.size();
}

usize MemorySystem::TotalBytesUsed() {
    usize total = 0;
    ForEachStack([&](StackAllocator& a) { total += a.BytesUsed(); });
    ForEachPool([&](PoolAllocator& a)  { total += a.BytesUsed(); });
    return total;
}

usize MemorySystem::TotalBytesCapacity() {
    usize total = 0;
    ForEachStack([&](StackAllocator& a) { total += a.BytesCapacity(); });
    ForEachPool([&](PoolAllocator& a)  { total += a.BytesCapacity(); });
    return total;
}

usize MemorySystem::TotalPeakBytes() {
    usize total = 0;
    ForEachStack([&](StackAllocator& a) { total += a.PeakBytes(); });
    ForEachPool([&](PoolAllocator& a)  { total += a.PeakBlocksInUse() * a.BlockSize(); });
    return total;
}

} // namespace eng
