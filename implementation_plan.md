# Ultra Low-Latency Matching Engine: Implementation Blueprint

This document serves as the definitive blueprint for building a FAANG/HFT interview-grade C++ matching engine. The objective is **depth over breadth**. We are building the fastest possible single-machine matching engine, deliberately excluding external features (networking, databases, auth) to focus exclusively on systems engineering, correctness, performance optimization, and clean architecture. 

## User Review Required

> [!IMPORTANT]
> I have updated the blueprint to include an explicit, phased execution strategy. Please review the phases at the bottom to ensure they represent the iterative development path you want to take. Once approved, we will begin Phase 1.

## 1. Core Philosophy

*   **Depth > Breadth:** The project should demonstrate profound engineering quality rather than an excessive feature list.
*   **Purpose-Driven Components:** Every piece of code exists for correctness, performance, modularity, maintainability, or interview discussion.
*   **Engineering Quality:** The final product must read like a systems engineer's codebase, prioritizing clean C++ and verifiable optimizations.

## 2. Development Philosophy

Development will rigidly adhere to the following sequence:

1.  **Correctness:** Establish a baseline that works flawlessly. No optimization without verification.
2.  **Measurement:** Build robust benchmarking and metrics tracking.
3.  **Profiling:** Use data (not intuition) to identify bottlenecks.
4.  **Optimization:** Implement targeted improvements.
5.  **Documentation:** Record the "why" and "how" of every decision.

*Every optimization will be backed by benchmark numbers rather than assumptions.*

## 3. Architecture & Code Quality

The architecture must be extremely modular, adhering to clean OOP principles and strong separation of concerns. 

*   **Composition over Inheritance:** Avoid God classes and deep inheritance trees.
*   **Interchangeable Components:** The engine must be designed so that core data structures can be swapped out without cascading rewrites.
*   **Modern C++20:** Leverage modern features appropriately.
*   **Best Practices:** RAII, const correctness, move semantics, high cohesion, minimal coupling.
*   **Readability:** Prioritize descriptive naming and readable code over clever, overly-abstract template metaprogramming.

```
src/
├── engine/          # Matching logic, OrderBook, PriceLevels
├── structures/      # Interchangeable data structure implementations
├── metrics/         # Lightweight statistics tracking
├── replay/          # Ingestion and replay logic
├── benchmark/       # Performance testing suites
├── utils/           # Timekeeping, allocators, etc.
└── tests/           # Correctness verification
docs/                # The Engineering Notebook
```

## 4. Execution Phases

We will build the engine iteratively, proving correctness and performance at each step before moving forward.

### Phase 1: Baseline & Correctness
*   Scaffold the core architecture (`src/engine/`, `src/structures/`, `tests/`).
*   Implement baseline data structures (`std::map`, `std::unordered_map`, `std::deque`).
*   Implement standard Price-Time Priority matching for Limit and Market orders.
*   Write exhaustive unit tests covering edge cases, partial fills, cancellations, and FIFO queues.
*   *Exit Criteria:* 100% test pass rate. Absolute correctness proven. No performance tuning yet.

### Phase 2: Measurement & Benchmarking
*   Scaffold `src/benchmark/` and `src/utils/`.
*   Implement the benchmark suite to generate synthetic workloads (Random, Sequential, Worst-case, Heavy Cancels).
*   Create a beautifully formatted terminal output tracking Orders/sec, Matches/sec, and Latency percentiles (P50, P95, P99).
*   *Exit Criteria:* A reliable, automated way to measure the baseline implementation's performance.

### Phase 3: Statistics & Replay Engine
*   Scaffold `src/metrics/` and `src/replay/`.
*   Implement the lightweight statistics module to track volume, spread, and queue depth.
*   Build a fast parser to ingest and replay historical market datasets or large-scale synthetic files.
*   *Exit Criteria:* Ability to benchmark the engine under realistic, bursty conditions using replay data.

### Phase 4: Profiling & Optimization (Version 2)
*   Profile the baseline engine using `perf`, `valgrind`, or `callgrind`.
*   Identify primary bottlenecks (e.g., cache misses, allocation overhead).
*   Implement alternative, optimized data structures (e.g., contiguous arrays, custom memory pools, intrusive lists) in `src/structures/`.
*   Benchmark the new structures against the baseline.
*   Begin logging the **Optimization History** (Version | Optimization | Reason | Before | After | Lessons).
*   *Exit Criteria:* At least one major, proven performance gain documented in the history.

### Phase 5: The Engineering Notebook
*   Flesh out the `docs/` directory.
*   Write detailed documentation answering the "why" for architecture, data structures, and the matching algorithm.
*   Document Big-O complexities and memory layouts based on the profiling and optimization phases.
*   *Exit Criteria:* A complete, educational project repository ready for a FAANG/HFT interview discussion.

## 5. Data Structures Strategy

We will **not** prematurely commit to one implementation. Candidates evaluated in Phase 4 will include:
*   `std::map` and `std::unordered_map` (Baseline Phase 1)
*   Contiguous price arrays
*   Skip lists
*   Intrusive linked lists
*   Custom memory pools vs. standard `malloc`

## 6. Optimization History

The `README.md` will contain an **Optimization History** tracking:

| Version | Optimization | Reason (Profiling Data) | Benchmark Before | Benchmark After | Lessons Learned |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1.1 | Custom Memory Pool | `malloc` was 40% of latency | 2.1M orders/sec | 5.8M orders/sec | Avoiding OS page faults is critical... |
