// =============================================================================
//  WEEK 2 - written BEFORE the implementations in MemUtil.cpp.
//
//  Not as ritual. Writing these is what forced the decisions the header now
//  documents: what ReverseBytes(nullptr, 0) returns, what CountBytes does with
//  a null pointer, whether a zero count is an error. Deciding that up front
//  was most of the work; the loops took ten minutes.
//
//  Coverage per the lab requirements: the ordinary case, a null pointer where
//  the contract mentions one, a zero count, and - for CopyOverlapping -
//  forward and backward overlap as separate cases, because they need loops
//  running in opposite directions and getting one right does not get you the
//  other.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/core/MemUtil.h>

#include <cstring>
#include <vector>

using namespace eng;

TEST_CASE("Accumulate adds into the target in place") {
    Vec2 target{1.0f, 2.0f};
    const Vec2 rhs{10.0f, 20.0f};

    Accumulate(target, rhs);

    CHECK(target.x == doctest::Approx(11.0f));
    CHECK(target.y == doctest::Approx(22.0f));
    CHECK(rhs.x    == doctest::Approx(10.0f));   // source unchanged
}

TEST_CASE("Accumulate is additive, so calling it twice doubles the delta") {
    Vec2 target{0.0f, 0.0f};
    const Vec2 rhs{1.5f, -2.5f};
    Accumulate(target, rhs);
    Accumulate(target, rhs);
    CHECK(target.x == doctest::Approx(3.0f));
    CHECK(target.y == doctest::Approx(-5.0f));
}

// --- SwapI32 ----------------------------------------------------------------

TEST_CASE("SwapI32 exchanges two values") {
    i32 a = 3;
    i32 b = -9;
    SwapI32(a, b);
    CHECK(a == -9);
    CHECK(b == 3);
}

TEST_CASE("SwapI32 with the same variable on both sides leaves it unchanged") {
    // Self-swap through two references to one object. The naive
    // temp-based implementation handles it; an XOR-based one famously does
    // not, which is why the case is here rather than assumed.
    i32 value = 42;
    SwapI32(value, value);
    CHECK(value == 42);
}

// --- ReverseBytes -----------------------------------------------------------

TEST_CASE("ReverseBytes reverses an ordinary range") {
    u8 data[] = {1, 2, 3, 4, 5, 6};
    REQUIRE(ReverseBytes(data, 6));
    const u8 expected[] = {6, 5, 4, 3, 2, 1};
    CHECK(std::memcmp(data, expected, 6) == 0);
}

TEST_CASE("ReverseBytes handles an odd count, leaving the middle byte alone") {
    u8 data[] = {1, 2, 3, 4, 5};
    REQUIRE(ReverseBytes(data, 5));
    const u8 expected[] = {5, 4, 3, 2, 1};
    CHECK(std::memcmp(data, expected, 5) == 0);
}

TEST_CASE("ReverseBytes with a count of 1 is a no-op and succeeds") {
    u8 data[] = {7};
    CHECK(ReverseBytes(data, 1));
    CHECK(data[0] == 7);
}

TEST_CASE("ReverseBytes with a count of 0 is not an error") {
    // The decision the contract records: reversing nothing SUCCEEDS. It does
    // not return false, because nothing went wrong.
    u8 data[] = {1, 2, 3};
    CHECK(ReverseBytes(data, 0));
    CHECK(data[0] == 1);   // untouched
}

TEST_CASE("ReverseBytes with a null pointer returns false") {
    CHECK_FALSE(ReverseBytes(nullptr, 8));
    // Null with a zero count is STILL false: the pointer is the invalid part,
    // and reporting success for a null pointer would let a caller believe a
    // range existed.
    CHECK_FALSE(ReverseBytes(nullptr, 0));
}

// --- CountBytes -------------------------------------------------------------

TEST_CASE("CountBytes counts matching bytes in an ordinary range") {
    const u8 data[] = {1, 2, 1, 3, 1, 4};
    CHECK(CountBytes(data, 6, 1) == 3);
    CHECK(CountBytes(data, 6, 2) == 1);
}

TEST_CASE("CountBytes returns 0 when the value is not present") {
    const u8 data[] = {1, 2, 3};
    CHECK(CountBytes(data, 3, 9) == 0);
}

