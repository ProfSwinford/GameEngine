// WEEK 4 - the timer registry. See ScopedTimer.h.

#include <engine/core/Log.h>
#include <engine/debug/ScopedTimer.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

namespace eng {
namespace {

// Ordered rather than unordered, so Report() and the Profiler panel enumerate
// in a stable alphabetical order. A profiler table whose rows jump around
// between frames is unreadable, and the lookup cost difference at a few dozen
// sites is not measurable.
std::map<std::string, TimerStats> g_sites;
std::mutex                        g_mutex;

f32   g_frames[TimerRegistry::kFrameHistoryCapacity]{};
usize g_frameCount = 0;
usize g_frameHead  = 0;
f32   g_frameLinear[TimerRegistry::kFrameHistoryCapacity]{};

} // namespace

void TimerRegistry::Submit(const char* name, f64 elapsedMs) {
    if (name == nullptr) {
        return;
    }
    // Week 5 made this lock-guarded: worker threads run timed scopes too, and
    // an unsynchronised map insert from two threads is a corrupted tree rather
    // than a wrong number.
    std::lock_guard<std::mutex> lock(g_mutex);

    TimerStats& stats = g_sites[name];
    if (stats.samples == 0) {
        stats.minMs = elapsedMs;
        stats.maxMs = elapsedMs;
    } else {
        stats.minMs = std::min(stats.minMs, elapsedMs);
        stats.maxMs = std::max(stats.maxMs, elapsedMs);
    }
    stats.totalMs += elapsedMs;
    ++stats.samples;
}

void TimerRegistry::Report() {
    std::map<std::string, TimerStats> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_sites;
    }

    if (copy.empty()) {
        ENGINE_LOG_INFO(Channels::kProfile, "no timed scopes recorded");
        return;
    }

    ENGINE_LOG_INFO(Channels::kProfile, "{:<34} {:>10} {:>10} {:>10} {:>9}", "site",
                    "min ms", "avg ms", "max ms", "samples");
    for (const auto& [name, stats] : copy) {
        ENGINE_LOG_INFO(Channels::kProfile, "{:<34} {:>10.4f} {:>10.4f} {:>10.4f} {:>9}",
                        name, stats.minMs, stats.AverageMs(), stats.maxMs, stats.samples);
    }
}

void TimerRegistry::Reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sites.clear();
}

TimerStats TimerRegistry::Get(const char* name) {
    if (name == nullptr) {
        return TimerStats{};
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_sites.find(name);
    return (it != g_sites.end()) ? it->second : TimerStats{};
}

void TimerRegistry::ForEach(const std::function<void(const char*, const TimerStats&)>& fn) {
    // Copy under the lock, then call the callback outside it. The callback is
    // panel code that draws ImGui widgets, and holding a lock across arbitrary
    // caller code is how deadlocks are made - the panel's own timed scopes
    // would re-enter Submit and block on a mutex this thread already holds.
    std::map<std::string, TimerStats> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_sites;
    }
    for (const auto& [name, stats] : copy) {
        fn(name.c_str(), stats);
    }
}

usize TimerRegistry::SiteCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_sites.size();
}

void TimerRegistry::SubmitFrameTime(f32 milliseconds) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_frames[g_frameHead] = milliseconds;
    g_frameHead           = (g_frameHead + 1) % kFrameHistoryCapacity;
    g_frameCount          = std::min(g_frameCount + 1, kFrameHistoryCapacity);
}

usize TimerRegistry::FrameHistoryCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_frameCount;
}

const f32* TimerRegistry::FrameHistory() {
    // Linearised into a second fixed buffer, oldest first, because
    // ImGui::PlotLines wants a contiguous array and a ring is not one. Fixed
    // size, so this does not become the leak the Memory panel exists to find.
    std::lock_guard<std::mutex> lock(g_mutex);
    const usize first = (g_frameCount == kFrameHistoryCapacity) ? g_frameHead : 0;
    for (usize i = 0; i < g_frameCount; ++i) {
        g_frameLinear[i] = g_frames[(first + i) % kFrameHistoryCapacity];
    }
    return g_frameLinear;
}

} // namespace eng
