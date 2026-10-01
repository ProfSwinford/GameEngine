// =============================================================================
//  LogBuffer.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. LogBuffer.h is the specification; read it first.
// =============================================================================

#include <engine/core/LogBuffer.h>

#include <algorithm>
#include <set>

namespace eng {
namespace {

    std::vector<LogRecord>  g_ring;
    std::size_t             g_capacity = LogBuffer::kDefaultCapacity;
    std::size_t             g_head = 0;
    std::size_t             g_size = 0;
    std::size_t             g_total = 0;          

    std::set<std::string> g_channels;
} // namespace

// Sets how many recent messages are kept. Older ones fall off the end, so a
// program left running overnight does not fill memory with its own log.
void LogBuffer::SetCapacity(std::size_t capacity) {
    if (capacity == 0)
        capacity = 1;
    
    g_capacity = capacity;
    g_ring.clear();
    g_ring.shrink_to_fit();
    g_head = 0;
    g_size = 0;
}

// How many messages the buffer is currently willing to hold.
std::size_t LogBuffer::Capacity() { return g_capacity; }

// Adds one message. Called by Log::Write, and by nothing else.
void LogBuffer::Append(const LogRecord& record) {
    if(g_ring.size() < g_capacity)
    {
        g_ring.resize(g_capacity);
    }

    LogRecord stored = record;
    stored.sequence = ++g_total;

    g_ring[g_head] = std::move(stored);

    g_head = (g_head + 1) % g_capacity;

    g_size = std::min(g_size + 1, g_capacity);
    g_channels.insert(record.channel);

}

// Copies the whole buffer out for the Console window to draw. A copy, because
// the buffer can change while the window is being drawn.
void LogBuffer::Snapshot(std::vector<LogRecord>& out) {
    out.clear();
    out.reserve(g_size);

    const std::size_t first = (g_size == g_capacity) ? g_head : 0;

    for (std::size_t i = 0; i < g_size; i++) {
        out.push_back(g_ring[(first + i) % g_capacity]);
    }
}

// Lists every channel name seen so far, which is what fills the Console's
// channel filter without anyone having to declare the list up front.
void LogBuffer::Channels(std::vector<std::string>& out) {
    out.assign(g_channels.begin(), g_channels.end());
}

// How many messages are being held right now.
std::size_t LogBuffer::Count() { return g_size; }

// How many messages have ever been written, including ones already dropped.
unsigned long long LogBuffer::TotalWritten() { return g_total; }

// Empties the buffer - the Console's Clear button.
void LogBuffer::Clear() {
    g_head = 0;
    g_size = 0;
}

} // namespace eng
