// ============================================================================
//  LogBuffer.cpp - the in-memory copy of the log. See LogBuffer.h.
//
//  The storage is a "ring buffer": one std::vector of a fixed size plus an
//  index saying where the next message goes. When the index reaches the end it
//  wraps back to the start and overwrites the oldest message.
//
//  WHY A RING RATHER THAN JUST push_back-ing FOREVER
//  A vector that only ever grows would use more and more memory the longer the
//  program runs. A ring uses a fixed amount no matter how long the session is,
//  and "forget the oldest" is exactly the behaviour a scrollback window wants.
// ============================================================================

#include <engine/core/LogBuffer.h>

#include <algorithm>
#include <set>

namespace eng {
namespace {

std::vector<LogRecord> g_ring;                                   // the storage
std::size_t            g_capacity = LogBuffer::kDefaultCapacity;  // how many fit
std::size_t            g_head     = 0;   // where the NEXT message will be written
std::size_t            g_size     = 0;   // how many slots currently hold a message
unsigned long long     g_total    = 0;   // messages ever written, including dropped

// std::set keeps its contents sorted and refuses duplicates automatically,
// which is exactly what "the list of distinct channel names, in alphabetical
// order" needs. Using a std::vector here would mean searching it by hand
// before every insert and sorting it before every read.
std::set<std::string> g_channels;

} // namespace

void LogBuffer::SetCapacity(std::size_t capacity) {
    if (capacity == 0) {
        capacity = 1;   // a zero-length ring would divide by zero below
    }
    g_capacity = capacity;
    g_ring.clear();
    g_ring.shrink_to_fit();   // actually hand the old memory back
    g_head = 0;
    g_size = 0;
}

std::size_t LogBuffer::Capacity() { return g_capacity; }

void LogBuffer::Append(const LogRecord& record) {
    // The storage is allocated the first time something is logged rather than
    // at startup, so a program that never logs never pays for it.
    if (g_ring.size() < g_capacity) {
        g_ring.resize(g_capacity);
    }

    LogRecord stored = record;
    stored.sequence  = ++g_total;

    g_ring[g_head] = std::move(stored);

    // The modulo is what makes this a ring: after the last slot, index 0 again.
    g_head = (g_head + 1) % g_capacity;

    // Grows until the ring is full, then stays there forever.
    g_size = std::min(g_size + 1, g_capacity);

    g_channels.insert(record.channel);
}

void LogBuffer::Snapshot(std::vector<LogRecord>& out) {
    out.clear();
    out.reserve(g_size);

    // Once the ring has wrapped, the OLDEST message is the one at g_head -
    // the slot about to be overwritten next. Before it wraps, the oldest is
    // simply slot 0.
    const std::size_t first = (g_size == g_capacity) ? g_head : 0;

    for (std::size_t i = 0; i < g_size; ++i) {
        out.push_back(g_ring[(first + i) % g_capacity]);
    }
}

void LogBuffer::Channels(std::vector<std::string>& out) {
    out.assign(g_channels.begin(), g_channels.end());
}

std::size_t        LogBuffer::Count()        { return g_size; }
unsigned long long LogBuffer::TotalWritten() { return g_total; }

void LogBuffer::Clear() {
    g_head = 0;
    g_size = 0;
    // The channel names are deliberately kept. Pressing "Clear" in the Console
    // should empty the message list, not make the filter checkboxes disappear
    // out from under the person who was using them.
}

} // namespace eng
