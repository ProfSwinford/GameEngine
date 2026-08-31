#pragma once

// =============================================================================
//  WEEK 7 - the stack allocator. Ch. 6.2.
//
//  Hands out memory by moving a pointer forward. Frees by moving that pointer
//  BACK to a previously taken marker, releasing everything allocated since,
//  all at once.
//
//  That is the entire data structure - about twenty lines. What makes it worth
//  a week is what it buys:
//    - allocation is a pointer add, not a heap search
//    - it cannot fragment
//    - the cost is deterministic, which matters when you have 16 ms
//    - freeing a thousand objects costs the same as freeing one
//
//  And what it costs: you cannot free an individual allocation. Memory comes
//  back in the reverse order it went out, or not at all. That constraint is
//  the price, and it is why an engine has several allocators rather than one.
//
//  ---------------------------------------------------------------------------
//  ALIGNMENT - the part that is easy to get subtly wrong.
//
//  *** ROUND THE ADDRESS, NOT THE SIZE. ***
//
//  Work one case through on paper: alignment 32, cursor at offset 5, base
//  address 0x1000. The address is 0x1005. The next multiple of 32 at or above
//  it is 0x1020, so 27 bytes of padding are skipped and the block starts at
//  0x1020. Rounding the SIZE up to 32 instead would hand out 0x1005, which is
//  misaligned - and the provided suite deliberately makes a 1-byte allocation
//  before each aligned request so that an implementation which is only
//  accidentally aligned cannot pass by luck.
//
//  The formula, for a power-of-two alignment:
//      aligned = (address + alignment - 1) & ~(alignment - 1)
//
//  Alignment is always a power of two. That is asserted rather than trusted,
//  because the formula above silently produces nonsense otherwise.
//
//  ---------------------------------------------------------------------------
//  THE HEAP BLOCK IS TAKEN ONCE, IN THE CONSTRUCTOR.
//
//  Week 5 measured first-touch page fault cost at roughly 0.9 microseconds per
//  4 KB page on this machine. A 1 MB slab is 256 pages, so touching it for the
//  first time costs about 230 microseconds - 1.4% of a 60 Hz frame, in one
//  lump. An engine pays that at startup and never mid-frame, and that sentence
//  is the entire practical content of the Week 5 measurement.
// =============================================================================

#include <engine/core/Types.h>

#include <cstddef>

namespace eng {

class StackAllocator {
public:
    // An offset from the base, not a pointer. That makes a marker copyable,
    // comparable and printable, and it survives the allocator being moved -
    // which a raw pointer would not.
    using Marker = usize;

    explicit StackAllocator(usize capacity, const char* name = "stack");
    ~StackAllocator();

    StackAllocator(const StackAllocator&)            = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    // Returns `size` bytes aligned to `alignment`, or NULLPTR if there is not
    // enough room.
    //
    // Returning nullptr is the required behaviour: no assert, no throw, no
    // growth. A caller must be able to ask and be told no - and the allocator
    // must remain usable afterwards, which the provided suite checks by making
    // a smaller request straight after a refused one.
    void* Allocate(usize size, usize alignment = alignof(std::max_align_t));

    Marker GetMarker() const;

    // Releases everything allocated since `marker`. Rewinding to a marker you
    // have already rewound past is a programmer error and asserts.
    void FreeToMarker(Marker marker);

    void Clear();

    // --- Ch. 10.9 instrumentation. Counters, not a subsystem. ---------------
    usize BytesUsed() const;
    usize BytesCapacity() const;
    usize AllocationCount() const;

    // PEAK IS THE NUMBER THAT EARNS ITS KEEP. It answers "how big does this
    // buffer actually need to be", which is a question Phase 2 will ask and
    // which nothing else can answer.
    //
    // It updates on allocation and NEVER on free - a peak that comes back down
    // is a current level with a longer name. The provided suite has a case
    // requiring that it survive a rewind, because the naive implementation
    // forgets.
    usize PeakBytes() const;

    const char* Name() const { return m_name; }

private:
    u8*         m_base     = nullptr;
    usize       m_capacity = 0;
    usize       m_cursor   = 0;
    usize       m_peak     = 0;
    usize       m_allocations = 0;
    const char* m_name     = "stack";
};

} // namespace eng
