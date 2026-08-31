#pragma once

// =============================================================================
//  WEEK 8 - hashed string ids. Ch. 6.4.
//
//  THE PROBLEM: strings are the natural way for a human and a data file to
//  name things - "PlayerSprite", "TransformComponent", "Jump". They are also,
//  in a game loop, a disaster: comparing walks characters, copying allocates,
//  and storing scatters your data across the heap. Once per component per
//  entity per frame is exactly what the Week 4 cache-line measurement was
//  about.
//
//  THE FIX: hash once, pass the hash. Comparison is an integer compare,
//  storage is eight bytes, and a debug-only side table keeps logs readable.
//
//  ---------------------------------------------------------------------------
//  HASH WIDTH: 64 BITS, chosen deliberately.
//
//  With 32 bits, the birthday bound says a collision becomes likely at around
//  77,000 distinct strings - which sounds like a lot until you remember that
//  every entity name in every scene file is one. With 64 bits the same
//  threshold is around five billion. The cost is four extra bytes per id, and
//  an id is never stored in the thousands (the things stored in the thousands
//  are components, which hold one id each at most).
//
//  Collisions are still possible, so the debug reverse table ASSERTS when an
//  insert would overwrite a DIFFERENT string with the same hash. That turns an
//  undiagnosable bug into a build-time shout.
//
//  ---------------------------------------------------------------------------
//  COMPILE-TIME HASHING is the property that matters and the only one a test
//  can verify with certainty. tests/src/test_stringid.cpp uses static_assert
//  for exactly that reason: no runtime test can distinguish a hash computed
//  during compilation from one computed at startup.
//
//  The consequence for the design: the CONSTRUCTOR must be constexpr and must
//  not touch the reverse table (which is a std::unordered_map and cannot exist
//  at compile time). Registration is therefore a separate, explicit step -
//  see Intern() and the _sid literal below. Separating the hashing from the
//  debug bookkeeping is the single thing that makes the static_assert compile.
// =============================================================================

#include <engine/core/Assert.h>
#include <engine/core/Types.h>

#include <compare>
#include <string_view>

namespace eng {

namespace detail {

// FNV-1a, 64-bit. Six lines, fully constexpr, and specified by its own
// arithmetic so the value of StringId("Player") is identical on every compiler
// - which matters because ids end up in data files.
//
// CASE SENSITIVE, deliberately: "Player" and "player" are different ids. A
// case-insensitive hash would have to lowercase first, which is either a
// locale question or a wrong-for-non-ASCII shortcut, and component type names
// are written by programmers who can match a capital letter.
inline constexpr u64 kFnvOffsetBasis = 14695981039346656037ull;
inline constexpr u64 kFnvPrime       = 1099511628211ull;

constexpr u64 Fnv1a64(std::string_view text) {
    u64 hash = kFnvOffsetBasis;
    for (char c : text) {
        hash ^= static_cast<u64>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace detail

class StringId {
public:
    constexpr StringId() = default;

    // constexpr, so an id built from a literal costs nothing at runtime.
    constexpr explicit StringId(std::string_view text) : m_value(detail::Fnv1a64(text)) {}

    // For reading an id straight out of a data file that stored the number.
    static constexpr StringId FromValue(u64 value) {
        StringId id;
        id.m_value = value;
        return id;
    }

    constexpr u64 Value() const { return m_value; }

    // THE EMPTY STRING: StringId("") is the FNV offset basis, not zero, and it
    // is therefore distinguishable from a default-constructed id. That is
    // deliberate - "a component whose type name is the empty string" is a
    // data-file error worth telling apart from "nobody set this field".
    constexpr bool IsValid() const { return m_value != 0; }

    // C++20's <=> gives ==, !=, <, <=, >, >= from one line, and makes StringId
    // usable as a map key - which every registry in the engine relies on.
    friend constexpr auto operator<=>(const StringId&, const StringId&) = default;
    friend constexpr bool operator==(const StringId&, const StringId&)  = default;

    // The original text, in a DEBUG build, from the reverse table.
    //
    // In a RELEASE build the table is empty and this returns "<sid:0x...>" -
    // the hash, formatted. NOT an empty string, and not a plausible-looking
    // fake name: a log line reading `component ''` sends you hunting for a
    // data bug that does not exist, whereas `component '<sid:0x8f3a...>'` says
    // "this is a release build, the name was compiled away" to anyone who has
    // read this comment once.
    const char* ToString() const;

private:
    u64 m_value = 0;
};

// Registers `text` in the debug reverse table and returns its id. This is what
// makes ToString() work, and it is a separate function from the constructor
// precisely so that the constructor can stay constexpr.
//
// In a release build it does nothing but hash.
StringId Intern(std::string_view text);

// Look up a hash directly, for the editor's inspectors.
const char* LookupStringId(u64 value);

// Number of interned strings, and the collision count observed so far. The
// second is exposed because "how close am I to the birthday bound" is a
// question worth being able to answer rather than assume.
usize InternedCount();
usize InternCollisionCount();

inline namespace literals {

// A literal suffix, because "Player"_sid gets typed hundreds of times in
// Weeks 9 and 10. consteval rather than constexpr: it may ONLY be called at
// compile time, so a stray runtime use is a compile error rather than a
// silent per-frame hash.
consteval StringId operator""_sid(const char* text, usize length) {
    return StringId(std::string_view(text, length));
}

} // namespace literals

} // namespace eng
