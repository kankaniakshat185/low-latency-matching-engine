# Benchmarking Methodology

## Overview
This document specifies the rigorous benchmarking philosophy governing this repository.

## Design & Philosophy
Benchmarking within this project produces *observations*, not conclusions.

1.  **Observation**: The benchmark output is recorded.
2.  **Hypothesis**: We hypothesize the root cause (e.g., cache misses, allocation overhead).
3.  **Profiling**: The hypothesis is tested using hardware counters (e.g., `perf`).
4.  **Verification**: Profiling results verify the hypothesis.
5.  **Optimization**: The codebase is modified to address the profiled bottleneck.

## Methodology
*   **Measurement**: Latency is measured via `std::chrono::high_resolution_clock` encapsulating exactly one action (`processOrder` or `cancelOrder`).
*   **Warm-up**: Every benchmark executes a 10% unmeasured warm-up phase to stabilize instruction caches and branch predictors before timing begins.
*   **Workloads**: We utilize synthetic workloads (Random Prices, Heavy Cancels, Worst-Case Same Price) as well as historical Limit Order Book (LOB) replays. A small sample historical dataset is provided (`data/sample.csv`). Larger public datasets for extensive historical replay benchmarking can be sourced from providers such as LOBSTER.
*   **Execution**: Synthetic workloads execute via `./engine_benchmark`. Historical replays execute via `./engine_benchmark data/sample.csv`.
*   **Environment**: Benchmark results are meaningless without hardware context. All published results include exact compiler flags, architecture, and OS configurations.

## Limitations & Threats to Validity
The current metrics are subject to the following known limitations:
*   **Observer Overhead**: The `std::chrono` syscall introduces measurable latency, which skews tail latency percentiles at the nanosecond scale.
*   **Scheduler Jitter**: Threads are currently not pinned to isolated cores (`taskset`), meaning P99.9 latencies likely capture OS interrupts rather than algorithmic stalls.
*   **Allocator Noise**: The use of standard heap allocation introduces page-fault variability.
