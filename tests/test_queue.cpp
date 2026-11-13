#include "waveform/queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

TEST_CASE ("spsc queue preserves order and reports when it is full", "[queue]")
{
    waveform::SpscQueue<int, 8> queue;
    REQUIRE (queue.capacity() == 7);

    for (int value = 0; value < 7; ++value)
        REQUIRE (queue.push (value));

    REQUIRE_FALSE (queue.push (99));

    for (int value = 0; value < 7; ++value)
    {
        int popped = -1;
        REQUIRE (queue.pop (popped));
        REQUIRE (popped == value);
    }

    int empty = 0;
    REQUIRE_FALSE (queue.pop (empty));
}

TEST_CASE ("spsc queue survives a producer and a consumer", "[queue]")
{
    constexpr int kCount = 100000;
    waveform::SpscQueue<int, 1024> queue;
    std::atomic<int> mismatches { 0 };

    std::thread producer ([&]
    {
        for (int value = 0; value < kCount; ++value)
            while (! queue.push (value))
                std::this_thread::yield();
    });

    int expected = 0;

    while (expected < kCount)
    {
        int value = -1;

        if (! queue.pop (value))
            continue;

        if (value != expected)
            mismatches.fetch_add (1);

        ++expected;
    }

    producer.join();
    REQUIRE (mismatches.load() == 0);
}
