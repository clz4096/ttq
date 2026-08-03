// Small test harness for ttq::SPSCQueue, no external deps.
// Build with CMake, run ./test_spsc — exit code 0 means everything passed.
// The threaded stress test runs last on purpose: it's the one that catches
// memory-ordering bugs.

#include "ttq/spsc_queue.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            std::cout << "PASS: " << #cond << "\n";                                                                    \
        } else {                                                                                                       \
            std::cout << "FAIL: " << #cond << "  (line " << __LINE__ << ")\n";                                         \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void test_basic_push_pop()
{
    ttq::SPSCQueue<int, 4> q;
    CHECK(!q.pop().has_value()); // empty pop -> nullopt
    CHECK(q.push(42)); // first push ok
    auto v = q.pop();
    CHECK(v.has_value() && *v == 42); // get it back
    CHECK(!q.pop().has_value()); // empty again
}

static void test_fill_and_full()
{
    // Capacity 4 with free-running counters, so all 4 slots are usable. I just
    // check I can store at least capacity-1 and that push returns false once
    // full.
    ttq::SPSCQueue<int, 4> q;
    int stored = 0;
    while (q.push(stored))
        ++stored;
    CHECK(stored >= 3); // stored at least capacity-1
    CHECK(!q.push(999)); // now full -> push fails
    // drain in FIFO order
    for (int i = 0; i < stored; ++i) {
        auto v = q.pop();
        CHECK(v.has_value() && *v == i); // FIFO: comes out in order pushed
    }
    CHECK(!q.pop().has_value());
}

static void test_wraparound()
{
    // Push/pop repeatedly so the indices wrap past Capacity many times.
    ttq::SPSCQueue<int, 4> q;
    for (int round = 0; round < 100; ++round) {
        CHECK(q.push(round));
        auto v = q.pop();
        CHECK(v.has_value() && *v == round);
    }
}

static void test_threaded_stress()
{
    // One producer, one consumer, N items. Checks nothing's lost, nothing's
    // duplicated, strict FIFO. If my acquire/release is wrong, this is what
    // trips.
    constexpr int N = 1'000'000;
    ttq::SPSCQueue<std::uint64_t, 1024> q;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < N; ++i) {
            while (!q.push(i)) { /* spin until space */
            }
        }
    });

    std::uint64_t expected = 0;
    bool order_ok = true;
    std::thread consumer([&] {
        int got = 0;
        while (got < N) {
            auto v = q.pop();
            if (!v)
                continue; // spin until item
            if (*v != expected)
                order_ok = false;
            ++expected;
            ++got;
        }
    });

    producer.join();
    consumer.join();
    CHECK(order_ok); // every item arrived exactly once, in order
    CHECK(expected == static_cast<std::uint64_t>(N));
}

// Counts every construction and destruction so a test can prove the queue builds
// and tears down each element exactly once. It has no default constructor on
// purpose: the queue must not require one.
struct LifetimeProbe {
    struct Counts {
        int constructed = 0;
        int destroyed = 0;
    };
    Counts* counts;
    int value;

    LifetimeProbe(Counts* c, int v)
        : counts(c)
        , value(v)
    {
        ++counts->constructed;
    }
    LifetimeProbe(const LifetimeProbe& o)
        : counts(o.counts)
        , value(o.value)
    {
        ++counts->constructed;
    }
    LifetimeProbe(LifetimeProbe&& o) noexcept
        : counts(o.counts)
        , value(o.value)
    {
        ++counts->constructed;
    }
    ~LifetimeProbe() { ++counts->destroyed; }
};

static void test_lifetime()
{
    // Every element the queue holds must be destroyed exactly once: on pop, and
    // for whatever is left, in the destructor. Balanced counts prove no leaks and
    // no double-frees. LifetimeProbe has no default constructor, so a clean build
    // also proves the queue does not require one.
    LifetimeProbe::Counts counts;
    {
        ttq::SPSCQueue<LifetimeProbe, 8> q;
        for (int i = 0; i < 5; ++i)
            CHECK(q.emplace(&counts, i)); // construct in place, no copy

        int destroyed_before = counts.destroyed;
        for (int i = 0; i < 3; ++i) {
            auto v = q.pop();
            CHECK(v.has_value() && v->value == i);
        }
        CHECK(counts.destroyed > destroyed_before); // pop destroys as it goes
        // Two elements are left in the queue; the destructor should drain them.
    }
    CHECK(counts.constructed == counts.destroyed); // every ctor matched by one dtor
    CHECK(counts.constructed > 0);
}

static void test_move_only()
{
    // A move-only element (unique_ptr) would not compile against a queue that
    // copies. push(T&&) and emplace must move it through instead.
    ttq::SPSCQueue<std::unique_ptr<int>, 4> q;
    CHECK(q.push(std::make_unique<int>(7))); // rvalue -> push(T&&)
    CHECK(q.emplace(new int(9))); // emplace forwards args to unique_ptr(int*)

    auto a = q.pop();
    CHECK(a.has_value() && **a == 7);
    auto b = q.pop();
    CHECK(b.has_value() && **b == 9);
    CHECK(!q.pop().has_value());
}

int main()
{
    test_basic_push_pop();
    test_fill_and_full();
    test_wraparound();
    test_lifetime();
    test_move_only();
    test_threaded_stress();
    if (g_failures == 0) {
        std::cout << "\nALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "\n" << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
