// WEEK 2 - see MemUtil.h for the contracts. Tests came first.

#include <engine/core/MemUtil.h>

namespace eng {

void Accumulate(Vec2& target, const Vec2& rhs) {
    target.x += rhs.x;
    target.y += rhs.y;
}

void SwapI32(i32& a, i32& b) {
    const i32 temp = a;
    a = b;
    b = temp;
}

bool ReverseBytes(u8* data, usize count) {
    if (data == nullptr) {
        return false;
    }
    if (count < 2) {
        return true;   // 0 and 1 are already reversed
    }

    usize low  = 0;
    usize high = count - 1;
    while (low < high) {
        const u8 temp = data[low];
        data[low]     = data[high];
        data[high]    = temp;
        ++low;
        --high;
    }
    return true;
}

usize CountBytes(const u8* data, usize count, u8 value) {
    if (data == nullptr) {
        return 0;
    }

    usize found = 0;
    for (usize i = 0; i < count; ++i) {
        if (data[i] == value) {
            ++found;
        }
    }
    return found;
}

bool CopyOverlapping(u8* dst, const u8* src, usize count) {
    if (dst == nullptr || src == nullptr) {
        return false;
    }
    if (count == 0 || dst == src) {
        return true;
    }

    if (dst < src) {
        // Forward. Every byte is read before anything can overwrite it,
        // because the write cursor trails the read cursor.
        for (usize i = 0; i < count; ++i) {
            dst[i] = src[i];
        }
    } else {
        // Backward. Same argument mirrored. Note the loop shape: counting down
        // with an unsigned index and `i > 0` avoids the wrap that `i >= 0`
        // would produce, which is a bug that only shows up on the last
        // iteration and therefore always.
        for (usize i = count; i > 0; --i) {
            dst[i - 1] = src[i - 1];
        }
    }
    return true;
}

} // namespace eng
