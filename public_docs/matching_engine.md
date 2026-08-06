# Matching Engine Implementation

## Overview
This document details the core matching algorithm implementation.

## Design
The engine processes each incoming order sequentially to guarantee strict determinism. 

*   **Priority Rule:** The engine enforces strict Price-Time priority (FIFO).
*   **Matching:** Incoming orders are evaluated against the resting OrderBook (bids against asks, asks against bids). The engine sweeps the book until the incoming order is either fully filled or the available opposing liquidity is exhausted.
*   **Partial Fills:** Resting orders decrement their quantity but maintain their iterator position.
*   **One matching path, not four:** Limit-buy, limit-sell, market-buy, and market-sell used to be four separate, near-identical functions. Now it's one `OrderBook::matchAgainst`, parameterized on side and an optional limit price — no limit price at all means a market order.

## Key Decisions
*   **Synchronous Execution:** The engine evaluates matches synchronously. There is no threading or actor model complexity introduced here, ensuring that behavioral tests serve as a deterministic regression suite.
*   **Iterator Preservation:** Order cancellation leverages `std::list::iterator` mapped by `OrderId`. The engine guarantees that these iterators are never invalidated during partial fills.
*   **Reject before matching, not during:** a zero-quantity order or an `OrderId` that's already resting gets rejected before any matching starts. Doing it up front, not just at insert time, also catches an order that would cross and trade against a resting order sharing its own id.

## Performance
The current algorithmic complexity for matching is O(K), where K represents the number of passive resting orders that must be traversed and filled by the incoming aggressive order.

## Limitations
*   Market orders currently sweep the book and discard any unfilled remainder. Future iterations may require more nuanced order types, but they are excluded here to maintain strict focus on the algorithmic core.
*   No self-trade prevention, no multi-symbol support, no order timestamps — see the README's Known Limitations section for the complete, current list of explicit non-goals.

## Correctness across implementations (Phase 4)

The matching rules above are now implemented twice — once in `OrderBook` (1.0) and once in `OrderBookV2` (2.0, an intrusive-list-backed variant) — and are required to produce byte-identical trade ledgers given the same input. This is checked directly, not assumed: a differential test suite replays the same randomized action sequence through both and asserts the resulting trades match exactly, trade-for-trade, including under adversarial conditions (deliberate `OrderId` reuse, an all-same-price worst case). Every future variant (3.0, 4.0) is held to the same bar before its performance numbers are trusted.
