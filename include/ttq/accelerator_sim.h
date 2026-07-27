#pragma once
#include <cstdint>

struct MatmulCommand {
  uint64_t m;
  uint64_t k;
  uint64_t n;
  uint64_t enqueue_time;
  uint64_t start_time;
  uint64_t complete_time;
};

struct AcceleratorConfig {
  double flops_per_second;
  double bandwidth_bytes_per_second;
  double bytes_per_element;
  double clock_freq;
};

struct LatencyResult {
  double latency_in_seconds;
  const char *bound_type; // compute or memory
};

LatencyResult compute_latency(const MatmulCommand &cmd,
                              const AcceleratorConfig &config);
