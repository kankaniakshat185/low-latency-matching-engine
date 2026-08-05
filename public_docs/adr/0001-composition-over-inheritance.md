# ADR-0001: Composition over inheritance for the core hierarchy

Status: Accepted
Date: 2026-07-06

## Context

Before any matching logic got written, `MatchingEngine`, `OrderBook`, `PriceLevel`, and `Order` needed a shape. That relationship gets exercised on every order, and with `processOrder` budgeted in the hundreds of nanoseconds, how these pieces talk to each other isn't a style preference — it decides whether the CPU spends its cycles matching orders or chasing vtables and cache misses.

## Decision

Plain composition, concrete types, no virtual dispatch anywhere. `MatchingEngine` owns an `OrderBook` by value, `OrderBook` owns two `std::map<Price, PriceLevel>`, `PriceLevel` owns a `std::list<Order>`. No interfaces, no base classes, nothing injected.

## Alternatives considered

A polymorphic order hierarchy — `LimitOrder`/`MarketOrder` subclasses matched through a virtual `match()` — was the obvious first idea, and it got rejected quickly. Every virtual call is a jump the branch predictor can't help with as well as a direct call, and polymorphic orders would need to live behind pointers instead of sitting by value in `std::list<Order>`. `Order.h` even has a comment about this: the `price` field is "irrelevant for Market orders, but included in struct to avoid polymorphism."

An abstract `OrderBook` interface (so an implementation could be swapped via a base pointer) went the same way. Phase 4's whole point is swapping `OrderBook`'s internals — adding an indirection layer just to enable that swap defeats the purpose, since every call pays for it regardless of which concrete implementation is active.

## Consequences

Flat memory layout, no vtables, and the compiler can see through most of `OrderBook`/`PriceLevel`'s methods without much trouble — that'll matter once profiling starts. The cost: no way to run two `OrderBook` implementations side by side at runtime without a compile-time switch. Fine for now, since Phase 4 is sequential before/after benchmarking, not a live comparison. If a lock-free variant is ever needed for concurrent ingress, that's a template parameter on `MatchingEngine` or accepting a vtable at that one boundary — a call to make explicitly when it comes up, not something foreclosed here.
