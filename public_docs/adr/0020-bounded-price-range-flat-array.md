#ADR - 0020 : Bounded, tick - indexed price range for 3.0

Status: Accepted
Date: 2026-08-06

## Context

3.0 replaces `std::map<Price, PriceLevel>` with a flat array indexed by price tick, to isolate whether tree-traversal/cache-miss cost on the price-level lookup is what's driving the still-unexplained Random-vs-Worst-Case throughput gap (2.0 ruled out allocation as the cause; see ADR-0018). A flat array needs a decision `std::map` never forced: how big is the array, and what happens at the edges.

## Decision

`OrderBookV3` takes `minPrice`, `maxPrice`, and `tickSize` as constructor arguments and pre-allocates one slot per tick in `[minPrice, maxPrice]` — no dynamic level creation or destruction at all. A price outside that range throws `std::out_of_range` immediately, rather than being silently clamped or routed somewhere unexpected. For the Phase 4 benchmark comparison specifically, this is constructed with `minPrice=9000, maxPrice=11000, tickSize=1` — exactly the range every synthetic workload generator already uses.

## Alternatives considered

Unbounded/dynamic growth (resizing the array when a price falls outside the current range) was the obvious "make it general-purpose" alternative, and it's rejected for the same reason ADR-0017 rejected a growable order pool: a resize is an allocation event at an unpredictable time, and the whole point of a flat array is *not* paying allocation costs on the hot path. If 3.0 needs that, it isn't really testing "does a flat array beat a tree," it's testing "does a flat array that sometimes behaves like a tree beat a tree" — a different, muddier question.

Silently clamping out-of-range prices to the nearest valid tick was considered and rejected for the same reason CSV validation (ADR-0006) rejects silent coercion: a clamped price is a different price, and matching against it would be matching at the wrong price without any signal that happened.

A bounded price range is also not a toy simplification unique to this benchmark — real exchanges enforce price bands/circuit breakers limiting how far an order's price can be from the last trade, for entirely unrelated (risk-management) reasons. A flat array's bounded-range requirement happens to line up with a constraint real systems already have.

## Consequences

3.0 cannot represent a price outside its configured range, full stop — this is a real, narrower scope than 1.0/2.0 (which use `std::map` and accept any `Price` value), and it's an explicit tradeoff being made for this comparison, not an oversight. The differential tests need a workload whose prices are known to fit the configured range in advance (straightforward here, since the range was chosen to match the workload generators exactly) — a real system would need this range chosen deliberately for its actual price universe, the same "sized for the workload, not guessed" discipline as ADR-0017's pool capacity.
