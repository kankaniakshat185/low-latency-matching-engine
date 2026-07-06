# Ultra Low-Latency Matching Engine

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
| **Random Prices** | ~1.08 M actions/sec | 250 ns | 2,208 ns |
| **Heavy Cancels** | ~1.21 M actions/sec | 250 ns | 2,333 ns |
| **Worst-Case** | ~2.75 M actions/sec | 84 ns | 667 ns |

*(Observation: The Worst-Case scenario bypasses O(log P) heap traversal, indicating that `std::map` lookup overhead is likely the primary bottleneck).*

## Documentation
Extensive documentation detailing architectural tradeoffs, design decisions, and optimization history is available in the [`public_docs/`](public_docs/) directory.

*   [Architecture](public_docs/architecture.md)
*   [Matching Engine](public_docs/matching_engine.md)
*   [Benchmarking Methodology](public_docs/benchmarking.md)
*   [Design Decisions](public_docs/design_decisions.md)
*   [Optimization History](public_docs/optimization_history.md)

## Build Instructions
The project contains zero external dependencies and compiles instantly.

```bash
mkdir build && cd build
cmake ..
make

# Run correctness regression suite
./engine_tests

# Run performance benchmarks
./engine_benchmark
```
