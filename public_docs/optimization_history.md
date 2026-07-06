# Optimization History

## Overview
This document tracks the measured evolution of the matching engine. We maintain a strict versioned history rather than overwriting past results.

## Historical Log

| Version | Environment | Optimization | Benchmark Before | Benchmark After | Profiler Evidence | Engineering Takeaway |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1.0** | Apple M2 (ARM64), macOS 14.5<br>`clang++ -O3 -std=c++20` | Baseline Implementation (`std::map` / `std::list`) | N/A | ~1.08 M actions/sec (Random)<br>Median: 250ns<br>P99: 2.2µs | None (Hypothesis phase) | Established verified correctness baseline. Observed significant throughput disparity between Random and Worst-Case workloads. |

## Future Work
*   Subsequent versions will introduce custom memory pools and contiguous array data structures. This table will be updated only after performance counters verify the root cause of latency reductions.
