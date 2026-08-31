#pragma once

// =============================================================================
//  WEEK 5 - the thread-safe queue. Ch. 4.5-4.8.
//
//  *** THE MOST LOAD-BEARING SINGLE FILE IN WEEKS 1-8. ***
//  Week 9 routes asynchronous asset loading through it.
//
//  Coming from C#: this is BlockingCollection<T> / Channel<T>, built by hand.
//  The reason to build it by hand is that "a mutex and a condition variable"
//  is a pattern you will read in engine code for the rest of your career, and
//  reading it is much easier once you have got the wakeup logic wrong yourself.
//
//  A TEMPLATE, so it lives entirely in this header - there is no .cpp. That is
//  one of the concrete ways C++ templates differ from C# generics: a generic
//  is compiled once and specialised by the runtime, whereas a template is
//  INSTANTIATED per type by the compiler, which therefore has to be able to
//  see the definition at every point of use.
// =============================================================================

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace eng {

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    ThreadSafeQueue(const ThreadSafeQueue&)            = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void Push(T item) {
        // ---- THE HOLD-THE-LOCK-WHILE-NOTIFYING QUESTION, ANSWERED. ----------
        //
        // The lock is RELEASED BEFORE notify_one(). Both orders are correct and
        // both appear in production code; the trade-off is:
        //
        //   Notify while holding  -> the woken thread immediately blocks on the
        //     mutex we still hold and goes back to sleep. That is the "hurry up
        //     and wait" problem. Some implementations optimise it away with
        //     wait morphing; you cannot rely on that.
        //
        //   Notify after releasing -> the woken thread can take the lock at
        //     once. The cost is a narrow race where a consumer that was ABOUT
        //     to wait misses the signal - which is harmless, because the
        //     predicate overload of wait() re-checks the queue under the lock
        //     before sleeping, so it sees the item and never waits at all.
        //
        // The second is chosen because the predicate makes the race benign, and
        // because the whole point of a job queue is that a woken worker starts
        // working immediately.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) {
                // Refusing pushes after Stop() is what makes shutdown
                // terminate. A producer that keeps feeding a stopping queue
                // keeps consumers alive forever.
                return;
            }
            m_items.push(std::move(item));
        }
        m_condition.notify_one();
    }

    // Blocks until an item is available or the queue is stopped AND drained.
    // Returns nullopt only in the second case.
    std::optional<T> WaitAndPop() {
        std::unique_lock<std::mutex> lock(m_mutex);

        // ---- THE PREDICATE OVERLOAD, AND WHY IT IS NOT OPTIONAL. -------------
        //
        // A condition variable is permitted to wake a thread when nothing has
        // happened - a SPURIOUS WAKEUP. Code written as
        //
        //     if (empty) m_condition.wait(lock);
        //
        // is therefore wrong: it can fall through with the queue still empty
        // and pop from nothing. The predicate overload is exactly equivalent to
        //
        //     while (!predicate()) m_condition.wait(lock);
        //
        // which re-checks after every wake. The bug the `if` version produces
        // reproduces roughly once in a few thousand runs, on someone else's
        // machine, in Week 9.
        m_condition.wait(lock, [this] { return !m_items.empty() || m_stopped; });

        // DESIGN DECISION, pinned down by the provided suite's last case:
        // items already in the queue when Stop() is called ARE still delivered.
        // A shutdown mid-load should not silently drop an asset load that was
        // already accepted. So the empty check comes after the wait and the
        // stopped flag alone is not enough to return nullopt.
        if (m_items.empty()) {
            return std::nullopt;
        }

        T item = std::move(m_items.front());
        m_items.pop();
        return item;
    }

    // Takes an item if one is available; never blocks.
    std::optional<T> TryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_items.empty()) {
            return std::nullopt;
        }
        T item = std::move(m_items.front());
        m_items.pop();
        return item;
    }

    // Wakes EVERY waiting consumer and refuses further pushes.
    //
    // *** "EVERY", NOT "ONE", AND THIS IS THE PART EVERYONE FORGETS. ***
    //
    // notify_one would wake a single blocked consumer; the other three stay
    // blocked in WaitAndPop forever, and joining a thread that will never wake
    // is a deadlock at exit - the engine appears to close and the process never
    // dies. notify_all is the whole fix, and the provided suite has a case
    // ("Stop wakes ALL blocked consumers") that exists solely to catch it.
    //
    // Also note the flag is set UNDER the lock. Setting it outside means a
    // consumer can evaluate the predicate between the store and the notify,
    // see an empty queue and a not-yet-stopped flag, and sleep through the
    // wakeup.
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_condition.notify_all();
    }

    bool IsStopped() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stopped;
    }

    // FOR TESTS AND DIAGNOSTICS ONLY. **DO NOT BRANCH ON THIS.**
    //
    // By the time you have read the value and acted on it, another thread may
    // have changed it - so `if (q.SizeApprox() > 0) q.TryPop()` is a race even
    // though both calls are individually safe. That is the difference between
    // a thread-safe OPERATION and a thread-safe SEQUENCE of operations, and it
    // is the single most common misunderstanding about lock-protected types.
    //
    // DISPLAYING it is fine and is exactly what it is for: the Jobs panel plots
    // it every frame. A number that may be one item stale is honest; a decision
    // made on one is not.
    std::size_t SizeApprox() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_condition;
    std::queue<T>           m_items;
    bool                    m_stopped = false;
};

} // namespace eng
