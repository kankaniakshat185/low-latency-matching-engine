# Matching Engine Implementation

## Overview
This document details the core matching algorithm implementation.

## Design
The engine processes each incoming order sequentially to guarantee strict determinism. 

*   **Priority Rule:** The engine enforces strict Price-Time priority (FIFO).
*   **Matching:** Incoming orders are evaluated against the resting OrderBook (bids against asks, asks against bids). The engine sweeps the book until the incoming order is either fully filled or the available opposing liquidity is exhausted.
*   **Partial Fills:** Resting orders decrement their quantity but maintain their iterator position.

## Key Decisions
*   **Synchronous Execution:** The engine evaluates matches synchronously. There is no threading or actor model complexity introduced here, ensuring that behavioral tests serve as a deterministic regression suite.
*   **Iterator Preservation:** Order cancellation leverages `std::list::iterator` mapped by `OrderId`. The engine guarantees that these iterators are never invalidated during partial fills.

## Performance
The current algorithmic complexity for matching is O(K), where K represents the number of passive resting orders that must be traversed and filled by the incoming aggressive order.

## Limitations
*   Market orders currently sweep the book and discard any unfilled remainder. Future iterations may require more nuanced order types, but they are excluded here to maintain strict focus on the algorithmic core.
