#pragma once
//
// ttq::SPSCQueue<T, Capacity>
// ---------------------------
// My bounded, lock-free ring buffer for one producer and one consumer.
//
// The whole thing only holds together because I promise exactly one thread
// ever calls push() and exactly one ever calls pop(). Let two threads push
// (or two pop) and it breaks. That single promise is what lets me skip locks
// and get by with just two atomics.
//
// I use it as the channel between the host thread, which enqueues commands,
// and the dispatcher thread, which drains them — one writer, one reader.
//
// Memory-ordering notes to myself:
//   head_ = next slot the consumer reads from   (consumer owns it)
//   tail_ = next slot the producer writes to    (producer owns it)
//
// I have to stop the compiler/CPU from reordering things so that:
//   * the consumer never reads a slot before the producer finished writing it
//   * the producer never overwrites a slot the consumer is still reading
//
// Acquire/release on the two atomics is how I get that:
//   - producer stores tail_ with release, so the buffer write lands before the
//     new index is visible
//   - consumer loads tail_ with acquire, so once it sees the new tail_ it also
//     sees the data behind it
//   - head_ is the mirror image: consumer releases, producer acquires
//   - an index a thread owns and alone writes, it can read back with relaxed —
//     nobody else touches it, so there's nothing to synchronize
//

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace ttq {

// Capacity is a compile-time constant so the storage is a fixed std::array —
// no heap, nothing allocated on the hot path, since I care about latency here.
//
// Telling EMPTY from FULL is the tricky part when both look like head_ == tail_.
// I went with free-running counters and masking: full is (tail_ - head_ ==
// Capacity), so I never have to waste a slot.
template <typename T, std::size_t Capacity> class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCQueue() = default;

    // non-copyable, non-movable: it's a shared channel, aliasing it is a bug.
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer thread only. Returns false if the queue is full (item not stored).
    bool push(const T& item)
    {
        auto t = tail_.load(std::memory_order_relaxed);

        // Re-read the consumer's index only when the cached copy says we're full.
        if (t - head_cache_ == capacity()) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (t - head_cache_ == capacity())
                return false;
        }

        buffer_[mask(t)] = item;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Returns nullopt if the queue is empty.
    std::optional<T> pop()
    {
        auto h = head_.load(std::memory_order_relaxed);

        // Re-read the producer's index only when the cached copy says we're empty.
        if (h == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (h == tail_cache_)
                return std::nullopt;
        }

        auto item = buffer_[mask(h)];
        head_.store(h + 1, std::memory_order_release);

        return item;
    }

    // Rough size — safe to call from either thread, but it can be stale the instant
    // it returns. I only use it for tests/metrics, so that's fine.
    std::size_t size_approx() const
    {
        auto t = tail_.load(std::memory_order_relaxed);
        auto h = head_.load(std::memory_order_relaxed);

        return t - h;
    }

    static constexpr std::size_t capacity() { return Capacity; }
    static constexpr std::size_t mask(std::size_t idx) { return idx & (capacity() - 1); }

private:
    // Cache-line size, used just below to keep the two indices on separate lines.
    // std::hardware_destructive_interference_size would do this, but its value can
    // change between builds, which could give this type different layouts in
    // different files, so use a plain constant. gcc warns about the same thing
    // (-Winterference-size); background:
    // https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
    // 128 on Apple silicon, 64 otherwise.
#if defined(__APPLE__) && defined(__aarch64__)
    static constexpr std::size_t kCacheLine = 128;
#else
    static constexpr std::size_t kCacheLine = 64;
#endif

    // Storage. One entry per slot.
    std::array<T, Capacity> buffer_ {};

    // head_ and tail_ sit on separate cache lines (see kCacheLine) so the two
    // threads don't keep bouncing one shared line between cores. Each index also
    // shares its line with a cached copy of the *other* index that only its owner
    // touches:
    //   head_ + tail_cache_  -> consumer's line
    //   tail_ + head_cache_  -> producer's line
    // Those caches let push and pop skip reading the other thread's index most of
    // the time: push re-reads head_ only when the cache says full, pop re-reads
    // tail_ only when it says empty. A cache is always behind the real index (which
    // only moves forward), so trusting it is safe, and the acquire load that
    // refills it carries the ordering the next operations rely on.
    alignas(kCacheLine) std::atomic<std::size_t> head_ { 0 };
    std::size_t tail_cache_ { 0 };

    alignas(kCacheLine) std::atomic<std::size_t> tail_ { 0 };
    std::size_t head_cache_ { 0 };
};

} // namespace ttq
