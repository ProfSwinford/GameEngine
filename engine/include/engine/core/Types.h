#pragma once

// =============================================================================
//  Fixed-width types and a few aliases used across the whole engine.
//
//  Coming from C#: `int` in C# is always 32 bits. In C++ it is "at least 16,
//  usually 32, and the standard will not promise you more than that." When the
//  width matters - and in an engine it usually does - say the width.
//
//  WEEK 4 NOTE (sizeof audit): this file is why the audit numbers are
//  reproducible between machines for the members themselves. It is NOT why the
//  struct totals are reproducible - alignment and padding are properties of
//  the ABI, not of the member widths, which is exactly the gap the audit is
//  about.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace eng {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

} // namespace eng
