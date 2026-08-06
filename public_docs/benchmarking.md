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
*   **Execution**: Synthetic workloads execute via `./engine_benchmark`. Historical replays execute via `./engine_benchmark data/sample.csv`. Run `./engine_benchmark --help` for full CLI usage; an unrecognized extra argument is now a hard error (exit code 1) rather than silently falling back to the synthetic suite.
*   **Environment**: Benchmark results are meaningless without hardware context. All published results include exact compiler flags, architecture, and OS configurations.

## CI Coverage vs. Performance Gating
CI builds and runs the benchmark binary in both Debug (sanitizer) and Release (`-O3`), and smoke-tests it against `data/sample.csv`, so a crash or build failure in the exact configuration behind the published numbers gets caught automatically. Performance itself isn't CI-gated on purpose — shared runners have too much noise for a nanosecond-scale number to be a trustworthy pass/fail signal. Perf work stays a local activity, recorded by hand in `optimization_history.md`.

## Limitations & Threats to Validity
The current metrics are subject to the following known limitations:
*   **Observer Overhead**: The `std::chrono` syscall introduces measurable latency, which skews tail latency percentiles at the nanosecond scale.
*   **Scheduler Jitter**: Threads are currently not pinned to isolated cores (`taskset`), meaning P99.9 latencies likely capture OS interrupts rather than algorithmic stalls.
*   **Allocator Noise**: The use of standard heap allocation introduces page-fault variability.

## Phase 4: Comparing Implementations, Not Just Measuring One

A separate binary, `variant_benchmark`, runs the same three synthetic workloads through every `OrderBook` implementation side by side (1.0, 2.0, ...) using this same three-pass methodology, so the numbers are directly comparable. It's deliberately a different binary from `engine_benchmark` — the README's published 1.0/1.0.1 numbers come from `engine_benchmark` specifically, and this comparison should never be able to change that.

Where available, wall-clock numbers are paired with real hardware-counter evidence (Instruments CPU Counters, correlated to the correct variant/workload via `os_signpost` markers — see ADR-0019) rather than staying at the wall-clock level alone. 2.0's results (a consistent +33–35% throughput gain, and every hardware bottleneck category improving) are in `optimization_history.md`.

**Caveat that applies to any `variant_benchmark` run, not just 2.0's**: absolute throughput/latency numbers are sensitive to background system load in a way the *relative* comparison between variants (measured back-to-back, same process, same machine state) is not. Treat absolute figures from this binary as indicative, and re-measure on an idle machine before quoting them outside this project.
