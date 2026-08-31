// =============================================================================
//  WEEK 2 - the five seeded bugs, found and fixed. Each fix is marked with the
//  bug number and how it was found. Full table in docs/week02-bug-report.md.
//
//  The class was NOT rewritten from scratch. The skill being trained is
//  reading unfamiliar broken code, and rewriting it skips the exercise.
// =============================================================================

#include <engine/core/ByteBuffer.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace eng {

ByteBuffer::ByteBuffer(usize size)
    : m_size(size) {
    m_data = new u8[size];
    std::memset(m_data, 0, size);
}

ByteBuffer::~ByteBuffer() {
    // ---- BUG 1: `delete m_data` on memory that came from `new u8[]`. --------
    // Mismatched form of delete; undefined behaviour. Found by AddressSanitizer
    // ("alloc-dealloc-mismatch"), NOT by a compiler warning - the compiler
    // cannot see which form of new produced a pointer it was handed.
    // In C#: there is no delete at all, so the question cannot arise.
    delete[] m_data;
    m_data = nullptr;
    m_size = 0;
}

ByteBuffer::ByteBuffer(const ByteBuffer& other) {
    // ---- BUG 2: shallow copy. -----------------------------------------------
    // It assigned `m_data = other.m_data`, so both objects pointed at the same
    // allocation and both destructors freed it - a double free, and until then
    // a mutation through one object was visible through the other. Found by
    // ASan ("double-free"); it crashes reliably, and the provided test
    // "a copied buffer is independent of its source" fails.
    // In C#: a class reference copy is EXPECTED to alias, and there is no
    // destructor racing to free it. C++ copy constructors have to say which
    // one they mean.
    m_size = other.m_size;
    m_data = new u8[m_size];
    if (m_size > 0) {
        std::memcpy(m_data, other.m_data, m_size);
    }
}

ByteBuffer& ByteBuffer::operator=(const ByteBuffer& other) {
    if (this == &other) {
        return *this;
    }
    delete[] m_data;
    m_size = other.m_size;
    m_data = new u8[m_size];
    if (m_size > 0) {
        std::memcpy(m_data, other.m_data, m_size);
    }
    return *this;
}

ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
    : m_data(std::exchange(other.m_data, nullptr)),
      m_size(std::exchange(other.m_size, 0)) {}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept {
    if (this != &other) {
        delete[] m_data;
        m_data = std::exchange(other.m_data, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

void ByteBuffer::Fill(u8 value) {
    // ---- BUG 3: `i <= m_size` wrote one byte past the end. ------------------
    // A classic off-by-one. Found by ASan ("heap-buffer-overflow"). It is the
    // one to discuss: it frequently PASSES the provided test suite, because
    // the single overrun byte lands in allocator padding and nothing observes
    // it. Green suite, corrupt heap.
    // In C#: an array index out of range throws immediately and names itself.
    for (usize i = 0; i < m_size; ++i) {
        m_data[i] = value;
    }
}

bool ByteBuffer::Write(usize offset, const void* src, usize count) {
    // The overflow-safe form of the fit check. `offset + count > m_size` can
    // wrap for absurd inputs; this cannot. Not one of the seeded five, but it
    // is a free fix while the function is open.
    if (offset > m_size || count > m_size - offset) {
        return false;
    }
    if (count == 0 || src == nullptr) {
        return count == 0;
    }
    // ---- BUG 4: `sizeof(src)` instead of `count`. ---------------------------
    // sizeof(src) is the size of a POINTER - 8 bytes on this machine -
    // regardless of how many bytes the caller asked for. Found by the compiler
    // (-Wsizeof-pointer-memaccess with GCC/Clang; MSVC catches this one less
    // reliably) and by the provided test "Write copies the requested number of
    // bytes". This is one of the two the `strict` preset hands you for free.
    // In C#: sizeof on a managed reference is not even legal outside unsafe
    // code, and Array.Copy takes a count with no way to confuse it for a size.
    std::memcpy(m_data + offset, src, count);
    return true;
}

void ByteBuffer::Release() {
    delete[] m_data;
    m_data = nullptr;
    m_size = 0;
}

const char* DescribeBuffer(const ByteBuffer& buffer) {
    // ---- BUG 5: returned the address of a local array. ----------------------
    // `char text[64]` inside this function has automatic storage duration; its
    // lifetime ends at the closing brace, so the returned pointer dangles.
    // Found by the compiler (-Wreturn-local-addr with GCC/Clang, C4172 with
    // MSVC - MSVC catches this one reliably) and by the provided test, which
    // constructs another ByteBuffer between the call and the check in order to
    // reuse that stack memory.
    //
    // The fix is `static thread_local`: the storage now outlives the call.
    // `thread_local` rather than plain `static` because two threads logging
    // simultaneously would otherwise overwrite each other's text - the same
    // shared-mutable-state problem Week 5 spends a week on, in miniature.
    //
    // The cost of this fix, stated honestly and repeated in the header: the
    // returned pointer is only valid until the next call on this thread.
    // Returning a std::string would be better and is what this would be if the
    // provided test suite were not fixed.
    //
    // In C#: a local array is heap-allocated and the GC keeps it alive exactly
    // as long as somebody holds a reference. This class of bug does not exist.
    static thread_local char text[64];
    std::snprintf(text, sizeof(text), "ByteBuffer{ size=%zu }", buffer.Size());
    return text;
}

} // namespace eng
