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

## Phase 4: the swap actually happening

`MatchingEngine` is now `MatchingEngineT<BookT>`, a template — the concrete `MatchingEngine` used everywhere is `MatchingEngineT<OrderBook>`, but nothing about it is special-cased to that one book type. Two further implementations now exist in `src/structures/`, each swapping one thing and keeping the rest fixed: `OrderBookV2` replaces `std::list<Order>` with an intrusive linked list and a fixed-capacity pool allocator (price levels still `std::map`); `OrderBookV3` additionally replaces `std::map<Price, PriceLevel>` itself with a flat, tick-indexed array and an occupancy bitmap. All three run through the identical `MatchingEngineT` with zero changes anywhere else, because every book exposes the same public interface. This is the composition-over-inheritance bet from the top of this document actually paying off, not just an aspiration: every new variant is verified against the 1.0 baseline with differential testing (byte-identical trade ledgers on the same input) before its numbers are trusted.

Not every swap is a clean win, and that's worth stating plainly here rather than only in the detailed history: 3.0 improved two of three benchmark workloads substantially but measurably *regressed* the third (Worst-Case-Same-Price), for an identifiable, mechanistic reason — see ADR-0021. The architecture's job is to make swaps like this cheap to try and cheap to verify, not to guarantee every swap helps.

## Future Work
*   4.0: reconsider the `OrderId → OrderLocation` cancellation index for cache-friendlier lookups, and address 3.0's identified Worst-Case regression (caching the best-price tick instead of always scanning from the array edge).
*   Full detail and results so far: [`public_docs/optimization_history.md`](optimization_history.md), [the Architecture Decision Log](adr/README.md).
