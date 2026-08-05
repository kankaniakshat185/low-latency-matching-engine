# Ultra Low-Latency Matching Engine

[![C++ CI](https://github.com/kankaniakshat185/low-latency-matching-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/kankaniakshat185/low-latency-matching-engine/actions/workflows/ci.yml)

This repository evaluates the architectural design and performance limits of a single-machine C++20 matching engine. The objective is to establish a rigorous, evidence-based methodology for implementing and measuring low-latency financial systems.

## Architecture Overview
The engine strictly implements Price-Time Priority (FIFO) matching. 

The architecture favors composition over inheritance. The core hierarchy is structurally isolated: `MatchingEngine` → `OrderBook` → `PriceLevel`. This isolation ensures that internal data structures (e.g., `std::map` vs. contiguous memory pools) can be empirically profiled and replaced without violating the deterministic matching logic.

## Benchmarking Methodology
This project adheres to a strict principle: **No engineering conclusion is drawn without measured evidence.**

*   **Workloads**: The engine is evaluated against synthetic workloads isolating specific algorithmic paths (Random Prices, Heavy Cancels, Worst-Case Same Price).
*   **Warm-Up**: Benchmarks execute a 10% unmeasured warm-up phase to stabilize instruction caches and branch predictors.
*   **Measurement**: Latencies (Median, P90, P99) and throughput are captured utilizing `std::chrono` around specific action invocations. 
*   *(Note: The observer overhead of `std::chrono` is actively acknowledged as a threat to validity at the nanosecond scale).*

## Current Baseline Performance (Phase 1/2)

The current implementation (Version 1.0) intentionally utilizes standard library containers (`std::map`, `std::list`) to establish a mathematically correct, verified baseline.

**Environment**: Apple M2 (ARM64), macOS 14.5, `clang++ -O3 -std=c++20`

| Workload (1M Actions) | Throughput | Median Latency | P99 Latency |
| :--- | :--- | :--- | :--- |
| **Random Prices** | ~6.78 M actions/sec | 125 ns | 417 ns |
| **Heavy Cancels** | ~8.73 M actions/sec | 125 ns | 541 ns |
| **Worst-Case** | ~15.73 M actions/sec | 42 ns | 209 ns |

*(Observation: The Worst-Case scenario bypasses O(log P) heap traversal, indicating that `std::map` lookup overhead is likely the primary bottleneck).*

## Known Limitations & Non-Goals

This is a single-machine, single-instrument, single-threaded matching engine — deliberately, per the project's phased scope (see [Documentation](#documentation) below). If you're evaluating it for anything beyond that scope, these are the boundaries as of the current phase, not oversights:

*   **Single instrument.** `Order`/`Trade` carry no symbol field; one `MatchingEngine` is implicitly one order book.
*   **No self-trade prevention.** Two crossing orders match regardless of where they originated.
*   **No timestamps.** Orders and trades carry no arrival/execution time — the benchmark harness times *itself*, independent of any wall-clock event log.
*   **No persistence, network layer, or risk checks.** This is an in-memory library and a CLI benchmark harness, not a running service.
*   **Single-threaded.** No concurrent order ingestion; every call to `processOrder`/`cancelOrder` is expected to be sequential.

## Documentation
Extensive documentation detailing architectural tradeoffs, design decisions, and optimization history is available in the [`public_docs/`](public_docs/) directory.

*   [Architecture](public_docs/architecture.md)
*   [Matching Engine](public_docs/matching_engine.md)
*   [Benchmarking Methodology](public_docs/benchmarking.md)
*   [Design Decisions](public_docs/design_decisions.md)
*   [Optimization History](public_docs/optimization_history.md)

There is also a `docs/` directory referenced in some of the writing above (a running "engineering notebook" / learning journal) — it's intentionally gitignored and local-only, not published, so a fresh clone of this repo won't have it. `public_docs/` is the polished, tracked counterpart meant for readers.

## Build Instructions
The project fetches GoogleTest automatically via CMake FetchContent on first build (requires a network connection). The engine and benchmark executables themselves have zero runtime dependencies.

```bash
mkdir build && cd build
cmake ..        # Downloads GoogleTest on first run
make

# Run correctness regression suite
./engine_tests

# Run performance benchmarks
./engine_benchmark
```
