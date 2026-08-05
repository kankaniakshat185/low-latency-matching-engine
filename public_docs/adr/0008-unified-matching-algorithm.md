# ADR-0008: Collapse the four match loops into `OrderBook::matchAgainst`

Status: Accepted
Date: 2026-08-05

## Context

`MatchingEngine.cpp` had four functions — `matchLimitBuy`, `matchLimitSell`, `matchMarketBuy`, `matchMarketSell` — each running basically the same ~20-line traversal/fill/removal loop. The only real differences were which side of the book to walk, whether a limit-price check applied, and whether an unfilled remainder should rest afterward. Any change to the matching loop — a bug fix, an optimization, a new invariant check — had to be made in all four places identically, which is its own bug generator.

## Decision

One private template inside `OrderBook`: `matchAgainstSide<PriceLevelMap>(order, levels, isBuy, limitPrice, trades)`. It's templated on the map because bids and asks are different concrete types (opposite comparators), and it takes an `isBuy` flag that flips the crossing comparison (`levelPrice > limitPrice` for a buy, `<` for a sell).

A public `OrderBook::matchAgainst(order, std::optional<Price> limitPrice, trades)` picks the right side from `order.side` and calls the template once. No limit price at all means sweep unconditionally (a market order); a limit price present means stop once the book crosses it. `MatchingEngine::processOrder` shrank to: validate, build the optional limit, call `matchAgainst`, rest the remainder if it's a limit order with quantity left over. `MatchingEngine.cpp` went from 230 lines to 47.

## Alternatives considered

Keeping the four functions but pulling the shared inner loop into a helper taking a map reference and a predicate was basically the same idea without actually moving the logic into `OrderBook` — which would've left the encapsulation problem in ADR-0009 unsolved, since `MatchingEngine` would still need mutable access to call the helper.

A single non-template function taking a `std::variant` of the two map types was the other option, and it loses on cost: that's real runtime dispatch (a `std::visit` per call) to avoid a compile-time template parameter that costs nothing at runtime. Two template instantiations in the binary is a fine price for zero-overhead sharing.

## Consequences

One code path to test, one to optimize in Phase 4, one place to add a future invariant check instead of four. It also fixed ADR-0009's encapsulation problem as a side effect — since the loop lives inside `OrderBook` now, it touches `bids_`/`asks_`/`orderLocations_` directly as private members instead of needing public mutable accessors.

Checked carefully, not just assumed: all 26 tests passed unchanged after the refactor, and a full 1.1M-action synthetic benchmark run plus a `data/sample.csv` replay were both re-run to confirm sane output. A refactor this central to the hot path is exactly where a subtle behavior change likes to hide.
