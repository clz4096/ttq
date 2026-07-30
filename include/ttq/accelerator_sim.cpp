#include "accelerator_sim.h"
#include <algorithm>
#include <cmath>
#include <limits>

LatencyResult compute_latency(const MatmulCommand& cmd, const AcceleratorConfig& config)
{
    uint64_t num_of_flops = 2 * cmd.m * cmd.k * cmd.n; // FLOPs standard formula for matrix multiplication
    uint64_t num_of_bytes = config.bytes_per_element * ((cmd.m * cmd.k) + (cmd.k * cmd.n) + (cmd.m * cmd.n));
    // const double arithmetic_intensity = static_cast<double>(num_of_flops) /
    // num_of_bytes;
    const double compute_time = static_cast<double>(num_of_flops) / config.flops_per_second;
    const double memory_time = static_cast<double>(num_of_bytes) / config.bandwidth_bytes_per_second;

    LatencyResult res;
    const double rel_epsilon = 2.0 * std::numeric_limits<double>::epsilon();
    if ((compute_time - memory_time) > std::max(compute_time, memory_time) * rel_epsilon) {
        res.latency_in_seconds = compute_time;
        res.bound_type = "compute";
    } else {
        res.latency_in_seconds = memory_time;
        res.bound_type = "memory";
    }

    return res;
}
