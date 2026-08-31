// WEEK 5 - worker threads. See JobSystem.h.

#include <engine/concurrency/JobSystem.h>
#include <engine/concurrency/ThreadSafeQueue.h>
#include <engine/core/Log.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace eng {
namespace {

ThreadSafeQueue<Job>       g_queue;
std::vector<std::thread>   g_workers;
std::vector<std::atomic<bool>> g_busy;   // one per worker; see the header
std::atomic<u64>           g_pushed{0};
std::atomic<u64>           g_completed{0};
std::atomic<bool>          g_running{false};
JobSystem::PlatformCosts   g_costs;

void WorkerMain(u32 index) {
    ENGINE_LOG_DEBUG(Channels::kJobs, "worker {} started", index);

    while (true) {
        std::optional<Job> job = g_queue.WaitAndPop();
        if (!job.has_value()) {
            break;   // stopped and drained - the only way out
        }

        g_busy[index].store(true, std::memory_order_relaxed);
        (*job)();
        g_busy[index].store(false, std::memory_order_relaxed);

        g_completed.fetch_add(1, std::memory_order_relaxed);
    }

    ENGINE_LOG_DEBUG(Channels::kJobs, "worker {} exiting", index);
}

} // namespace

bool JobSystem::Init(u32 workerCount) {
    if (g_running.load()) {
        return true;
    }

    if (workerCount == 0) {
        const u32 hardware = std::thread::hardware_concurrency();
        workerCount = (hardware > 1) ? hardware - 1 : 1;   // leave the main thread one
    }

    // Constructed before any thread starts: g_busy must not be resized while
    // a worker is indexing into it, and a vector resize moves its elements.
    g_busy = std::vector<std::atomic<bool>>(workerCount);
    for (auto& flag : g_busy) {
        flag.store(false, std::memory_order_relaxed);
    }

    g_running.store(true);
    g_workers.reserve(workerCount);
    for (u32 i = 0; i < workerCount; ++i) {
        g_workers.emplace_back(WorkerMain, i);
    }

    ENGINE_LOG_INFO(Channels::kJobs, "JobSystem up with {} worker thread(s)", workerCount);
    return true;
}

void JobSystem::Shutdown() {
    if (!g_running.load()) {
        return;
    }

    // Stop FIRST, then join. Stop() wakes every waiter with notify_all; join
    // on a thread still blocked in WaitAndPop would never return, and the
    // process would appear to close and then simply not die. This is the exact
    // failure the provided Week 5 test suite was written to catch.
    g_queue.Stop();
    for (std::thread& worker : g_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    g_workers.clear();
    g_running.store(false);

    ENGINE_LOG_INFO(Channels::kJobs, "JobSystem down ({} job(s) completed)",
                    g_completed.load());
}

bool JobSystem::IsRunning() {
    return g_running.load();
}

void JobSystem::Enqueue(Job job) {
    if (!g_running.load()) {
        // No workers: run it inline rather than dropping it. That keeps the
        // engine correct when the job system is deliberately off (the test
        // binary, a headless tool) at the cost of the caller blocking - which
        // is the honest degradation.
        job();
        g_pushed.fetch_add(1, std::memory_order_relaxed);
        g_completed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_pushed.fetch_add(1, std::memory_order_relaxed);
    g_queue.Push(std::move(job));
}

void JobSystem::WaitForIdle() {
    // Deliberately a sleep-poll rather than another condition variable. It is
    // called from scene loading and from tests, never per frame, and a second
    // condition variable would be a second thing to get wrong for a path that
    // is not hot. Said out loud rather than left as an accident.
    while (g_queue.SizeApprox() > 0 || g_pushed.load() != g_completed.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

u32 JobSystem::WorkerCount() {
    return static_cast<u32>(g_workers.size());
}

bool JobSystem::IsWorkerBusy(u32 index) {
    return index < g_busy.size() && g_busy[index].load(std::memory_order_relaxed);
}

usize JobSystem::QueueDepth() {
    return g_queue.SizeApprox();
}

u64 JobSystem::JobsPushed() {
    return g_pushed.load(std::memory_order_relaxed);
}

u64 JobSystem::JobsCompleted() {
    return g_completed.load(std::memory_order_relaxed);
}

void JobSystem::SetPlatformCosts(const PlatformCosts& costs) {
    g_costs = costs;
}

const JobSystem::PlatformCosts& JobSystem::GetPlatformCosts() {
    return g_costs;
}

} // namespace eng
