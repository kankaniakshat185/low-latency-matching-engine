# Optimization History

## Overview
This document tracks the measured evolution of the matching engine. We maintain a strict versioned history rather than overwriting past results.

## Historical Log

| Version | Environment | Optimization | Benchmark Before | Benchmark After | Profiler Evidence | Engineering Takeaway |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1.0** | Apple M2 (ARM64), macOS 14.5<br>`clang++ -O3 -std=c++20` | Baseline Implementation (`std::map` / `std::list`) | N/A | ~1.08 M actions/sec (Random)<br>Median: 250ns<br>P99: 2.2µs | None (Hypothesis phase) | Established verified correctness baseline. Observed significant throughput disparity between Random and Worst-Case workloads. |
| **1.0.1** | Apple M2 (ARM64), macOS 14.5<br>`clang++ -O3 -std=c++20` | *Not a code optimization* — fixed benchmark timer contamination (the harness was including per-op `std::chrono` sampling overhead inside the throughput-measurement pass, not just the dedicated latency pass). Same engine code as 1.0. | ~1.08 M actions/sec (Random), as above | ~6.78 M actions/sec (Random)<br>Median: 125ns<br>P99: 417ns | Before/after diff of the corrected harness against the same baseline binary | The 1.0 row's numbers were a measurement artifact, not the engine's real throughput — appended rather than overwritten so the correction itself is part of the record. `README.md`'s "Current Baseline Performance" table reflects this corrected harness; if you see the two disagree again, the harness (not the engine) is the first place to look. |

## Future Work
*   Subsequent versions will introduce custom memory pools and contiguous array data structures. This table will be updated only after performance counters verify the root cause of latency reductions.
