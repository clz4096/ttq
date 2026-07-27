#include "accelerator_sim.h"
#include "spsc_queue.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <thread>
#include <vector>

// ── Timestamp helper ─────────────────────────────────────────────

uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// ── Producer ─────────────────────────────────────────────────────

void producer(auto &queue, int num_commands, const auto &test_list) {
  std::random_device rd;
  std::mt19937 gen(rd());
  auto size = test_list.size();
  std::uniform_int_distribution<size_t> dist(0, size - 1);

  for (size_t i = 0; i < num_commands; ++i) {
    const auto random_idx = dist(gen);
    auto cmd = test_list[random_idx % size];

    cmd.enqueue_time = now_ns();
    while (!queue.push(cmd))
      ;
  }

  std::cout << "produced " << num_commands << " commands\n";
}

// ── Dispatcher ───────────────────────────────────────────────────

void dispatcher(auto &queue, const auto &config, auto num_commands,
                auto &command_result_vec, auto &latency_result_vec) {

  size_t processed = 0;
  while (processed < num_commands) {
    auto cmd_opt = queue.pop();
    if (!cmd_opt.has_value())
      continue; // spin until data arrives
    auto cmd = cmd_opt.value();
    cmd.start_time = now_ns();
    LatencyResult latency_res = compute_latency(cmd, config);
    uint64_t latency_ns =
        static_cast<uint64_t>(latency_res.latency_in_seconds * 1e9);
    cmd.complete_time = cmd.start_time + latency_ns;
    command_result_vec.emplace_back(cmd);
    latency_result_vec.emplace_back(latency_res);
    ++processed;
  }

  std::cout << "dispatched " << num_commands << " commands\n";
}

// ── Per-command metrics ──────────────────────────────────────────

struct CommandMetrics {
  uint64_t flops;
  uint64_t bytes;
  double arithmetic_intensity;
  double cycles;
};

CommandMetrics compute_metrics(const MatmulCommand &cmd,
                               const LatencyResult &res,
                               const AcceleratorConfig &config) {
  uint64_t flops = 2 * cmd.m * cmd.k * cmd.n;
  uint64_t bytes = config.bytes_per_element *
                   ((cmd.m * cmd.k) + (cmd.k * cmd.n) + (cmd.m * cmd.n));
  double ai = static_cast<double>(flops) / bytes;
  double cycles = res.latency_in_seconds * config.clock_freq;
  return {flops, bytes, ai, cycles};
}

// ── Output: stdout + CSV file ────────────────────────────────────

void write_results(const std::vector<MatmulCommand> &commands,
                   const std::vector<LatencyResult> &latencies,
                   const AcceleratorConfig &config, const char *filename) {
  std::ofstream out(filename);
  if (!out) {
    std::cerr << "Failed to open " << filename << '\n';
    return;
  }

  out << "m,k,n,FLOPs,bytes,AI,bound,latency_s,cycles\n";

  for (size_t i = 0; i < commands.size(); ++i) {
    const auto &cmd = commands[i];
    const auto &res = latencies[i];
    auto metrics = compute_metrics(cmd, res, config);

    // stdout
    std::cout << std::fixed << std::setprecision(2) << "(" << cmd.m << ","
              << cmd.k << "," << cmd.n << ")" << " | FLOPs=" << metrics.flops
              << " | bytes=" << metrics.bytes
              << " | AI=" << metrics.arithmetic_intensity
              << " | bound=" << res.bound_type
              << " | latency_s=" << std::scientific << res.latency_in_seconds
              << " | cycles=" << std::fixed << metrics.cycles << '\n';

    // CSV
    out << cmd.m << ',' << cmd.k << ',' << cmd.n << ',' << metrics.flops << ','
        << metrics.bytes << ',' << std::fixed << std::setprecision(4)
        << metrics.arithmetic_intensity << ',' << std::quoted(res.bound_type)
        << ',' << std::scientific << std::setprecision(6)
        << res.latency_in_seconds << ',' << std::fixed << std::setprecision(0)
        << metrics.cycles << '\n';
  }

  out.close();
}

// ── Aggregate statistics ─────────────────────────────────────────

struct AggregateStats {
  uint64_t total_cycles;
  uint64_t p50_cycles;
  uint64_t p99_cycles;
  size_t command_count;
};

AggregateStats compute_aggregates(const std::vector<LatencyResult> &results,
                                  const AcceleratorConfig &config) {
  std::vector<uint64_t> cycle_latencies;
  cycle_latencies.reserve(results.size());
  for (const auto &res : results) {
    cycle_latencies.push_back(
        static_cast<uint64_t>(res.latency_in_seconds * config.clock_freq));
  }

  std::sort(cycle_latencies.begin(), cycle_latencies.end());

  uint64_t total = 0;
  for (auto c : cycle_latencies)
    total += c;

  size_t p50_idx = cycle_latencies.size() * 0.50;
  size_t p99_idx = cycle_latencies.size() * 0.99;

  return {
      .total_cycles = total,
      .p50_cycles = cycle_latencies[p50_idx],
      .p99_cycles = cycle_latencies[p99_idx],
      .command_count = cycle_latencies.size(),
  };
}

void print_aggregates(const AggregateStats &stats) {
  std::cout << "──────────────────────────────────────────\n"
            << "Total commands: " << stats.command_count << '\n'
            << "Total cycles:   " << stats.total_cycles << '\n'
            << "p50 latency:    " << stats.p50_cycles << " cycles\n"
            << "p99 latency:    " << stats.p99_cycles << " cycles\n";
}

// ── Entry point ──────────────────────────────────────────────────

int main() {
  AcceleratorConfig config{.flops_per_second = 1e12,
                           .bandwidth_bytes_per_second = 1e12,
                           .bytes_per_element = 2.0,
                           .clock_freq = 1e9};

  std::vector<MatmulCommand> tests = {
      {2, 2, 2},    {5, 5, 5},    {10, 10, 10},   {20, 20, 20},
      {25, 50, 75}, {30, 60, 90}, {100, 100, 100}};

  constexpr size_t num_commands{256};
  ttq::SPSCQueue<MatmulCommand, num_commands> q;
  std::vector<MatmulCommand> command_result_vec;
  std::vector<LatencyResult> latency_result_vec;

  std::thread prod([&]() { producer(q, num_commands, tests); });
  std::thread dispatch([&]() {
    dispatcher(q, config, num_commands, command_result_vec, latency_result_vec);
  });
  prod.join();
  dispatch.join();

  write_results(command_result_vec, latency_result_vec, config, "results.txt");
  auto stats = compute_aggregates(latency_result_vec, config);
  print_aggregates(stats);

  return 0;
}
