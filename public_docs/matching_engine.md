# Matching Engine Implementation

## How matching actually works here
Every incoming order gets processed synchronously, one at a time, in the order it arrives — no threading, no actor model, nothing concurrent to reason about. That's a deliberate simplicity, not a missing feature: it's what lets the test suite double as a real deterministic regression suite instead of something that only *usually* reproduces.

An incoming order sweeps the opposite side of the book (buys against asks, sells against bids), starting from the best price, until it's either fully filled or the opposing side runs out of liquidity. Price-time priority is enforced strictly: better prices trade first, and at the same price, whoever got there first trades first. A partial fill decrements a resting order's quantity in place — its position in the book (its iterator, in 1.0's case) never changes just because it got smaller.

This used to be four separate, near-identical functions — limit-buy, limit-sell, market-buy, market-sell — before collapsing into one `OrderBook::matchAgainst`, parameterized on side and an optional limit price. No limit price at all just means a market order; there was never a real reason for the market-order path to be a different function once the limit-price check became optional instead of mandatory.

Two more things worth stating plainly: cancellation is O(1) by construction, not by accident — the hash map from `OrderId` to iterator, plus `std::list`'s guarantee that an iterator survives insertions/deletions happening anywhere else in the same list, means a cancel never has to scan anything. And validation happens *before* matching starts, not partway through — a zero-quantity order or an `OrderId` that's already resting gets rejected up front, which incidentally also catches the case where an incoming order would cross and trade against a resting order that happens to share its own id.

## Complexity and what's deliberately out of scope
Matching itself is O(K), where K is however many resting orders the incoming one actually fills — not the size of the book, just the part of it that gets touched.

Market orders sweep and discard whatever doesn't fill; there's no support for more nuanced order types (stop orders, iceberg orders, and so on), and that's scope, not an oversight — see the README's Known Limitations section for the full, current list of what's deliberately not here yet (no self-trade prevention, no multi-symbol support, no timestamps).

## Correctness across implementations (Phase 4)

The matching rules above are now implemented four times — `OrderBook` (1.0), `OrderBookV2` (2.0, intrusive-list-backed), `OrderBookV3` (3.0, flat-array-backed), `OrderBookV4` (4.0, flat-array + cached best-price tick + flat cancellation index) — and every one is required to produce byte-identical trade ledgers given the same input. This is checked directly, not assumed: a differential test suite replays the same randomized action sequence through each and asserts the resulting trades match exactly, trade-for-trade, including under adversarial conditions (deliberate `OrderId` reuse, an all-same-price worst case). This is precisely what caught nothing wrong with 3.0's correctness even though its performance turned out mixed, and again with 4.0 even though its own hardware-counter data turned out more nuanced than the headline throughput number suggests (see `optimization_history.md`): a workload-specific regression or an uneven hardware trade-off is a tuning question, not a correctness one, and the two are verified completely separately.
