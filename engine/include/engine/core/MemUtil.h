#pragma once

// =============================================================================
//  WEEK 2 - pointer fluency, in the form of functions the engine keeps.
//
//  The tests in tests/src/test_memutil.cpp were written BEFORE these
//  implementations. Not as ritual: writing the test is what forced the
//  decision about what ReverseBytes(nullptr, 0) does, and deciding that up
//  front was most of the work.
//
//  WEEK 6 UPDATE: the placeholder Vec2 that used to live in this header is
//  gone. It has been replaced by the real, fully unit-tested one in
//  <engine/math/Vec2.h>, which is included below so that Accumulate still
//  compiles and the Week 2 test still passes unchanged. It did pass unchanged,
//  which is the check the Week 6 patch asks for.
// =============================================================================

#include <engine/core/Types.h>
#include <engine/math/Vec2.h>

namespace eng {

// Adds `rhs` into `target`, modifying `target` in place.
//
// The signature question, answered: `target` is a non-const reference because
// it is the output; `rhs` is a const reference because it is an input that is
// not modified and copying it, while cheap here, is a habit that stops being
// cheap the moment the type grows. In C# the choice is made for you by whether
// the type is a class or a struct; in C++ you choose per parameter, every time.
void Accumulate(Vec2& target, const Vec2& rhs);

// Swaps the contents of two i32.
//
// What would have to change to make this work for Vec2 as well: the type would
// have to become a parameter, which means a TEMPLATE. Week 8 gives the real
// answer, and the standard library already has it as std::swap. This exists so
// the reference-parameter mechanics are typed once by hand.
void SwapI32(i32& a, i32& b);

// Reverses `count` bytes starting at `data`, in place.
// Returns false if `data` is null. A count of 0 is NOT an error and returns
// true - reversing nothing succeeds.
bool ReverseBytes(u8* data, usize count);

// Returns the number of bytes equal to `value` in [data, data + count).
// Returns 0 for a null pointer - "no matches" is the honest answer for a range
// that does not exist, and there is no error channel on a usize return.
usize CountBytes(const u8* data, usize count, u8 value);

// Copies `count` bytes from `src` to `dst`, correctly, EVEN WHEN THE TWO
// RANGES OVERLAP.
//
// std::memcpy explicitly does not promise anything if the ranges overlap;
// std::memmove does, and behaves as if the source were first copied to a
// temporary. This is memmove's guarantee, implemented by hand, because the
// exercise is the pointer arithmetic and the direction of the loop:
//
//   dst < src  -> copy FORWARD;  by the time we overwrite a source byte we
//                                have already read it.
//   dst > src  -> copy BACKWARD; same argument, mirrored.
//   dst == src -> nothing to do.
//
// Returns false if either pointer is null. A count of 0 returns true.
bool CopyOverlapping(u8* dst, const u8* src, usize count);

} // namespace eng
