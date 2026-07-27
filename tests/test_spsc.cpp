// Minimal test harness (no external deps) for ttq::SPSCQueue.
// Build via CMake, run ./test_spsc. Exit code 0 = all passed.
//
// Your job tonight: implement push/pop/size_approx in the header until every
// check below prints PASS. Start with the single-threaded ones; do the
// threaded stress test last (it will catch memory-ordering bugs).

#include "ttq/spsc_queue.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (cond) {                                                                \
      std::cout << "PASS: " << #cond << "\n";                                  \
    } else {                                                                   \
      std::cout << "FAIL: " << #cond << "  (line " << __LINE__ << ")\n";       \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static void test_basic_push_pop() {
  ttq::SPSCQueue<int, 4> q;
  CHECK(!q.pop().has_value()); // empty pop -> nullopt
  CHECK(q.push(42));           // first push ok
  auto v = q.pop();
  CHECK(v.has_value() && *v == 42); // get it back
  CHECK(!q.pop().has_value());      // empty again
}

static void test_fill_and_full() {
  // Capacity 4. Depending on your full/empty scheme you can store either
  // 3 or 4 items. This test only assumes you can store at least 3 and that
  // push eventually returns false when full. Adjust the expected count to
  // match the scheme you choose, and delete this comment when you do.
  ttq::SPSCQueue<int, 4> q;
  int stored = 0;
  while (q.push(stored))
    ++stored;
  CHECK(stored >= 3);  // stored at least capacity-1
  CHECK(!q.push(999)); // now full -> push fails
  // drain in FIFO order
  for (int i = 0; i < stored; ++i) {
    auto v = q.pop();
    CHECK(v.has_value() && *v == i); // FIFO: comes out in order pushed
  }
  CHECK(!q.pop().has_value());
}

static void test_wraparound() {
  // Push/pop repeatedly so the indices wrap past Capacity many times.
  ttq::SPSCQueue<int, 4> q;
  for (int round = 0; round < 100; ++round) {
    CHECK(q.push(round));
    auto v = q.pop();
    CHECK(v.has_value() && *v == round);
  }
}

static void test_threaded_stress() {
  // One producer, one consumer, N items. Verify none lost, none duplicated,
  // strict FIFO. This is the test that fails if your acquire/release is wrong.
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

int main() {
  test_basic_push_pop();
  test_fill_and_full();
  test_wraparound();
  test_threaded_stress();
  if (g_failures == 0) {
    std::cout << "\nALL TESTS PASSED\n";
    return 0;
  }
  std::cout << "\n" << g_failures << " CHECK(S) FAILED\n";
  return 1;
}
