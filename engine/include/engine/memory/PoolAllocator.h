#pragma once

// =============================================================================
//  WEEK 7 - the pool allocator. Ch. 6.2.
//
//  Fixed-size blocks from a pre-allocated slab. Every block is the same size,
//  so there is no searching and no fragmentation - a freed block is exactly
//  the right shape for the next request, always.
//
//  Unlike the stack allocator, blocks are freed INDIVIDUALLY AND IN ANY ORDER.
//  That is what makes it right for objects with independent lifetimes:
//  particles, entities, components, resource records.
//
//  ---------------------------------------------------------------------------
//  THE FREE LIST TRICK, worth appreciating rather than copying.
//
//  A free block is, by definition, memory nobody is using. So the pointer to
//  the NEXT free block is stored INSIDE the free block itself. The list of
//  free blocks costs zero extra memory, because it lives in the holes.
//
//  Allocate = take the head. Free = push onto the head. Neither searches
//  anything; both are a couple of instructions.
//
//  CONSEQUENCE: the block size cannot be smaller than a pointer, because a
//  free block has to be able to hold one. That is asserted at construction
//  rather than discovered as memory corruption at 2am - and it is why
//  BlockSize() may report more than you asked for.
//
//  ---------------------------------------------------------------------------
//  THE DEBUG OWNERSHIP CHECK.
//
//  Free() asserts in debug that the pointer came from this pool and lands
//  exactly on a block boundary. It costs two comparisons and a modulo per
//  free, and it turns "freed the wrong pointer" from silent heap corruption
//  into an immediate, named failure. It has caught more real bugs than
//  anything else written in Week 7.
//
//  Worth being precise about what a sanitizer can and cannot see here: blocks
//  are handed out from INSIDE one big heap allocation, so an overrun from one
//  block into the next is invisible to ASan - that memory legitimately belongs
//  to the process. An overrun past the end of the whole slab IS visible.
//  Knowing which of your bugs the tool can catch is part of this week.
// =============================================================================

#include <engine/core/Types.h>

#include <cstddef>

namespace eng {

class PoolAllocator {
public:
    // `blockSize` is rounded up to at least sizeof(void*) and then up to a
    // multiple of `alignment`, so that CONSECUTIVE blocks all land on aligned
    // addresses - aligning only the first one is the classic near-miss, and
    // the provided suite checks every block, not just block zero.
    PoolAllocator(usize blockSize, usize blockCount,
                  usize alignment = alignof(std::max_align_t),
                  const char* name = "pool");
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Pops a block, or nullptr when exhausted. No assert, no growth.
    void* Allocate();

    // Pushes a block back onto the free list. Freeing nullptr is harmless and
    // is explicitly tested - gameplay code releases optional things.
    void Free(void* block);

    usize BlockSize() const;        // the EFFECTIVE size, after rounding
    usize BlockCount() const;
    usize BlocksInUse() const;
    usize PeakBlocksInUse() const;
    usize BytesUsed() const;
    usize BytesCapacity() const;

    const char* Name() const { return m_name; }

    // Debug-only: walks the free list and returns its length. Used by the
    // engine's own assert that (free list length + blocks in use) == block
    // count, which is how a free list that loses or duplicates a node gets
    // caught on the cycle it happens rather than 90 cycles later.
    usize DebugFreeListLength() const;

private:
    u8*         m_slab       = nullptr;
    void*       m_freeHead   = nullptr;
    usize       m_blockSize  = 0;
    usize       m_blockCount = 0;
    usize       m_inUse      = 0;
    usize       m_peakInUse  = 0;
    usize       m_alignment  = alignof(std::max_align_t);
    const char* m_name       = "pool";
};

} // namespace eng
