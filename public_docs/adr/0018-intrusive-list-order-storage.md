# ADR-0018: 2.0 — intrusive linked list + pool allocator for order storage

Status: Accepted
Date: 2026-08-06

## Context

1.0's `PriceLevel` stores resting orders in `std::list<Order>` — one heap allocation per order, plus pointer-chasing through separately-allocated nodes on every traversal. 2.0 is the first step of the Phase 4 comparative study: change exactly this one thing, keep everything else (price levels still `std::map`) identical to 1.0, and measure what allocation alone was costing.

## Decision

`OrderNode` (order + `prev`/`next` as pool indices, not raw pointers — see ADR-0017's `OrderPool`) replaces `std::list<Order>`'s nodes. Every price level keeps a `headIndex`/`tailIndex` pair into the shared pool instead of owning a `std::list`. `OrderBookV2` mirrors `engine::OrderBook`'s public interface exactly (`hasOrder`/`addOrder`/`cancelOrder`/`matchAgainst`) so `MatchingEngineT<BookT>` — introduced specifically for this — can run either implementation with zero code changes elsewhere, and so the differential test harness can compare them directly.

Internally, `OrderBookV2` mirrors 1.0's `PriceLevel` operations one-for-one on purpose: `appendOrder` (add + adjust total), `removeOrder` (cancel path: unlink + adjust total), `decreaseQuantity` (match-path fill accounting, quantity only), `unlinkOnly` (pure list surgery, called after `decreaseQuantity` has already accounted for the trade — same split 1.0 has between `decreaseQuantity()` and the match loop's direct `orders.erase()`). Same shape, different mechanics, which is what makes reading the two side by side actually useful.

## Alternatives considered

Raw pointers instead of pool indices for `prev`/`next` were the obvious alternative and lost on a small margin: indices are half the size on a 64-bit build, remain meaningful for the pool's entire lifetime, and are easier to reason about in a debugger than a pointer into the middle of a `std::vector` that (in principle, if the pool ever became growable) could move. Given ADR-0017 already ruled out a growable pool, this is a smaller win than it would otherwise be — but it costs nothing to take.

A per-price-level pool (one arena per price, instead of one for the whole book) was considered and rejected: it multiplies the sizing problem from ADR-0017 (now every price level needs its own capacity guess) for no benefit, since orders move between "which price level owns this pool" boundaries never actually happens — one book-wide pool is simpler and exactly as fast.

## Consequences

First-try correctness: both differential tests (a 200,000-action random workload and a 50,000-action worst-case-same-price workload) passed against the 1.0 baseline without a single fix needed, and a standalone 500,000-action run under UBSan alone turned up nothing (ASan itself is unreliable on this dev machine independent of this code — confirmed by the pre-existing binary crashing identically under the same sanitizer config before any of 2.0 existed).

Measured result (see `optimization_history.md`'s 2.0 row for the full numbers and caveats): a consistent ~33–35% throughput improvement across all three synthetic workloads, with trade counts identical to 1.0 in every case. The more interesting finding is what *didn't* change — the Worst-Case/Random-Prices throughput ratio moved from 1.668 to 1.655, essentially nothing. Allocation was a real, substantial cost, but it is not the reason Worst Case outperforms Random Prices. That gap is still unexplained and is exactly what 3.0 (flat array price levels) is built to test next.
