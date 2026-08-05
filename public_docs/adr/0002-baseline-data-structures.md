# ADR-0002: `std::map`/`std::list`/`std::unordered_map` as the Phase 1 baseline

Status: Accepted
Date: 2026-07-06

## Context

`OrderBook` needs price levels kept sorted (best bid/ask first), FIFO ordering within a level, and cancellation fast enough not to dominate the workload. Phase 4 is explicitly set aside for replacing all of this with cache-friendlier structures — contiguous arrays, intrusive lists, memory pools — which means Phase 1's job was to optimize for provable correctness, not speed. It's the yardstick everything after it gets measured against.

## Decision

- `std::map<Price, PriceLevel, std::greater<Price>>` for bids, `std::less<Price>` for asks — red-black trees, so the comparator gives us "best price first" for free.
- `std::list<Order>` inside each `PriceLevel` for time priority, chosen specifically because it guarantees iterator stability across insertion and unrelated erasure.
- `std::unordered_map<OrderId, OrderLocation>` mapping an id straight to its `std::list<Order>::iterator`, so cancellation is a hash lookup plus a list erase — O(1), not a linear scan.

## Alternatives considered

A contiguous array of price levels, indexed by price or price-tick offset, was on the table and is *not* rejected — it's deferred. Fast for dense price ranges, memory-hungry for sparse ones, and premature before correctness was even established. This is literally the first thing on the Phase 4 list.

`std::vector<Order>` instead of `std::list` for price-level storage didn't survive contact with the cancellation requirement: erasing from the middle of a vector is O(N) and invalidates iterators, which breaks the whole O(1)-cancel design. `std::list` costs cache locality to buy iterator stability, and Phase 1 needed the latter more than the former.

`std::unordered_map<Price, PriceLevel>` instead of `std::map` doesn't maintain sorted order, so finding the best bid/ask would mean scanning every key — a non-starter for something done on every single order.

## Consequences

The baseline is correct and easy to reason about, and every later optimization has a known-good reference to diff against — which is exactly what the book-invariant fuzz test and the regression suite check on every change. The cost is real and already showing up: `std::map`/`std::list` are both node-based, every insert is a heap allocation, and traversal is pointer-chasing with poor cache locality. That's the entire reason Phase 4 exists.

One thing this baseline surfaced that we don't fully understand yet: the "Worst Case (Same Price)" benchmark currently *beats* "Random Prices," which is backwards from what you'd expect if `std::map` lookups were free. The working theory is cache misses on the red-black tree traversal dominating over `std::list`'s O(1) append cost, but that's a hypothesis, not a measurement — Phase 4 is where `perf stat -e cache-misses` either confirms it or doesn't.
