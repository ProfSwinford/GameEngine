// =============================================================================
//  WEEK 5 - PROVIDED. Do not edit. This is the stress test your queue must
//  survive, and it is the Week 5 verification.
//
//  Read it before you implement the queue. It tells you exactly what
//  "correct" means here: every item pushed is received EXACTLY ONCE, with no
//  loss and no duplication, and shutdown does not hang.
//
//  IMPORTANT: passing once is not evidence. Race conditions are probabilistic.
//  Run this repeatedly - the guide shows how to loop it - and run it under a
//  thread sanitizer if your toolchain has one. A race that reproduces one run
//  in two hundred is still a race, and Week 9 will run this code path
//  thousands of times.
// =============================================================================

#include <doctest/doctest.h>
#include <engine/concurrency/ThreadSafeQueue.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace eng;

TEST_CASE("single producer, single consumer, every item arrives once") {
    ThreadSafeQueue<int> queue;
    constexpr int kCount = 10000;

    std::vector<char> seen(kCount, 0);

    std::thread consumer([&] {
        for (int i = 0; i < kCount; ++i) {
            auto item = queue.WaitAndPop();
            REQUIRE(item.has_value());
            REQUIRE(*item >= 0);
            REQUIRE(*item < kCount);
            CHECK(seen[static_cast<std::size_t>(*item)] == 0);   // no duplicates
            seen[static_cast<std::size_t>(*item)] = 1;
        }
    });

    for (int i = 0; i < kCount; ++i) {
        queue.Push(i);
    }

    consumer.join();

    for (int i = 0; i < kCount; ++i) {
        CHECK(seen[static_cast<std::size_t>(i)] == 1);            // no losses
    }
}

TEST_CASE("N producers, one consumer, nothing lost or duplicated") {
    ThreadSafeQueue<int> queue;
    constexpr int kProducers   = 8;
    constexpr int kPerProducer = 2000;
    constexpr int kTotal       = kProducers * kPerProducer;

    std::vector<char> seen(kTotal, 0);
    std::atomic<int>  received{0};

    std::thread consumer([&] {
        while (received.load() < kTotal) {
            auto item = queue.WaitAndPop();
            if (!item) break;
            CHECK(seen[static_cast<std::size_t>(*item)] == 0);
            seen[static_cast<std::size_t>(*item)] = 1;
            received.fetch_add(1);
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                queue.Push(p * kPerProducer + i);
            }
        });
    }

    for (auto& t : producers) t.join();
    consumer.join();

    CHECK(received.load() == kTotal);
    for (int i = 0; i < kTotal; ++i) {
        CHECK(seen[static_cast<std::size_t>(i)] == 1);
    }
}

TEST_CASE("Stop wakes a blocked consumer instead of hanging forever") {
    // If this test hangs, your Stop() is not notifying, or it is notifying
    // only one waiter. This is the exact failure that will hang your engine's
    // shutdown in Week 9, so fix it here where it takes ten seconds to notice.
    ThreadSafeQueue<int> queue;

    std::atomic<bool> consumerReturned{false};

    std::thread consumer([&] {
        auto item = queue.WaitAndPop();
        CHECK_FALSE(item.has_value());
        consumerReturned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(consumerReturned.load());   // genuinely blocked

    queue.Stop();
    consumer.join();

    CHECK(consumerReturned.load());
}

TEST_CASE("Stop wakes ALL blocked consumers") {
    ThreadSafeQueue<int> queue;
    constexpr int kConsumers = 4;

    std::atomic<int> returned{0};
    std::vector<std::thread> consumers;
    consumers.reserve(kConsumers);

    for (int i = 0; i < kConsumers; ++i) {
        consumers.emplace_back([&] {
            auto item = queue.WaitAndPop();
            CHECK_FALSE(item.has_value());
            returned.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.Stop();
    for (auto& t : consumers) t.join();

    CHECK(returned.load() == kConsumers);
}

TEST_CASE("items drained after Stop are still delivered") {
    // Design decision, and reasonable engines differ. This suite requires that
    // Stop() lets already-queued work finish rather than discarding it, which
    // is what you want for Week 9: a shutdown mid-load should not silently
    // drop an asset load that was already accepted.
    ThreadSafeQueue<int> queue;
    queue.Push(1);
    queue.Push(2);
    queue.Stop();

    auto a = queue.WaitAndPop();
    auto b = queue.WaitAndPop();
    auto c = queue.WaitAndPop();

    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK_FALSE(c.has_value());
}
