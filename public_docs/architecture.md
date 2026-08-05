# Architecture Overview

## Overview
This document outlines the high-level architecture of the matching engine. The system is designed as a focused, single-machine C++20 engine that evaluates algorithmic efficiency and cache locality. 

## Design
The repository strictly favors composition over inheritance. 

The core object hierarchy is strictly defined:
*   `MatchingEngine` owns exactly one `OrderBook`.
*   `OrderBook` manages two maps of `PriceLevel`s (Bids and Asks) **and owns the matching algorithm itself** (`OrderBook::matchAgainst`) — it is not just a passive container.
*   `PriceLevel` encapsulates a priority queue (currently `std::list`) of `Order` structs.

This modularity isolates the matching logic from the internal data structures, allowing future replacement of standard library containers with custom allocators and contiguous memory layouts without breaking the core engine.

`MatchingEngine` is deliberately thin now: validate the order, delegate to `OrderBook::matchAgainst`, decide whether to rest an unfilled remainder. It has no access to `OrderBook`'s maps or `PriceLevel`'s order list at all — those are private to the classes that own them.

## Key Decisions
*   **Zero External Dependencies:** The engine compiles instantly and has no runtime dependencies.
*   **Separation of Concerns:** Benchmarking mechanics (`BenchmarkConfig`, `WorkloadGenerator`) are entirely isolated from the engine itself, preventing benchmark-specific code from polluting the critical path.
*   **Validation at the boundaries:** `OrderBook` rejects a duplicate `OrderId` instead of overwriting its cancel index; `CSVParser` rejects malformed or negative input instead of coercing it. Both backed by tests, including a fuzz test that reuses ids on purpose across 20,000 operations and checks the book's invariants still hold.

## Tradeoffs
*   **Flexibility vs. Performance:** The current baseline utilizes `std::map` and `std::list`. While this provides a mathematically correct O(log P) lookup and O(1) cancel guarantee, it sacrifices cache locality. This tradeoff was intentionally made to establish a verified behavioral baseline before introducing cache-aware structures.

## Future Work
*   Replace `std::map` with an array-backed flat map to eliminate node allocations.
*   Implement custom memory pools to eliminate OS page faults during burst allocations.
