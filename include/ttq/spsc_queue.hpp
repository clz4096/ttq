#pragma once
//
// ttq::SPSCQueue<T, Capacity>
// ---------------------------
// A bounded, lock-free, single-producer / single-consumer ring buffer.
//
// "Single-producer / single-consumer" is the CONTRACT you rely on:
//   - EXACTLY ONE thread ever calls push().
//   - EXACTLY ONE thread ever calls pop().
// If two threads push (or two pop), this class is NOT safe. That single
// assumption is what lets us avoid locks entirely and use just two atomics.
//
// Why this exists in a Metal-Runtime-flavored project:
//   The host thread enqueues commands; the dispatcher ("device") thread
//   drains them. One writer, one reader — exactly SPSC. This queue is the
//   channel between them.
//
// ---------------------------------------------------------------------------
// MEMORY ORDERING (the #1 follow-up question — read this before you code)
// ---------------------------------------------------------------------------
// Two indices describe the buffer state:
//   head_  : the NEXT slot the consumer will read from   (owned by consumer)
//   tail_  : the NEXT slot the producer will write to     (owned by producer)
//
// The danger in lock-free code is REORDERING: the compiler/CPU may run your
// instructions out of order. We must guarantee:
//   * The consumer never reads a slot BEFORE the producer finished writing it.
//   * The producer never overwrites a slot BEFORE the consumer finished reading.
//
// The tool is acquire/release ordering on the two atomics:
//
//   RELEASE (producer, when publishing tail_):
//     "Everything I wrote to the buffer slot BEFORE this store must be visible
//      to anyone who later reads tail_ with acquire." i.e. the data write is
//      guaranteed to land before the index update becomes visible.
//
//   ACQUIRE (consumer, when reading tail_):
//     "Once I see the new tail_ value, I am also guaranteed to see the buffer
//      write that happened-before it." i.e. no reading a slot the producer
//      hasn't actually filled yet.
//
//   The mirror image applies to head_ (consumer releases, producer acquires),
//   so the producer never clobbers a slot the consumer is still reading.
//
//   An index a thread OWNS and only it writes can be read by that same thread
//   with memory_order_relaxed (no other thread changes it, so no sync needed).
//
// Rule of thumb you can say out loud in an interview:
//   "Producer publishes with release, consumer observes with acquire; that
//    release/acquire pair is what orders the data write before the index
//    update. relaxed is fine for reading the index you alone own."
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstddef>
#include <optional>
#include <array>

namespace ttq {

// Capacity is a compile-time constant so the storage is a fixed std::array
// (no heap, no allocation on the hot path — important for latency).
//
// DESIGN NOTE we will drill together: how do you tell EMPTY apart from FULL
// when both can look like head_ == tail_? Two classic answers:
//   (a) waste one slot: buffer holds Capacity-1 items; full == next(tail)==head
//   (b) free-running counters + masking: full == (tail_ - head_ == Capacity)
// You will pick one when you implement. The signatures below work for either.
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCQueue() = default;

    // non-copyable, non-movable: it's a shared channel, aliasing it is a bug.
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Called ONLY by the producer thread.
    // Returns false if the queue is full (item NOT stored).
    // TODO(you): implement.
    bool push(const T& item) {
        auto t = tail_.load(std::memory_order_relaxed);
        auto h = head_.load(std::memory_order_acquire);

        if ((t - h) == capacity() ) return false;

        buffer_[mask(t)] = item;
        tail_.store( t + 1, std::memory_order_release);
        return true;
    }

    // Called ONLY by the consumer thread.
    // Returns std::nullopt if the queue is empty.
    // TODO(you): implement.
    std::optional<T> pop() {
        auto t = tail_.load(std::memory_order_acquire);
        auto h = head_.load(std::memory_order_relaxed);

        if (t - h == 0) return std::nullopt;
        auto item = buffer_[mask(h)];
        head_.store(h + 1, std::memory_order_release);

        return item;
    }

    // Approximate size. Safe to call from either thread but the value may be
    // stale the instant it returns (that's fine — it's for tests/metrics).
    // TODO(you): implement.
    std::size_t size_approx() const {
        auto t = tail_.load(std::memory_order_relaxed);        
        auto h = head_.load(std::memory_order_relaxed);

        return t - h;
    }

    static constexpr std::size_t capacity() { return Capacity; }
    static constexpr std::size_t mask(std::size_t idx) { return idx & (capacity() - 1 ); }

private:
    // Storage. One entry per slot.
    std::array<T, Capacity> buffer_{};

    // Consumer's index. Consumer writes it (release), producer reads it (acquire).
    std::atomic<std::size_t> head_{0};

    // Producer's index. Producer writes it (release), consumer reads it (acquire).
    std::atomic<std::size_t> tail_{0};

    // Optional helper you may or may not want depending on which full/empty
    // scheme you choose:
    // static constexpr std::size_t next(std::size_t i) { return (i + 1) % Capacity; }
};

} // namespace ttq
