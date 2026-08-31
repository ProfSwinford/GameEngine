// =============================================================================
//  WEEK 8 - PARTIALLY PROVIDED, now filled in. The compile-time case was given
//  because it verifies the property that actually matters.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/core/Assert.h>
#include <engine/core/StringId.h>

#include <string>

using namespace eng;

TEST_CASE("hashing happens at compile time") {
    // If this compiles, the hash ran during COMPILATION and costs nothing at
    // runtime. If it does not compile, the hash function is not constexpr, and
    // every StringId in the engine is doing string work in the game loop.
    //
    // A static_assert is the ONLY way to verify this. No runtime test can tell
    // the difference between a hash computed during compilation and one
    // computed at startup - which is why the separation between the constexpr
    // constructor and the reverse-table Intern() exists at all.
    static_assert(StringId("Player").Value() != 0);
    static_assert(StringId("Player").Value() == StringId("Player").Value());
    static_assert(StringId("Player").Value() != StringId("Enemy").Value());
    CHECK(true);
}

TEST_CASE("the literal suffix is also compile time") {
    using namespace eng::literals;
    // consteval, so this CANNOT compile as a runtime call - a stray runtime
    // use is a build error rather than a silent per-frame hash.
    static_assert("Player"_sid.Value() == StringId("Player").Value());
    CHECK(true);
}

TEST_CASE("equal text produces equal ids") {
    // Including an id built at RUNTIME from a std::string, which must equal one
    // built at compile time from the same characters. If these ever disagreed,
    // a component named in a scene file would not match the same component
    // named in C++ - which is the whole mechanism.
    const std::string runtimeText = std::string("Trans") + "formComponent";
    const StringId fromRuntime(runtimeText);
    const StringId fromLiteral("TransformComponent");

    CHECK(fromRuntime == fromLiteral);
    CHECK(fromRuntime.Value() == fromLiteral.Value());
}

TEST_CASE("different text produces different ids") {
    // The near-misses. CASE SENSITIVITY IS A DECISION, documented in StringId.h
    // and pinned down here: "Player" and "player" are DIFFERENT ids.
    CHECK(StringId("Player") != StringId("player"));
    CHECK(StringId("Player") != StringId("Player "));   // trailing space matters
    CHECK(StringId("Player") != StringId("Playe"));
    CHECK(StringId("Player") != StringId("PlayerX"));
    // Order matters too, which a naive additive hash would get wrong.
    CHECK(StringId("ab") != StringId("ba"));
}

TEST_CASE("an empty string is handled sanely") {
    // StringId("") is the FNV offset basis, NOT zero - so it is
    // distinguishable from a default-constructed id. That is deliberate: "a
    // component whose type name is the empty string" is a data-file error
    // worth telling apart from "nobody set this field".
    const StringId empty("");
    const StringId defaulted;

    CHECK(empty.Value() != 0);
    CHECK(defaulted.Value() == 0);
    CHECK(empty != defaulted);
    CHECK_FALSE(defaulted.IsValid());
    CHECK(empty.IsValid());
}

TEST_CASE("ids are ordered, so they can be map keys") {
    // C++20's <=> gives all six comparisons from one line, and every registry
    // in the engine relies on the ordering.
    const StringId a("aaa");
    const StringId b("bbb");
    CHECK(((a < b) || (b < a)));      // strictly ordered one way or the other
    CHECK_FALSE(a < a);
    CHECK(a <= a);
}

TEST_CASE("FromValue round-trips a raw hash") {
    const StringId original("SpriteComponent");
    const StringId rebuilt = StringId::FromValue(original.Value());
    CHECK(rebuilt == original);
}

TEST_CASE("reverse lookup returns the original text in debug builds") {
    const StringId id = Intern("ReverseLookupProbe");

#if ENGINE_ASSERTS_ENABLED
    CHECK(std::string(id.ToString()) == "ReverseLookupProbe");
#else
    // In a release build the table is empty and ToString returns the hash,
    // formatted - NOT an empty string, which would read as a valid name with
    // nothing in it.
    const std::string text = id.ToString();
    CHECK(text.starts_with("<sid:"));
    CHECK_FALSE(text.empty());
#endif
}

TEST_CASE("an id that was never interned still stringifies honestly") {
    // Never passed through Intern, so it is not in the table even in debug.
    const StringId id("NeverInternedAnywhere_qx91");
    const std::string text = id.ToString();
    CHECK_FALSE(text.empty());
    CHECK(text.starts_with("<sid:"));
}

TEST_CASE("interning the same text twice does not report a collision") {
    const usize before = InternCollisionCount();
    Intern("RepeatedInternProbe");
    Intern("RepeatedInternProbe");
    CHECK(InternCollisionCount() == before);
}
