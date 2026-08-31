// WEEK 7 - the pool allocator. See PoolAllocator.h for the free-list trick.

#include <engine/core/Assert.h>
#include <engine/core/Log.h>
#include <engine/memory/MemorySystem.h>
#include <engine/memory/PoolAllocator.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace eng {
namespace {

constexpr bool IsPowerOfTwo(usize value) {
    return value != 0 && (value & (value - 1)) == 0;
}

constexpr usize RoundUpToMultiple(usize value, usize multiple) {
    return ((value + multiple - 1) / multiple) * multiple;
}

} // namespace

PoolAllocator::PoolAllocator(usize blockSize, usize blockCount, usize alignment,
                             const char* name)
    : m_blockCount(blockCount), m_alignment(alignment),
      m_name(name != nullptr ? name : "pool") {
    ENGINE_ASSERT_MSG(IsPowerOfTwo(alignment), "pool alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) {
        m_alignment = alignof(std::max_align_t);
    }

    // A free block must be able to hold the next-free pointer. Asserting here
    // rather than discovering it as corruption is the whole point of the note
    // in the header.
    ENGINE_ASSERT_MSG(blockSize >= 1, "pool block size must be non-zero");
    usize effective = blockSize;
    if (effective < sizeof(void*)) {
        effective = sizeof(void*);
    }
    // Rounded to a multiple of the alignment so that block i is aligned for
    // every i, not just for i == 0.
    effective   = RoundUpToMultiple(effective, m_alignment);
    m_blockSize = effective;

    if (m_blockCount > 0) {
        m_slab = static_cast<u8*>(
            ::operator new(m_blockSize * m_blockCount, std::align_val_t{m_alignment}));

        // Thread every block onto the free list, front to back, so that the
        // first few allocations come back in address order - which makes a
        // memory dump readable and costs nothing.
        //
        // The LAST block's next-pointer must be null, and the head must point
        // at block 0. Getting either wrong loses exactly one block per cycle,
        // which the provided 100-cycle test catches and a single-pass test
        // does not.
        m_freeHead = m_slab;
        for (usize i = 0; i + 1 < m_blockCount; ++i) {
            u8* current = m_slab + i * m_blockSize;
            u8* next    = m_slab + (i + 1) * m_blockSize;
            std::memcpy(current, &next, sizeof(void*));
        }
        void* terminator = nullptr;
        std::memcpy(m_slab + (m_blockCount - 1) * m_blockSize, &terminator, sizeof(void*));
    }

    MemorySystem::RegisterPool(this);
    ENGINE_LOG_DEBUG(Channels::kMemory,
                     "PoolAllocator '{}': {} blocks x {} bytes (requested {}), align {}",
                     m_name, m_blockCount, m_blockSize, blockSize, m_alignment);
}

PoolAllocator::~PoolAllocator() {
    MemorySystem::UnregisterPool(this);
    ENGINE_LOG_DEBUG(Channels::kMemory, "PoolAllocator '{}' released ({} peak blocks)",
                     m_name, m_peakInUse);
    ::operator delete(m_slab, std::align_val_t{m_alignment});
    m_slab = nullptr;
}

void* PoolAllocator::Allocate() {
    if (m_freeHead == nullptr) {
        return nullptr;   // exhausted. No assert, no growth.
    }

    void* block = m_freeHead;

    // Read the next-free pointer out of the block we are about to hand over,
    // BEFORE the caller can overwrite it. memcpy rather than a cast-and-
    // dereference because the slab is raw bytes and reading a void* through a
    // u8* is a strict-aliasing violation - the kind that works until the
    // optimiser is turned up.
    void* next = nullptr;
    std::memcpy(&next, block, sizeof(void*));
    m_freeHead = next;

    ++m_inUse;
    if (m_inUse > m_peakInUse) {
        m_peakInUse = m_inUse;   // on allocation only
    }
    return block;
}

void PoolAllocator::Free(void* block) {
    if (block == nullptr) {
        return;   // harmless, and tested
    }

    // THE DEBUG OWNERSHIP CHECK. Two comparisons and a modulo, and it converts
    // a whole category of silent heap corruption into a named failure at the
    // moment of the mistake.
#if ENGINE_ASSERTS_ENABLED
    const auto address = reinterpret_cast<std::uintptr_t>(block);
    const auto base    = reinterpret_cast<std::uintptr_t>(m_slab);
    const auto end     = base + m_blockSize * m_blockCount;
    ENGINE_ASSERT_MSG(address >= base && address < end,
                      "PoolAllocator::Free called with a pointer from a different pool");
    ENGINE_ASSERT_MSG(((address - base) % m_blockSize) == 0,
                      "PoolAllocator::Free called with a pointer that is not on a block "
                      "boundary");
    if (address < base || address >= end || ((address - base) % m_blockSize) != 0) {
        return;
    }
#endif

    std::memcpy(block, &m_freeHead, sizeof(void*));
    m_freeHead = block;

    ENGINE_ASSERT_MSG(m_inUse > 0, "PoolAllocator::Free with no blocks in use");
    if (m_inUse > 0) {
        --m_inUse;
    }
}

usize PoolAllocator::BlockSize() const       { return m_blockSize; }
usize PoolAllocator::BlockCount() const      { return m_blockCount; }
usize PoolAllocator::BlocksInUse() const     { return m_inUse; }
usize PoolAllocator::PeakBlocksInUse() const { return m_peakInUse; }
usize PoolAllocator::BytesUsed() const       { return m_inUse * m_blockSize; }
usize PoolAllocator::BytesCapacity() const   { return m_blockCount * m_blockSize; }

usize PoolAllocator::DebugFreeListLength() const {
    usize length = 0;
    void* node   = m_freeHead;
    while (node != nullptr && length <= m_blockCount) {
        void* next = nullptr;
        std::memcpy(&next, node, sizeof(void*));
        node = next;
        ++length;
    }
    return length;
}

} // namespace eng
