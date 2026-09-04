# ADR-0001: Composition over inheritance for the core hierarchy

Status: Accepted
Date: 2026-07-06

## Context

Before any matching logic got written, `MatchingEngine`, `OrderBook`, `PriceLevel`, and `Order` needed a shape. That relationship gets exercised on every order, and with `processOrder` budgeted in the hundreds of nanoseconds, how these pieces talk to each other isn't a style preference — it decides whether the CPU spends its cycles matching orders or chasing vtables and cache misses.

## Decision

Plain composition, concrete types, no virtual dispatch anywhere. `MatchingEngine` owns an `OrderBook` by value, `OrderBook` owns two `std::map<Price, PriceLevel>`, `PriceLevel` owns a `std::list<Order>`. No interfaces, no base classes, nothing injected.

## Alternatives considered

A polymorphic order hierarchy — `LimitOrder`/`MarketOrder` subclasses matched through a virtual `match()` — was the obvious first idea, and it got rejected quickly, for two concrete mechanical reasons rather than a style preference:

A non-virtual call's target address is fixed at compile time, so the CPU never has to guess where it lands. A virtual call can't do that — `LimitOrder` and `MarketOrder` overriding a shared `match()` means the real target is read out of a per-object vtable at runtime, so the *same call site* jumps to a different address depending on which concrete type showed up. The branch predictor has to guess that destination ahead of time to keep the pipeline full, and it guesses worse on a target that varies call-to-call than on one that's always identical — every wrong guess means discarding speculative work and re-fetching.

Separately: `LimitOrder` and `MarketOrder` would be different sizes, and `std::list<Order>`'s nodes are all one fixed size — polymorphic orders can't sit in the list by value the way a flat struct can. They'd need to live behind pointers instead, allocated wherever, turning every list walk into a pointer-chase to a possibly-cold cache line — the same cache-locality cost this ADR's own Context section is trying to avoid, just introduced from a different direction.

`Order.h` even has a comment about the first fix: the `price` field is "irrelevant for Market orders, but included in struct to avoid polymorphism."

An abstract `OrderBook` interface (so an implementation could be swapped via a base pointer) went the same way. Phase 4's whole point is swapping `OrderBook`'s internals — adding an indirection layer just to enable that swap defeats the purpose, since every call pays for it regardless of which concrete implementation is active.

## Consequences

Flat memory layout, no vtables, and the compiler can see through most of `OrderBook`/`PriceLevel`'s methods without much trouble — that'll matter once profiling starts. The cost: no way to run two `OrderBook` implementations side by side at runtime without a compile-time switch. Fine for now, since Phase 4 is sequential before/after benchmarking, not a live comparison. If a lock-free variant is ever needed for concurrent ingress, that's a template parameter on `MatchingEngine` or accepting a vtable at that one boundary — a call to make explicitly when it comes up, not something foreclosed here.
