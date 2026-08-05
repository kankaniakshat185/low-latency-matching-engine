# ADR-0003: Single-instrument, single-threaded scope

Status: Accepted
Date: 2026-07-06 (made explicit in the README on 2026-08-05)

## Context

A real exchange handles many instruments at once, many participants, network ingress, persistence, risk checks — all in the loop together. Building any of that before the matching algorithm itself is correct and measurable would mean every benchmark number is confounded by things that have nothing to do with matching: locking, serialization, I/O. That's exactly the kind of unmeasured complexity this project is trying to avoid.

## Decision

One instrument (`Order`/`Trade` carry no symbol field — one `MatchingEngine` is implicitly one order book) and one thread (`processOrder`/`cancelOrder` assume sequential calls, no locking anywhere).

## Alternatives considered

Multi-symbol from day one — a `Symbol`-keyed map of engines, most likely — got pushed to later on purpose. It's a straightforward extension that adds surface area without touching the actual matching algorithm being validated, so there's no reason to build it before the single-instrument core is proven and fast.

A thread-safe `OrderBook` from day one (a mutex, or something lock-free) got rejected for a sharper reason: locking has a real cost, and baking it into every Phase 1–3 benchmark makes it impossible to tell "the algorithm's cost" apart from "the concurrency control's cost." If concurrency does get added later, it's much more likely to look like a single-threaded matching core fed by a lock-free ring buffer from a separate ingress thread — the LMAX Disruptor pattern real exchanges actually use — than a lock living inside `OrderBook`. Building the latter now would probably just get thrown away.

## Consequences

Every published benchmark number measures one thing: the algorithm and its data structures, nothing else. That's what makes the Phase 4 before/after methodology mean anything at all.

The flip side is real too — this codebase currently can't demonstrate anything about concurrent correctness, and a systems interview at an HFT shop is going to ask about it directly. That's a known gap, not a hidden one; the roadmap has a planned direction (a dedicated gateway thread, not a lock in the engine). And "matching engine" as a name overpromises slightly relative to "single-instrument order book," which is why the README says so outright instead of leaving it implied.
