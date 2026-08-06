# Interview Prep: Talking Through This Project

This is a condensed, talking-points version of Phase 4 — the part of this project most worth walking an interviewer through in depth. It's meant to be read once before a conversation, not during one; the full detail behind every claim here lives in `optimization_history.md` and the [ADR log](adr/README.md).

## The 30-second version

A single-threaded, single-instrument limit order book and matching engine, built in C++20. After a full correctness/security audit (Phase 1–3), the rest of the project became a controlled experiment: four independent implementations of the same order book, each changing exactly one thing, each verified byte-identical against the baseline before its performance numbers were trusted, each measured with both wall-clock timing and real Apple Silicon hardware performance counters. The headline result: **~2.4–3.9x throughput** from 1.0 to 4.0 depending on workload, and a two-phases-old open hypothesis about *why* one workload consistently outperformed another finally got resolved with actual evidence instead of a guess.

## Why four implementations instead of one "optimized" version

The more common approach — profile once, optimize, ship — collapses "what made it faster" into one undifferentiated diff. This project isolates one variable per version specifically so each claim ("removing allocation helped," "the tree traversal was the bottleneck," "the array's edge-scan was the actual cost") can be attributed to one specific change, not a bundle of them. It also means a regression (3.0's, below) is legible instead of buried — it's obviously and only attributable to the one thing that version changed.

The connective tissue making this possible without four separate codebases: `MatchingEngine` is `MatchingEngineT<BookT>`, a template, with the matching algorithm's actual invariants (price-time priority, partial fills, the erase-after-decrement ordering) written once as documentation and re-implemented four times independently — deliberately, so the differential test suite is checking four independent authors' interpretations of the same spec against each other, not one implementation against its own assumptions.

## The four versions, in one table

All four measured back-to-back in the same process, same run (the cleanest single comparison available — see `optimization_history.md`'s "Final Comparison" section for the caveat on cross-run numbers):

| Workload | 1.0 | 2.0 | 3.0 | 4.0 | Total gain |
|---|---|---|---|---|---|
| Random Prices | 3.13 M/s | 4.35 M/s | 5.55 M/s | **12.33 M/s** | **+294%** |
| Heavy Cancels | 4.23 M/s | 5.96 M/s | 7.37 M/s | **14.52 M/s** | **+243%** |
| Worst Case (Same Price) | 5.38 M/s | 6.75 M/s | 6.68 M/s | **13.39 M/s** | **+149%** |

- **1.0 — baseline.** `std::map<Price, PriceLevel>` + `std::list<Order>` + `std::unordered_map<OrderId, OrderLocation>`. Correct, easy to verify, cache-unfriendly by construction (three separate heap-backed structures, pointer-chasing throughout).
- **2.0 — remove per-order heap allocation.** Intrusive doubly-linked list over a fixed-capacity pool allocator (`OrderPool`), index-based not pointer-based. Price levels unchanged. **+33–35% everywhere**, every hardware bottleneck category improved — but the gap between Random and Worst Case *didn't* close (ratio 1.719→1.552), so allocation wasn't the reason for that gap.
- **3.0 — remove the tree.** `std::map<Price, PriceLevel>` becomes a flat, tick-indexed array plus an occupancy bitmap (`std::countr_zero`/`std::countl_zero`). **+23.7%/+24.2%** on Random/Heavy Cancels — but a real **−10.3% regression** on Worst Case, confirmed at the hardware-counter level. Mechanism: the bitmap scan always starts from the array's edge with no cached "current best price," so cost scales with distance-to-nearest-occupied-tick — free when many levels are active, expensive when there's exactly one, sitting mid-range.
- **4.0 — cache the best price, index cancellation directly.** Two changes: a per-side cached best-tick (closes 3.0's regression), and the `OrderId → OrderLocation` index becomes a flat vector indexed directly by id instead of a hash map. **Every workload roughly doubled**, and Worst Case's 3.0 regression becomes the single largest gain of the three (+100.6%). Instruments shows the fix is real (Discarded Bottleneck drops sharply) — and a second category (Instruction Processing) rose in *percentage* terms everywhere, which turned out to be the total cycle count shrinking faster than that category's absolute cost, not new cost (confirmed by multiplying the percentage back against each workload's total cycles).

## The standing hypothesis, resolved

Since Phase 2: *why does the degenerate "everyone trades at one price" workload consistently outperform realistic random-price traffic?* Tracked across all four versions via the Worst-Case/Random-Prices throughput ratio:

**1.719 → 1.552 → 1.204 → 1.086**

The answer turned out to be three separable, independently-measured contributors, not one: allocation cost (real, ~33% uniform, but not what caused the gap), `std::map` traversal cost (real, closed most of the gap — the biggest single mover), and lookup-structure shape for cancellation (real, closed most of what remained). No single implementation would have separated these; that separation is the actual point of building four instead of one.

## Methodology, if asked

- **Differential testing.** Every variant replays the same seeded, randomized action sequence (and a deliberately adversarial single-price worst case) through `MatchingEngineT<BookT>` and asserts byte-identical trade ledgers against the 1.0 baseline before any performance number is trusted. A faster implementation that's subtly wrong is worthless — this is what makes every number in the table above trustworthy rather than just fast.
- **Three-pass wall-clock benchmarking**: warm-up, throughput, then a separate latency pass, because measuring latency and throughput in the same loop lets the timer itself contaminate the throughput number (an actual bug this project found and fixed early — see the 1.0→1.0.1 row in `optimization_history.md`).
- **Real hardware counters, not just wall-clock.** Apple Silicon's virtualization layer doesn't expose real PMU counters to a Linux VM guest, so a `perf`-in-a-VM approach would have measured nothing real. Used Instruments' CPU Counters template natively instead, with `os_signpost` markers wrapped around each benchmark run so twelve back-to-back runs in one trace could be attributed to the correct variant/workload after the fact (ADR-0019).
- **An honest regression, reported plainly.** 3.0 made Worst Case ~10% slower and that's stated as clearly as the workloads that improved, with the mechanism explained and confirmed by hardware data rather than hand-waved. Good for an interview specifically because it's evidence of engineering judgment under a result you didn't want, not just a story about wins.

## Likely follow-ups, and short answers

**"Why not just use `perf` / a Linux box?"** Apple Silicon's virtualization doesn't pass real hardware counters through to a VM, and profiling on the same chip the benchmark numbers already come from is more methodologically sound anyway, not just more convenient.

**"How do you know the faster versions are actually correct?"** Differential testing, not code review or intuition — byte-identical trade ledgers, checked automatically, before any speed claim gets made. Worth naming the specific case: `V4MatchesBaselineOnWorstCaseSamePrice` is built to run continuously against 4.0's new best-tick cache, the exact code most likely to have an off-by-one.

**"What would you do differently, or next?"** Re-measure every absolute number here on an idle machine — everything so far was measured under real background system load on a personal machine; the relative deltas are trustworthy, the absolute throughput figures should be re-measured before being quoted alone. (4.0's one open question — the Instruction Processing Bottleneck percentage rise — is resolved, not outstanding: it tracks the total cycle count shrinking faster than that category's absolute cost, confirmed by multiplying the percentage back against each workload's own cycle count rather than reading the percentage alone.)

**"Why single-threaded / single-instrument?"** Explicit, stated scope (see the README's "Known Limitations & Non-Goals") — the goal was depth on one well-defined problem (what actually makes an order book fast) rather than breadth across features unrelated to that question.

## Where to go deeper

- [`optimization_history.md`](optimization_history.md) — the full historical log, one row per version, methodology caveats included.
- [ADR log](adr/README.md) — every decision, dated, with alternatives considered and why they lost. ADR-0017 through ADR-0022 cover Phase 4 specifically.
- [`architecture.md`](architecture.md) / [`matching_engine.md`](matching_engine.md) — the system as it stands today, not just the history of getting here.
