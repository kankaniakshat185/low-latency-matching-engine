# ADR-0009: OrderBook owns matching, accessors go private

Status: Accepted
Date: 2026-08-05

## Context

`OrderBook` exposed mutable `getBids()`/`getAsks()`, and `PriceLevel` exposed a mutable `getOrders()`, for one reason: so `MatchingEngine` could reach in and run the matching loop itself — walking price levels, erasing filled orders, decrementing quantities on containers that conceptually belonged to somebody else. That's Feature Envy in its purest form — the "match this order" operation was being assembled from the outside using accessors that existed only to make that possible.

It also meant nothing stopped some other future caller — a test, a new API layer, a benchmark helper — from grabbing `getOrders()` and mutating the list directly, quietly desyncing `PriceLevel::totalQuantity_` from what's actually in the list, with no way to catch it. Same class of silent corruption as the duplicate-id bug in ADR-0005, just a different door into it.

## Decision

Follows directly from ADR-0008 moving the matching loop into `OrderBook::matchAgainst`:

- `OrderBook`'s mutable `getBids()`/`getAsks()` are gone. Only the const versions are public; `addOrder`/`cancelOrder`/`matchAgainst` mutate `bids_`/`asks_` directly as private members and never needed a public mutable accessor in the first place.
- `PriceLevel::getOrders()`'s mutable overload is `private`, with `friend class OrderBook` for the one caller that legitimately needs it. The const overload — added to support the fuzz test's invariant checker — stays public.

## Alternatives considered

Leaving the accessors public with a comment asking people not to mutate them directly isn't a fix, it's a suggestion. This exact unenforced convention is how the original design happened.

Friending `PriceLevel` to `OrderBook` without also removing `OrderBook`'s own mutable accessors would've closed only half the hole — anything outside could still reach the maps directly.

## Consequences

The violation is now a compile error, not a runtime risk someone discovers under load. No test needed for this one — a test would need the offending code to compile first, and now it doesn't.

Worth noting this fell out of ADR-0008 rather than being a separate change — the duplication and the encapsulation problem turn out to have been the same design issue seen from two angles. The `friend class OrderBook` declaration on `PriceLevel` is a real coupling, but it's one that already existed conceptually (`OrderBook` owns `PriceLevel` per ADR-0001) — this just makes it explicit.
