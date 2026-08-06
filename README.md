# Ultra Low-Latency Matching Engine

[![C++ CI](https://github.com/kankaniakshat185/low-latency-matching-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/kankaniakshat185/low-latency-matching-engine/actions/workflows/ci.yml)

A limit order book and matching engine, built from scratch in C++20 — price-time priority, partial fills, limit and market orders. Single instrument, single thread, by design (see Known Limitations below). The point isn't just that it matches orders correctly; it's that every performance claim in this repo is backed by two things: a differential test proving the faster version is still correct, and real hardware counters, not just a wall-clock number.

## Architecture

Composition over inheritance, all the way down: `MatchingEngine` owns an `OrderBook`, which owns `PriceLevel`s. Nothing is virtual. That's not a style preference — it's what lets the internals (`std::map` vs. a flat array, `std::list` vs. an intrusive pool-backed list) get swapped out and profiled independently, without touching the matching logic itself or any call site above it. `MatchingEngine` is templated on the book type for exactly this reason; see Phase 4 below for what that bought.

## Benchmarking

Three synthetic workloads, each isolating a different part of the system: orders scattered across a wide price range, a 70/30 insert/cancel mix, and every order landing at the same price. Every benchmark run does a 10% warm-up pass first (discarded, not timed) to get the instruction cache and branch predictor out of their cold-start state, then measures throughput and per-operation latency in two separate passes — timing both in the same loop was an early bug here (see `optimization_history.md`'s 1.0.1 row) that made the throughput number partly measure its own stopwatch.

`std::chrono`'s own overhead (20-40ns per call, called twice per operation) is a real, acknowledged limit on how much to trust nanosecond-scale numbers from this harness — see `public_docs/benchmarking.md` for the rest of what this methodology does and doesn't account for.

## Baseline Performance (1.0)

The first working version uses `std::map`/`std::list`/`std::unordered_map` throughout — correct and easy to verify, deliberately not optimized yet. These are the numbers before any of Phase 4's data-structure work below.

**Environment**: Apple M2 (ARM64), macOS 26.5.1, `clang++ -O3 -std=c++20`

| Workload (1M Actions) | Throughput | Median Latency | P99 Latency |
| :--- | :--- | :--- | :--- |
| **Random Prices** | ~6.78 M actions/sec | 125 ns | 417 ns |
| **Heavy Cancels** | ~8.73 M actions/sec | 125 ns | 541 ns |
| **Worst-Case** | ~15.73 M actions/sec | 42 ns | 209 ns |

Worst-Case beating Random Prices by more than 2x here is the whole reason Phase 4 exists — it's a strong hint that `std::map`'s tree traversal is costing more than it looks like on paper, and Phase 4 below is what actually went and checked.

## Phase 4: The Comparative Study (done)

Phase 4 replaces the baseline's data structures one variable at a time, verifying correctness against the baseline after each change and benchmarking both wall-clock and (where available) hardware-counter evidence. Full detail is in [`public_docs/optimization_history.md`](public_docs/optimization_history.md) and the [Architecture Decision Log](public_docs/adr/README.md); the short version:

*   **2.0 (done)**: replaced `std::list<Order>`'s per-order heap allocation with an intrusive doubly-linked list backed by a fixed-capacity pool allocator. Price levels unchanged (still `std::map`). **+33–35% throughput** across all three workloads; every hardware bottleneck category improved (Instruments CPU Counters); the Worst-Case/Random-Prices throughput ratio barely moved (1.668 → 1.655) — allocation cost was real but wasn't what explained that persistent gap.
*   **3.0 (done)**: replaced `std::map<Price, PriceLevel>` with a flat, tick-indexed array plus an occupancy bitmap. **+23.7% (Random) and +24.2% (Heavy Cancels)** on top of 2.0 — but a genuine **−10.3% regression on Worst Case**, confirmed at the hardware-counter level, not noise. The mechanism: the array's bitmap scan starts from the edge with no cached "best price," so its cost scales with how far the sole occupied price is from that edge — cheap when many levels are active, expensive when there's exactly one. The standing hypothesis finally moved regardless: the Worst-Case/Random-Prices ratio dropped from ~1.65 to **1.195**. Full mechanism, and the identified-but-not-yet-built fix, in [ADR-0021](public_docs/adr/0021-flat-array-price-levels.md).
*   **4.0 (done)**: cached the best-price tick per side (closes 3.0's regression) and replaced the `OrderId → OrderLocation` cancellation index — a `std::unordered_map` since 1.0 — with a flat vector indexed directly by id. **+122.3% (Random), +97.1% (Heavy Cancels), +100.6% (Worst Case)** on top of 3.0 — every workload roughly doubled, including the one that regressed in 3.0, which is now the single largest relative gain of the three. The Worst-Case/Random-Prices ratio closes further still, to **1.086**. Instruments shows the fix is real: Discarded Bottleneck (branch-misprediction cost) drops sharply everywhere. A second category (Instruction Processing) rises in *percentage* terms everywhere too — resolved, not left hanging: its absolute cost held flat or dropped in every workload, and the percentage only rose because the total cycle count shrank even faster. Full breakdown in [ADR-0022](public_docs/adr/0022-cached-best-tick-and-flat-cancellation-index.md).
*   **Next**: Phase 4's comparative study (1.0 → 4.0) is functionally complete. What's left is closing the loop — a final cross-version summary and interview-prep write-up — not a new numbered version.

## Known Limitations & Non-Goals

This is a single-machine, single-instrument, single-threaded matching engine — deliberately, per the project's phased scope (see [Documentation](#documentation) below). If you're evaluating it for anything beyond that scope, these are the boundaries as of the current phase, not oversights:

*   **Single instrument.** `Order`/`Trade` carry no symbol field; one `MatchingEngine` is implicitly one order book.
*   **No self-trade prevention.** Two crossing orders match regardless of where they originated.
*   **No timestamps.** Orders and trades carry no arrival/execution time — the benchmark harness times *itself*, independent of any wall-clock event log.
*   **No persistence, network layer, or risk checks.** This is an in-memory library and a CLI benchmark harness, not a running service.
*   **Single-threaded.** No concurrent order ingestion; every call to `processOrder`/`cancelOrder` is expected to be sequential.

## Documentation
The full writeup — architecture, design decisions, and the whole optimization history — lives in [`public_docs/`](public_docs/):

*   [Architecture](public_docs/architecture.md)
*   [Matching Engine](public_docs/matching_engine.md)
*   [Benchmarking Methodology](public_docs/benchmarking.md)
*   [Design Decisions](public_docs/design_decisions.md)
*   [Optimization History](public_docs/optimization_history.md)
*   [Architecture Decision Log](public_docs/adr/README.md) — every decision, major or minor, individually dated
*   [Interview Prep](public_docs/interview_prep.md) — a condensed, talking-points version of Phase 4

There is also a `docs/` directory referenced in some of the writing above (a running "engineering notebook" / learning journal) — it's intentionally gitignored and local-only, not published, so a fresh clone of this repo won't have it. `public_docs/` is the polished, tracked counterpart meant for readers.

## Build Instructions
The project fetches GoogleTest automatically via CMake FetchContent on first build (requires a network connection). The engine and benchmark executables themselves have zero runtime dependencies.

```bash
mkdir build && cd build
cmake ..        # Downloads GoogleTest on first run
make

# Run correctness regression suite
./engine_tests

# Run performance benchmarks (1.0 baseline only — what the numbers above come from)
./engine_benchmark

# Run the Phase 4 comparative study (1.0 vs 2.0 vs 3.0 vs 4.0, side by side)
./variant_benchmark
```
