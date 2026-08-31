#pragma once

// =============================================================================
//  WEEK 2 - the seeded-bug module, now FIXED. All five defects found; the
//  writeup with "how I found it" per bug is in docs/week02-bug-report.md.
//
//  Bug 2 lived in this header's copy constructor (declared here, defined in
//  the .cpp). Bugs 1, 3, 4 and 5 lived in ByteBuffer.cpp.
//
//  It is a real engine component now and it is kept. Week 9's file reads land
//  in one of these.
// =============================================================================

#include <engine/core/Types.h>

namespace eng {

// A fixed-size block of raw bytes that owns its own storage.
class ByteBuffer {
public:
    explicit ByteBuffer(usize size);
    ~ByteBuffer();

    ByteBuffer(const ByteBuffer& other);
    ByteBuffer& operator=(const ByteBuffer& other);

    // Move operations. Not part of the seeded-bug exercise, but once the copy
    // is a genuine deep copy, moving a buffer into a container instead of
    // copying it is the difference between one allocation and two - and Week 9
    // moves file contents around constantly.
    ByteBuffer(ByteBuffer&& other) noexcept;
    ByteBuffer& operator=(ByteBuffer&& other) noexcept;

    // Raw access. Both overloads exist so that a const ByteBuffer hands out a
    // const pointer - that is the const-correctness half of Week 2's reading.
    u8*       Data()       { return m_data; }
    const u8* Data() const { return m_data; }

    usize Size() const { return m_size; }

    // Set every byte to `value`.
    void Fill(u8 value);

    // Copy `count` bytes from `src` into this buffer at `offset`.
    // Returns false and copies nothing if the range would not fit.
    bool Write(usize offset, const void* src, usize count);

    // Release the storage early. After this, Size() is 0 and Data() is null.
    // Safe to call twice.
    void Release();

private:
    u8*   m_data = nullptr;
    usize m_size = 0;
};

// Returns a human-readable one-line description of the buffer, for logging.
//
//  *** BUG 5 LIVED IN THE DEFINITION OF THIS FUNCTION. *** It returned the
//  address of a function-local `char text[64]`, which is dangling the instant
//  the function returns. The provided test allocates a second ByteBuffer
//  between the call and the check specifically to reuse that stack memory and
//  turn a latent bug into a visible one.
//
//  The signature is fixed by the provided test suite (which must not be
//  edited), so the fix is a storage-duration fix rather than a signature
//  change: see ByteBuffer.cpp. The contract that comes with it, stated here
//  because callers need to know it:
//
//      The returned pointer is valid until the NEXT call to DescribeBuffer on
//      the same thread. Copy it if you need to keep it.
//
//  A std::string return would be strictly better and is what this would be in
//  new code.
const char* DescribeBuffer(const ByteBuffer& buffer);

} // namespace eng
