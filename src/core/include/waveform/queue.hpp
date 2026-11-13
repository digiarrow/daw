#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace waveform
{

/// Wait-free single-producer, single-consumer ring.
///
/// Capacity is the number of slots in the ring and must be a power of two.
/// One slot is left empty, so the queue holds `Capacity - 1` items.
/// `T` has to be trivially copyable: the audio thread must not run a constructor that allocates.
/// Only one thread may call push, and only one thread may call pop.
template <typename T, std::size_t Capacity>
class SpscQueue
{
public:
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert (std::is_trivially_copyable_v<T>, "SpscQueue requires a trivially copyable payload");

    SpscQueue() = default;

    SpscQueue (const SpscQueue&) = delete;
    SpscQueue& operator= (const SpscQueue&) = delete;

    /// False when the ring is full. The item is left untouched in that case.
    bool push (const T& item) noexcept
    {
        const auto head = head_.load (std::memory_order_relaxed);
        const auto next = (head + 1) & kMask;

        if (next == tail_.load (std::memory_order_acquire))
            return false;

        buffer_[head] = item;
        head_.store (next, std::memory_order_release);
        return true;
    }

    /// False when the ring is empty.
    bool pop (T& item) noexcept
    {
        const auto tail = tail_.load (std::memory_order_relaxed);

        if (tail == head_.load (std::memory_order_acquire))
            return false;

        item = buffer_[tail];
        tail_.store ((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_ {};
    alignas (64) std::atomic<std::size_t> head_ { 0 };
    alignas (64) std::atomic<std::size_t> tail_ { 0 };
};

} // namespace waveform
