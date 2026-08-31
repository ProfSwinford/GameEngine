// WEEK 3 - the in-memory log sink. See LogBuffer.h.
//
// A ring of fixed capacity. Appending past capacity overwrites the oldest
// entry rather than growing, because a session that runs for an hour must not
// end with a hundred megabytes of log text resident.
//
// WEEK 5: every public function takes m_mutex. Snapshot() copies under it so
// that the editor can draw from its copy without holding a lock that worker
// threads need in order to log.

#include <engine/core/LogBuffer.h>

#include <algorithm>
#include <mutex>
#include <set>

namespace eng {
namespace {

std::mutex             g_mutex;
std::vector<LogRecord> g_ring;
usize                  g_capacity = LogBuffer::kDefaultCapacity;
usize                  g_head     = 0;   // index of the next slot to write
usize                  g_size     = 0;
u64                    g_total    = 0;
std::set<std::string>  g_channels;

} // namespace

void LogBuffer::SetCapacity(usize capacity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (capacity == 0) {
        capacity = 1;
    }
    g_capacity = capacity;
    g_ring.clear();
    g_ring.shrink_to_fit();
    g_head = 0;
    g_size = 0;
}

usize LogBuffer::Capacity() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_capacity;
}

void LogBuffer::Append(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ring.size() < g_capacity) {
        g_ring.resize(g_capacity);
    }

    LogRecord stored = record;
    stored.sequence  = ++g_total;

    g_ring[g_head] = std::move(stored);
    g_head         = (g_head + 1) % g_capacity;
    g_size         = std::min(g_size + 1, g_capacity);

    // The panel discovers its channel checkboxes from this set. Inserting a
    // std::string per line would allocate; inserting only on a genuinely new
    // channel costs one lookup, and the set stops growing within a second of
    // startup because the channel list is small and fixed in practice.
    if (!g_channels.contains(record.channel)) {
        g_channels.insert(record.channel);
    }
}

void LogBuffer::Snapshot(std::vector<LogRecord>& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    out.clear();
    out.reserve(g_size);

    const usize first = (g_size == g_capacity) ? g_head : 0;
    for (usize i = 0; i < g_size; ++i) {
        out.push_back(g_ring[(first + i) % g_capacity]);
    }
}

void LogBuffer::Channels(std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    out.assign(g_channels.begin(), g_channels.end());
}

usize LogBuffer::Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_size;
}

u64 LogBuffer::TotalWritten() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_total;
}

void LogBuffer::Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_head = 0;
    g_size = 0;
    // The channel set is deliberately NOT cleared. Clearing the visible text
    // should not make the filter checkboxes vanish underneath the user.
}

} // namespace eng
