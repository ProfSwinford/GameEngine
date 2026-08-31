#pragma once

// =============================================================================
//  WEEK 9 - handles. Ch. 7.2.
//
//  *** HANDLES ARE MANDATORY, NOT STYLISTIC. ***
//
//  1. A raw pointer to a released asset is a use-after-free, and it is silent.
//     A handle can be CHECKED, and a stale one is reported rather than
//     dereferenced - a Milestone 3 verification item.
//
//  2. Next semester's C# gameplay layer CANNOT HOLD A RAW C++ POINTER across a
//     garbage collection. The GC moves things. An integer survives anything.
//     Retrofitting handles later means rewriting every system that touches an
//     asset, and by then that is every system there is.
//
//  ---------------------------------------------------------------------------
//  THE GENERATION TRICK, which is the whole idea and is about fifteen lines.
//
//  A handle packs an INDEX into a dense array and a GENERATION counter. The
//  manager keeps a parallel array of generations and increments a slot's
//  generation when it is freed. A handle is valid iff its generation still
//  matches the one in its slot.
//
//  Slot 7 generation 3 is released, generation becomes 4, the slot is reused
//  by a different asset. Any old handle saying "slot 7 generation 3" no longer
//  matches - detected in O(1), with no per-handle bookkeeping, no reference
//  cycles, and nothing to scan.
//
//  ---------------------------------------------------------------------------
//  *** THE BIT SPLIT, CHOSEN DELIBERATELY, WITH ITS CONSEQUENCES: ***
//
//      32 bits total = 20 index bits + 12 generation bits.
//
//    - 20 index bits  -> 1,048,575 live objects of one type at once. A 2D game
//      that exceeds a million simultaneous entities has a design problem, not
//      a handle problem.
//
//    - 12 generation bits -> a slot may be reused 4,095 times before its
//      counter WRAPS and a very old handle starts looking valid again.
//
//  The wrap is the interesting half, so state the risk honestly: at a
//  worst-case one create-and-destroy per slot per frame at 60 Hz, a slot wraps
//  after roughly 68 seconds. That is NOT hypothetical for a bullet pool.
//
//  What makes it survivable here: the manager allocates the LOWEST FREE index
//  only after exhausting fresh ones, so reuse is spread across the whole slot
//  array rather than hammering slot 0; and 4,095 reuses of a specific slot
//  require something to be holding a specific stale handle across all of them,
//  which nothing in this engine does. Wrapping is not a crash - it is a bug
//  that appears after hours of play, which is worse - so it is written down
//  here and it is the first thing to widen if Phase 2 grows a bullet hell.
//  Moving to a 64-bit handle with 32/32 makes the problem disappear and costs
//  four bytes per reference.
//
//  NULL: the default-constructed handle has value 0, which is index 0
//  generation 0. Generations START AT 1, so no live object is ever
//  generation 0 and slot 0 is a perfectly ordinary usable slot. A handle
//  nobody initialised is therefore obviously invalid rather than accidentally
//  pointing at a real asset - which is the detail this most often gets wrong.
// =============================================================================

#include <engine/core/Types.h>

#include <compare>

namespace eng {

// WHY THIS IS A TEMPLATE ON T:
//
// So that a Handle<Texture> cannot be passed where a Handle<Sound> is
// expected. Both are a u32 at runtime and the distinction costs literally
// nothing - no storage, no branch, no indirection - but the compiler will
// refuse to mix them. It is the type system doing free work, and it is the
// sort of thing C++ is genuinely good at. It is also the first thing that
// looks like pointless ceremony and gets simplified away; it is not, and it
// should not be.
template <typename T>
struct Handle {
    static constexpr u32 kIndexBits      = 20;
    static constexpr u32 kGenerationBits = 12;

    static constexpr u32 kMaxIndex      = (1u << kIndexBits) - 1u;        // 1048575
    static constexpr u32 kMaxGeneration = (1u << kGenerationBits) - 1u;   // 4095

    u32 value = 0;

    constexpr u32  Index() const      { return value & kMaxIndex; }
    constexpr u32  Generation() const { return (value >> kIndexBits) & kMaxGeneration; }
    constexpr bool IsNull() const     { return value == 0; }

    // C++20: one declaration gives ==, !=, <, <=, >, >= and makes the type
    // usable as a std::map key and in std::set, which the collision system's
    // pair tracking relies on.
    friend constexpr auto operator<=>(const Handle&, const Handle&) = default;
    friend constexpr bool operator==(const Handle&, const Handle&)  = default;
};

template <typename T>
constexpr Handle<T> MakeHandle(u32 index, u32 generation) {
    Handle<T> handle;
    handle.value = ((generation & Handle<T>::kMaxGeneration) << Handle<T>::kIndexBits) |
                   (index & Handle<T>::kMaxIndex);
    return handle;
}

} // namespace eng