TEST_CASE("CountBytes respects the count and does not read past it") {
    const u8 data[] = {1, 1, 1, 1};
    CHECK(CountBytes(data, 2, 1) == 2);
    CHECK(CountBytes(data, 0, 1) == 0);
}

TEST_CASE("CountBytes with a null pointer returns 0") {
    // "No matches" is the honest answer for a range that does not exist, and
    // there is no error channel on a usize return - which is a design
    // trade-off the contract states rather than hides.
    CHECK(CountBytes(nullptr, 10, 1) == 0);
}

// --- CopyOverlapping --------------------------------------------------------

TEST_CASE("CopyOverlapping copies non-overlapping ranges") {
    u8 source[] = {1, 2, 3, 4};
    u8 destination[4] = {};
    REQUIRE(CopyOverlapping(destination, source, 4));
    CHECK(std::memcmp(destination, source, 4) == 0);
}

TEST_CASE("CopyOverlapping handles a FORWARD overlap (dst > src)") {
    // Shifting right inside one buffer. A naive forward loop overwrites source
    // bytes before reading them and smears the first byte across the range,
    // which is exactly what std::memcpy is permitted to do here and what
    // std::memmove promises not to.
    u8 buffer[] = {1, 2, 3, 4, 5, 0, 0};
    REQUIRE(CopyOverlapping(buffer + 2, buffer, 5));
    const u8 expected[] = {1, 2, 1, 2, 3, 4, 5};
    CHECK(std::memcmp(buffer, expected, 7) == 0);
}

TEST_CASE("CopyOverlapping handles a BACKWARD overlap (dst < src)") {
    // Shifting left inside one buffer. This is the case a backward loop gets
    // wrong, which is why it is a separate test: getting one direction right
    // does not get you the other.
    u8 buffer[] = {0, 0, 1, 2, 3, 4, 5};
    REQUIRE(CopyOverlapping(buffer, buffer + 2, 5));
    const u8 expected[] = {1, 2, 3, 4, 5, 4, 5};
    CHECK(std::memcmp(buffer, expected, 7) == 0);
}

TEST_CASE("CopyOverlapping with identical pointers succeeds and changes nothing") {
    u8 buffer[] = {1, 2, 3};
    REQUIRE(CopyOverlapping(buffer, buffer, 3));
    const u8 expected[] = {1, 2, 3};
    CHECK(std::memcmp(buffer, expected, 3) == 0);
}

TEST_CASE("CopyOverlapping with a count of 0 succeeds and copies nothing") {
    u8 source[] = {9};
    u8 destination[] = {1};
    CHECK(CopyOverlapping(destination, source, 0));
    CHECK(destination[0] == 1);
}

TEST_CASE("CopyOverlapping with either pointer null returns false") {
    u8 buffer[] = {1, 2, 3};
    CHECK_FALSE(CopyOverlapping(nullptr, buffer, 3));
    CHECK_FALSE(CopyOverlapping(buffer, nullptr, 3));
    CHECK_FALSE(CopyOverlapping(nullptr, nullptr, 0));
}

TEST_CASE("CopyOverlapping matches memmove on a range of overlaps") {
    // A sweep rather than three hand-picked cases: every offset from -8 to +8
    // inside one buffer, compared against std::memmove, which is the behaviour
    // being reimplemented. This is the case that would have caught an
    // off-by-one in either loop direction.
    constexpr usize kSize = 32;
    for (int offset = -8; offset <= 8; ++offset) {
        std::vector<u8> mine(kSize);
        std::vector<u8> reference(kSize);
        for (usize i = 0; i < kSize; ++i) {
            mine[i]      = static_cast<u8>(i);
            reference[i] = static_cast<u8>(i);
        }

        const usize count = 12;
        const usize from  = 10;
        const usize to    = static_cast<usize>(static_cast<int>(from) + offset);

        REQUIRE(CopyOverlapping(mine.data() + to, mine.data() + from, count));
        std::memmove(reference.data() + to, reference.data() + from, count);

        CHECK(std::memcmp(mine.data(), reference.data(), kSize) == 0);
    }
}
