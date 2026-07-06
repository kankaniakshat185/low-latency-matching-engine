# Ultra Low-Latency Matching Engine

This repository contains the source code for a FAANG/HFT interview-grade C++ matching engine. The project prioritizes **depth over breadth**, focusing entirely on building the fastest possible single-machine matching engine.

## Core Philosophy

This project intentionally excludes external features like networking, databases, frontend dashboards, and authentication. Every component exists for one of the following reasons:
*   Correctness
*   Performance
*   Modularity
*   Maintainability
*   Engineering Quality

The engine is built iteratively, adhering strictly to the principle of **Measurement Before Optimization**. 

## Execution Phases

Development is structured into five distinct phases:

1.  **Phase 1: Baseline & Correctness** (Status: **Complete**) - Established a mathematically correct O(log P) baseline using `std::map` and `std::list`. All matching logic (Price-Time Priority) is verified against a strict regression suite.
2.  **Phase 2: Measurement & Benchmarking** (Status: Pending) - Implementing synthetic workloads and highly accurate latency/throughput tracking.
3.  **Phase 3: Statistics & Replay Engine** (Status: Pending) - Building a fast parser to replay historical market datasets for realistic bursty testing.
4.  **Phase 4: Profiling & Optimization** (Status: Pending) - Using `perf` to identify bottlenecks and replacing standard containers with optimized structures (contiguous arrays, memory pools).
5.  **Phase 5: The Engineering Notebook** (Status: Ongoing) - Extensive documentation of every architectural decision, tradeoff, and optimization.

## Optimization History

*(This section will be populated during Phase 4 as optimizations are implemented and benchmarked against the Phase 1 baseline).*

| Version | Optimization | Reason (Profiling Data) | Benchmark Before | Benchmark After | Lessons Learned |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1.0 | Baseline (std::map/std::list) | N/A - Establish correctness | TBD | TBD | Correctness verified. |

## Build Instructions

This project requires a C++20 compliant compiler.

```bash
mkdir build && cd build
cmake ..
make
./engine_tests
```

## Documentation

The project includes an extensive `docs/` directory acting as an "Engineering Notebook" that details the architecture, design decisions, memory layouts, algorithmic complexities, and a learning journal. 
