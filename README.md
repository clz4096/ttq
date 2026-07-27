# ttq — a mock device command runtime

A small systems project modeling the core of a hardware command runtime (the
kind that sits between a host CPU and an accelerator): the host enqueues
commands, a dispatcher thread drains and executes them, and we measure
end-to-end latency.

Built to explore the **hardware/software boundary in C++** — command queues,
single-dispatch execution, host↔device data movement, and lock-free
concurrency.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure   # or: ./build/test_spsc
```

Requires a C++20 compiler.

## Roadmap

- [ ] **Rung 1 — Lock-free SPSC ring buffer** (`include/ttq/spsc_queue.hpp`)
      Single-producer / single-consumer, bounded, no locks. The channel
      between host and dispatcher.
- [ ] **Rung 2 — Command struct + dispatch loop.** Producer thread enqueues
      `Command{op, payload}`; consumer thread drains and "executes."
- [ ] **Rung 3 — Latency measurement.** Timestamp enqueue → complete; report
      p50/p99. Turns a toy into a systems project.
- [ ] **Rung 4 (stretch) — host→device staging** or a second dispatch queue.

## Design notes

### Why SPSC (single-producer, single-consumer)?
The host is one writer; the dispatcher is one reader. That exact-one-each
contract is what removes the need for locks: with a single producer and a
single consumer, two atomic indices are sufficient to synchronize.

### Distinguishing empty from full
Both conditions can present as `head == tail`. Two standard resolutions:
- **Sacrifice one slot:** capacity holds `N-1` items; "full" is
  `next(tail) == head`.
- **Free-running counters + masking:** "full" is `tail - head == N`.

(Implementation picks one — see header.)

### Memory ordering
The producer publishes the tail index with **release**; the consumer observes
it with **acquire**. That release/acquire pair orders the buffer write *before*
the index update becomes visible, so the consumer never reads a slot the
producer hasn't finished writing. The mirror applies to the head index. An
index a thread solely owns is read with **relaxed**. Full reasoning is in the
header comments.
