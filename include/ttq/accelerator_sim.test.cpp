#include "accelerator_sim.h"
#include <iomanip>
#include <iostream>
#include <vector>

void print_result(const MatmulCommand &cmd, const LatencyResult &result,
                  const AcceleratorConfig &config) {
  uint64_t flops = 2 * cmd.m * cmd.k * cmd.n;
  uint64_t bytes = config.bytes_per_element *
                   ((cmd.m * cmd.k) + (cmd.k * cmd.n) + (cmd.m * cmd.n));
  double ai = static_cast<double>(flops) / bytes;
  double cycles = result.latency_in_seconds * config.clock_freq;

  std::cout << std::fixed << std::setprecision(2) << "(" << cmd.m << ","
            << cmd.k << "," << cmd.n << ")" << " | FLOPs=" << flops
            << " | bytes=" << bytes << " | AI=" << ai
            << " | bound=" << result.bound_type
            << " | latency_s=" << std::scientific << result.latency_in_seconds
            << " | cycles=" << std::fixed << cycles << '\n';
}

int main() {
  AcceleratorConfig config{.flops_per_second = 1e12,
                           .bandwidth_bytes_per_second = 1e12,
                           .bytes_per_element = 2.0,
                           .clock_freq = 1e9};

  std::vector<MatmulCommand> tests = {
      {2, 2, 2},    {5, 5, 5},    {10, 10, 10},   {20, 20, 20},
      {25, 50, 75}, {30, 60, 90}, {100, 100, 100}};

  std::cout << "=== ACCELERATOR TIMING SIMULATOR — COST MODEL TEST ===\n";
  std::cout << "Config: " << config.flops_per_second << " FLOP/s, "
            << config.bandwidth_bytes_per_second << " B/s, "
            << config.bytes_per_element << " bytes/elem, " << config.clock_freq
            << " Hz\n\n";

  for (const auto &cmd : tests) {
    auto result = compute_latency(cmd, config);
    print_result(cmd, result, config);
  }

  return 0;
}
