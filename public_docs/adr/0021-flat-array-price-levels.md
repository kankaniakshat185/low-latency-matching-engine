# ADR-0021: 3.0 — flat array price levels, and an honest regression

Status: Accepted
Date: 2026-08-06

## Context

3.0 replaces `std::map<Price, PriceLevel>` with a flat, tick-indexed array (ADR-0020) plus an occupancy bitmap, carrying forward 2.0's intrusive-list-and-pool order storage unchanged. This is the step actually positioned to test the standing hypothesis — unresolved since Phase 2 — that `std::map`'s tree traversal and cache-miss behavior explains why the Worst-Case-Same-Price workload consistently outperforms Random Prices.

## Decision

`OrderBookV3` (`src/structures/`), same public interface as `OrderBook`/`OrderBookV2`. Finding the best bid/ask walks the occupancy bitmap with `std::countr_zero`/`std::countl_zero`, starting from the array edge (tick 0 for asks, the top tick for bids) on every call — no cached "current best price" pointer carried between calls.

## Alternatives considered

Caching the last-known best-price tick and only rescanning when it empties was considered and explicitly *not* done in this pass — it's a real, identified follow-up (see Consequences), deliberately deferred so this ADR could report what the simplest correct version actually costs before adding complexity to fix it.

A hybrid structure (array for a dense central range, tree for sparse tails) was considered and rejected as premature — solving a problem (sparse-range cost) that hadn't been measured yet at the time of the decision.

## Consequences

**The result is genuinely mixed, and that's the interesting part.** Measured against 2.0 (same three workloads, same run, same three-pass methodology):

| Workload | Throughput vs. 2.0 | 1.0→3.0 cumulative |
|---|---|---|
| Random Prices | **+23.7%** | +86% |
| Heavy Cancels | **+24.2%** | +65% |
| Worst Case | **−10.3%** | +21% |

Worst Case *regressed* going from 2.0 to 3.0. Instruments confirms this isn't noise: Cycles (+8.6%) and Discarded Bottleneck (+4.7% — the category associated with speculative-execution waste, i.e. branch misprediction penalties) both got measurably worse for Worst Case specifically, while every category improved substantially for Random Prices and Heavy Cancels (Instruction Processing alone dropped 34.3% and 30.9% respectively).

**The mechanism, and why it's coherent rather than contradictory:** scanning from the array edge costs an amount proportional to how far the nearest occupied tick is from that edge, with no shortcut when few ticks are occupied. Random Prices and Heavy Cancels keep many price levels active simultaneously — the bitmap scan usually finds an occupied bit within the first word or two, because there's almost always *something* occupied nearby. Worst Case has exactly one occupied tick, sitting near the middle of the configured 9000–11000 range — every single order pays the full ~1000-tick (~16-word) scan from the edge to reach it, a cost `std::map` never had (a tree with one entry finds that entry immediately, regardless of where the price sits in the theoretical range). The flat array's win is real and substantial for realistic multi-level books; its cost is real and specific to the degenerate single-price case.

**On the original hypothesis**, this is real progress, not a wash: the Worst-Case/Random-Prices throughput ratio was 1.843 on 1.0, barely moved to 1.650 on 2.0, and dropped to **1.195** on 3.0 — `std::map` traversal genuinely was a meaningful part of what made Random Prices slower than Worst Case, exactly as hypothesized back in Phase 2. It didn't resolve by Random Prices alone catching up, though — Worst Case gave ground too, which a simple "remove the tree, Random Prices gets faster" story wouldn't have predicted on its own.

**Identified, not yet built:** caching the current best-bid/best-ask tick and only re-scanning when it empties would likely close most or all of the Worst Case regression without costing Random Prices or Heavy Cancels anything — the scan is only expensive when it has to run at all, and for a workload with one stable price, it would almost never need to. Worth a dedicated pass before 3.0 is considered "done" rather than "measured."

**Standing caveat, same as 2.0's**: measured on a machine with real background load (see `optimization_history.md`), so relative deltas here are trustworthy, absolute figures are not, until re-measured on an idle machine.
