// WEEK 7 - the stack allocator. See StackAllocator.h for the alignment
// arithmetic worked through on paper.

#include <engine/core/Assert.h>
#include <engine/core/Log.h>
#include <engine/memory/MemorySystem.h>
#include <engine/memory/StackAllocator.h>

#include <cstdint>
#include <new>

namespace eng {
namespace {

constexpr bool IsPowerOfTwo(usize value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// Round an ADDRESS up. Not a size. See the header.
constexpr std::uintptr_t AlignUp(std::uintptr_t address, usize alignment) {
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment) - 1u;
    return (address + mask) & ~mask;
}

} // namespace

StackAllocator::StackAllocator(usize capacity, const char* name)
    : m_capacity(capacity), m_name(name != nullptr ? name : "stack") {
    if (capacity > 0) {
        // THE ONLY heap allocation this object ever makes. Over-aligned to
        // max_align_t so that the base is already suitable for any scalar
        // type and the first Allocate does not have to skip padding it did not
        // budget for.
        m_base = static_cast<u8*>(::operator new(capacity, std::align_val_t{
            alignof(std::max_align_t)}));
    }
    MemorySystem::RegisterStack(this);
    ENGINE_LOG_DEBUG(Channels::kMemory, "StackAllocator '{}' reserved {} bytes", m_name,
                     capacity);
}

StackAllocator::~StackAllocator() {
    MemorySystem::UnregisterStack(this);
    ENGINE_LOG_DEBUG(Channels::kMemory, "StackAllocator '{}' released ({} peak bytes)",
                     m_name, m_peak);
    ::operator delete(m_base, std::align_val_t{alignof(std::max_align_t)});
    m_base = nullptr;
}

void* StackAllocator::Allocate(usize size, usize alignment) {
    ENGINE_ASSERT_MSG(IsPowerOfTwo(alignment), "alignment must be a power of two");
    if (!IsPowerOfTwo(alignment) || m_base == nullptr) {
        return nullptr;
    }

    // Align the ADDRESS the cursor currently points at, then convert back to an
    // offset. Doing the arithmetic on the real address matters: aligning the
    // offset alone would only be correct if the base itself were aligned to at
    // least `alignment`, and for alignment 64 on a 16-aligned base it is not.
    const auto  baseAddress    = reinterpret_cast<std::uintptr_t>(m_base);
    const auto  currentAddress = baseAddress + m_cursor;
    const auto  alignedAddress = AlignUp(currentAddress, alignment);
    const usize alignedOffset  = static_cast<usize>(alignedAddress - baseAddress);

    // Overflow-safe capacity test. `alignedOffset + size > m_capacity` can wrap
    // for an absurd size and would then hand out a pointer past the end.
    if (alignedOffset > m_capacity || size > m_capacity - alignedOffset) {
        return nullptr;   // told no, still usable
    }

    m_cursor = alignedOffset + size;
    if (m_cursor > m_peak) {
        m_peak = m_cursor;   // updated on allocation only - never on free
    }
    ++m_allocations;

    return m_base + alignedOffset;
}

StackAllocator::Marker StackAllocator::GetMarker() const {
    return m_cursor;
}

void StackAllocator::FreeToMarker(Marker marker) {
    ENGINE_ASSERT_MSG(marker <= m_cursor,
                      "StackAllocator::FreeToMarker to a marker already rewound past");
    if (marker > m_cursor) {
        return;
    }
    m_cursor = marker;
    // m_peak deliberately untouched.
}

void StackAllocator::Clear() {
    m_cursor = 0;
}

usize StackAllocator::BytesUsed() const       { return m_cursor; }
usize StackAllocator::BytesCapacity() const   { return m_capacity; }
usize StackAllocator::AllocationCount() const { return m_allocations; }
usize StackAllocator::PeakBytes() const       { return m_peak; }

} // namespace eng
