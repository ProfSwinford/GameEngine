// =============================================================================
//  WEEK 9 - PROVIDED. Do not edit.
//
//  The generation trick, expressed as tests. If these pass, stale-handle
//  detection works, which is a Milestone 3 verification item.
//
//  Read this before implementing Handle. It pins down the null-handle
//  convention, which is the detail most often got wrong.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/resource/Handle.h>

using namespace eng;

namespace { struct Thing {}; struct OtherThing {}; }

TEST_CASE("a default-constructed handle is null") {
    // Critical. A handle nobody initialised must be OBVIOUSLY invalid, not
    // accidentally pointing at slot 0 generation 0 - which is a real asset.
    Handle<Thing> h;
    CHECK(h.IsNull());
}

TEST_CASE("index and generation round-trip through packing") {
    Handle<Thing> h = MakeHandle<Thing>(1234, 7);
    CHECK(h.Index() == 1234);
    CHECK(h.Generation() == 7);
    CHECK_FALSE(h.IsNull());
}

TEST_CASE("handles to the same slot in different generations are not equal") {
    // This is the whole mechanism. Slot 5 generation 2 is a DIFFERENT asset
    // from slot 5 generation 3, and the handles must say so.
    Handle<Thing> older = MakeHandle<Thing>(5, 2);
    Handle<Thing> newer = MakeHandle<Thing>(5, 3);
    CHECK(older != newer);
    CHECK(older.Index() == newer.Index());
}

TEST_CASE("identical handles compare equal") {
    CHECK(MakeHandle<Thing>(9, 4) == MakeHandle<Thing>(9, 4));
}

TEST_CASE("the maximum index and generation are representable") {
    // Whatever bit split you chose, the largest values that fit must survive
    // packing. Off-by-one here means your last slot is unusable, which shows
    // up only when a pool fills - i.e. in Phase 2, under load.
    const u32 maxIndex = Handle<Thing>::kMaxIndex;
    const u32 maxGen   = Handle<Thing>::kMaxGeneration;

    Handle<Thing> h = MakeHandle<Thing>(maxIndex, maxGen);
    CHECK(h.Index() == maxIndex);
    CHECK(h.Generation() == maxGen);
    CHECK_FALSE(h.IsNull());
}

// Uncomment this and confirm it FAILS TO COMPILE. That is the point of making
// Handle a template: the compiler refuses to mix asset types even though both
// are just integers at runtime.
//
// TEST_CASE("handles of different types do not interchange") {
//     Handle<Thing> a = MakeHandle<Thing>(1, 1);
//     Handle<OtherThing> b = a;   // must not compile
// }
