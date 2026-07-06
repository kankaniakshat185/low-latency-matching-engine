# Architecture Overview

## Overview
This document outlines the high-level architecture of the matching engine. The system is designed as a focused, single-machine C++20 engine that evaluates algorithmic efficiency and cache locality. 

## Design
The repository strictly favors composition over inheritance. 

The core object hierarchy is strictly defined:
*   `MatchingEngine` owns exactly one `OrderBook`.
*   `OrderBook` manages two maps of `PriceLevel`s (Bids and Asks).
*   `PriceLevel` encapsulates a priority queue (currently `std::list`) of `Order` structs.

This modularity isolates the matching logic from the internal data structures, allowing future replacement of standard library containers with custom allocators and contiguous memory layouts without breaking the core engine.

## Key Decisions
*   **Zero External Dependencies:** The engine compiles instantly and has no runtime dependencies.
*   **Separation of Concerns:** Benchmarking mechanics (`BenchmarkConfig`, `WorkloadGenerator`) are entirely isolated from the engine itself, preventing benchmark-specific code from polluting the critical path.

## Tradeoffs
*   **Flexibility vs. Performance:** The current baseline utilizes `std::map` and `std::list`. While this provides a mathematically correct O(log P) lookup and O(1) cancel guarantee, it sacrifices cache locality. This tradeoff was intentionally made to establish a verified behavioral baseline before introducing cache-aware structures.

## Future Work
*   Replace `std::map` with an array-backed flat map to eliminate node allocations.
*   Implement custom memory pools to eliminate OS page faults during burst allocations.
