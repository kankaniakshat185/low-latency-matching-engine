# ADR-0005: Reject duplicate `OrderId`s at the book boundary

Status: Accepted
Date: 2026-08-05

## Context

An audit turned up a real bug: `OrderBook::addOrder` had no guard against a reused `OrderId`. Send an order whose id already belongs to a resting order, and `orderLocations_[id]` just gets overwritten. The original order stays physically in its `PriceLevel`'s list — it still trades fine when swept — but it's now unreachable by id. Cancel that id later and you cancel the *new* order instead; the original can never be cancelled again. No exception, no crash, no log line. The cancel index just quietly starts lying.

This isn't a made-up scenario. A client retrying after a dropped ack, a corrupted replay row, an id generator with a bug upstream — any of these hand you a duplicate id, and a real exchange connectivity layer has to deal with exactly this.

## Decision

Two checks, deliberately overlapping:

1. `OrderBook::addOrder` looks up the incoming id first and returns `false` on a collision instead of overwriting.
2. `MatchingEngine::processOrder` calls `OrderBook::hasOrder(id)` before any matching starts at all, and throws `std::invalid_argument` immediately if it's already resting.

The second check is wider than the bug we started with — it also catches an incoming order that would immediately cross and trade against a resting order sharing its own id, which `addOrder` alone would never see (that order never reaches the "rest in the book" step).

## Alternatives considered

Silently dropping the second order was the laziest fix and the wrong one — silent drops are exactly the failure mode we're removing.

Auto-generating a fresh internal id and proceeding anyway sounds convenient until you remember `OrderId` is caller-supplied and meaningful to the caller; substituting a different one just moves the bookkeeping bug somewhere else.

Guarding only in `addOrder`, which is what the fix originally scoped, turned out to be insufficient once the self-match variant showed up during implementation — it wouldn't catch an order that crosses against its own duplicate id before ever resting.

## Consequences

The cancel index can no longer be corrupted by a duplicate id — closed with a hard error instead of a soft best-effort fix. A 20,000-iteration fuzz test with deliberate id reuse (`MatchingEngineFuzz.RandomOpsWithIdReusePreserveBookInvariants`) checks this holds under adversarial load.

This is a breaking change for anyone relying on the old silent-overwrite behavior, even by accident — acceptable since nothing external consumes this library yet.

One thing left genuinely open: reusing an id *after* the original order is fully matched away is fine and intentional (the fuzz test distinguishes "stale but tracked" from "actually live" via `hasOrder()`). Whether that's the right long-term id lifecycle, versus permanently retiring an id once it's been used, hasn't been decided.
