// WEEK 8 - the debug reverse table. See StringId.h.
//
// Everything in this file is bookkeeping that exists so logs and inspectors
// can show text. None of it runs at compile time, and none of it is reachable
// from the constructor - which is what allows the constructor to be constexpr
// and the static_assert in the test suite to compile.

#include <engine/core/Assert.h>
#include <engine/core/StringId.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eng {
namespace {

// A function-local static rather than a namespace-scope one: constructed on
// first use, so it cannot be used before it exists no matter what order
// translation units initialise in. That is the static initialization order
// fiasco Week 7 opens with, avoided rather than survived - and it matters
// here because component types intern their names from static registration
// objects that run before main().
struct Table {
    std::mutex                            mutex;
    std::unordered_map<u64, std::string>  entries;
    usize                                 collisions = 0;
};

Table& GetTable() {
    static Table table;
    return table;
}

// Formatted hashes for release builds and for unknown ids. A small ring so
// that two ToString() calls in one std::format argument list do not overwrite
// each other - which they did, exactly once, and it took ten minutes to
// believe.
thread_local char  g_fallback[4][32];
thread_local usize g_fallbackSlot = 0;

const char* FormatFallback(u64 value) {
    char* slot     = g_fallback[g_fallbackSlot];
    g_fallbackSlot = (g_fallbackSlot + 1) % 4;
    std::snprintf(slot, sizeof(g_fallback[0]), "<sid:%016llx>",
                  static_cast<unsigned long long>(value));
    return slot;
}

} // namespace

StringId Intern(std::string_view text) {
    const StringId id(text);

#if ENGINE_ASSERTS_ENABLED
    Table& table = GetTable();
    std::lock_guard<std::mutex> lock(table.mutex);

    const auto it = table.entries.find(id.Value());
    if (it == table.entries.end()) {
        table.entries.emplace(id.Value(), std::string(text));
    } else if (it->second != text) {
        // A genuine 64-bit FNV collision between two strings a human typed.
        // Vanishingly unlikely, and if it ever fires the fix is to rename one
        // of them - which is only possible because this shouts instead of
        // silently aliasing two component types onto one id.
        ++table.collisions;
        ENGINE_ASSERT_MSG(false, "StringId hash collision between two different strings");
    }
#endif

    return id;
}

const char* StringId::ToString() const {
#if ENGINE_ASSERTS_ENABLED
    Table& table = GetTable();
    std::lock_guard<std::mutex> lock(table.mutex);
    const auto it = table.entries.find(m_value);
    if (it != table.entries.end()) {
        return it->second.c_str();
    }
#endif
    // Release build, or an id that was never interned. Honest and cheap: the
    // hash itself, formatted, rather than an empty string that would read as a
    // valid name with nothing in it.
    return FormatFallback(m_value);
}

const char* LookupStringId(u64 value) {
    return StringId::FromValue(value).ToString();
}

usize InternedCount() {
#if ENGINE_ASSERTS_ENABLED
    Table& table = GetTable();
    std::lock_guard<std::mutex> lock(table.mutex);
    return table.entries.size();
#else
    return 0;
#endif
}

usize InternCollisionCount() {
#if ENGINE_ASSERTS_ENABLED
    Table& table = GetTable();
    std::lock_guard<std::mutex> lock(table.mutex);
    return table.collisions;
#else
    return 0;
#endif
}

} // namespace eng
