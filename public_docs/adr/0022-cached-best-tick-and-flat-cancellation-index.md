# ADR-0022: 4.0 — cached best-price tick and a flat OrderId-indexed cancellation index

Status: Accepted
Date: 2026-08-06

## Context

3.0 left two things on the table, both named explicitly in ADR-0021 rather than built there: a cached best-price tick to close the Worst Case regression, and a reconsideration of the `OrderId → OrderLocation` cancellation index, which has been a `std::unordered_map` — a hash plus a bucket walk on every insert/cancel/lookup — since 1.0, unchanged across every variant so far.

Both are addressed here, in one version, because they touch the same file and neither is large enough on its own to justify a fifth implementation just to keep the "one variable per step" rule literal. What stays constant across both changes is the actual variable being isolated: replacing a general-purpose data structure (hash map, bitmap-scan-from-edge) with one that exploits a bound this project already leans on — a fixed, dense range — instead of handling the fully general case.

## Decision

`OrderBookV4` (`src/structures/`), same public interface as `OrderBook`/`OrderBookV2`/`OrderBookV3`, built directly on top of 3.0's flat tick-indexed array rather than starting over.

**Cached best-price tick.** Each `FlatSide` now carries a `bestTick` member, kept correct incrementally instead of recomputed by scanning: `noteInserted(tick)` runs the moment a tick transitions empty→occupied (a single comparison — bids want the max occupied tick, asks the min) and `noteMaybeBestCleared(tick)` runs the moment a tick transitions occupied→empty, but only does any work — a bitmap rescan via the existing `nextOccupiedFrom`/`prevOccupiedFrom` — when the emptied tick *was* the cached best. `matchAgainstSide` reads `bestTick` directly instead of starting from the array edge.

**Flat OrderId-indexed cancellation index.** `orderLocations_` changes from `std::unordered_map<OrderId, OrderLocationV4>` to `std::vector<OrderLocationV4>`, sized by a new constructor parameter `orderIdCapacity` and indexed directly by `order.id`. `kInvalidNodeIndex` doubles as the "this id slot is unused" sentinel, so no separate occupancy tracking is needed for this vector. An id at or beyond `orderIdCapacity` throws `std::out_of_range` from `addOrder` — the same class of error as an out-of-range price (`tickIndexOrThrow`), a caller/config violation rather than a routine outcome; `hasOrder`/`cancelOrder` treat an out-of-range id as simply "not found," since those are routine queries.

## Alternatives considered

**Two separate versions (4.0 and 5.0), one per change.** Rejected: neither change is complex enough on its own to carry a full ADR/benchmark/Instruments cycle, and — unlike 2.0 vs. 3.0, which isolated genuinely different subsystems (order storage vs. price-level lookup) — both of 4.0's changes are instances of the same underlying idea (bounded-domain direct indexing beats a general-purpose structure when the bound is real), so keeping them together doesn't blur what's being isolated.

**Robin Hood / open-addressing hash map instead of `unordered_map`**, keeping OrderId unbounded. Considered and rejected: it would have been a genuine improvement over `std::unordered_map`'s chaining, but it doesn't test the same idea 3.0 and the rest of Phase 4 have been testing — that a real, bounded domain (price range, and here, order-id range) can be exploited directly rather than engineered around generically. A dense sequential id space assigned by an exchange gateway is exactly that kind of bound, the same class of assumption as ADR-0020's price range.

**Storing `bestTick` on `OrderBookV4` itself (two scalars) instead of inside `FlatSide`.** Rejected for a small reason: the update logic (`isBid ? tick > bestTick : tick < bestTick`) is direction-dependent, and keeping it as a method on the struct that already knows its own direction (`FlatSide::isBid`) avoids duplicating that branch at every call site.

## Consequences

Differential tests (`V4MatchesBaseline`, `V4MatchesBaselineOnWorstCaseSamePrice`) passed against the 1.0 baseline on first run, including the worst-case-same-price workload — the one adversarial scenario that exercises the best-tick cache continuously, since the best price never moves once the first order lands. A clean UBSan build (Debug, `-fsanitize=undefined`) also passed; ASan itself is still not usable for local verification on this machine (see `optimization_history.md`/journal — an environment quirk unrelated to this codebase).

**Wall-clock, measured against 3.0 (same process, same run, three-pass methodology):**

| Workload | Throughput vs. 3.0 | 1.0→4.0 cumulative |
|---|---|---|
| Random Prices | **+122.3%** | +294% |
| Heavy Cancels | **+97.1%** | +243% |
| Worst Case | **+100.6%** | +149% |

Every workload roughly doubled or better on top of 3.0 — including Worst Case, which is the headline result here: 3.0's −10.3% regression on that workload is not just recovered, it's overcorrected into a +100.6% gain, the biggest reversal of the three (Random's +122.3% is the largest gain outright, but it didn't start from a regression). Trade counts matched all prior versions on every workload.

The standing hypothesis metric keeps moving in the same direction it has all through Phase 4 — the Worst-Case/Random-Prices throughput ratio: **1.719 → 1.552 → 1.204 → 1.086** across 1.0/2.0/3.0/4.0 (this run). Four versions in, the two workloads that started nearly a factor of two apart are now within 9% of each other.

**Instruments confirms the mechanism, and adds an honest nuance.** Correlating the same `os_signpost` regions (ADR-0019) against this run's CPU Counters trace, comparing 3.0 → 4.0:

*Discarded Bottleneck (branch misprediction / pipeline-restart cost) dropped everywhere*, most on the workload that motivated the whole change:
- Random: 23.9% → 14.6% (**−9.3pp**)
- Heavy Cancels: 23.3% → 14.7% (**−8.6pp**)
- Worst Case: 11.1% → 5.1% (**−6.0pp**, more than half)

This is exactly the mechanism ADR-0021 named: the bitmap-scan-from-edge was branch-heavy loop code with no shortcut, and it's specifically what the cached `bestTick` removes from the hot path. Worst Case's Discarded Bottleneck was never the *highest* of the three in absolute terms, but the relative drop there is the largest, which is consistent with the scan-from-edge being disproportionately expensive exactly where the array's one occupied tick sits far from the edge.

*Instruction Processing Bottleneck rose everywhere*, most on Heavy Cancels:
- Random: 17.7% → 24.3% (**+6.6pp**)
- Heavy Cancels: 17.7% → 31.4% (**+13.8pp**)
- Worst Case: 10.0% → 15.4% (**+5.3pp**)

This looked worse than it is. Multiplying each workload's Processing percentage back out against its own total cycle count (also captured in the same trace) gives the *absolute* processing-bound cycle count, not just its share:

| Workload | 3.0 processing cycles | 4.0 processing cycles | Absolute change |
|---|---|---|---|
| Random | 316.5M | 202.7M | **−35.9%** |
| Heavy Cancels | 235.0M | 240.5M | +2.4% |
| Worst Case | 130.7M | 124.1M | −5.0% |

In every workload, the absolute processing-bound cost held flat or *dropped* — Random dropped by over a third. The percentage rose because the denominator (total cycles) collapsed even faster: Heavy Cancels' total cycle count fell 42.5% while its processing-bound cycles stayed essentially flat, so the same near-constant cost became a much larger share of a much smaller pie. This is an Amdahl's-law effect, not new cost — 4.0 didn't make memory-bound work more expensive, it made the branch-heavy work around it so much cheaper that what was already memory-bound stood out more in the percentage view.

One candidate mechanism was tested directly and ruled out before landing on the above: an oversized flat cancellation-index array (`variant_benchmark` sizes `orderIdCapacity` to 3× a single workload's action count, since three workloads share one id counter — see `compare_variants.cpp`) blowing the working set out of cache, versus a hash map whose footprint tracks live orders rather than the full id range ever issued. A standalone benchmark ran the identical single workload through `OrderBookV4` twice — once with a tight, exactly-sized capacity, once with the oversized one `variant_benchmark` actually uses — five passes each. The delta was small and inconsistent (−3.4% to +7.5%, no consistent direction), meaning array size isn't the driver: ids are assigned sequentially, so access at any given moment clusters near the current high-water mark regardless of how large the array's declared capacity is. Worth recording as a ruled-out path, not just a footnote — it's the more obvious-sounding hypothesis, and it isn't the right one.

*Useful fraction* rose for Random (+3.9pp) and Worst Case (+0.9pp) but dipped slightly for Heavy Cancels (−2.8pp) — worth stating plainly rather than smoothing over: a lower Useful *fraction* is not the same claim as fewer useful cycles in absolute terms. Heavy Cancels' total cycle count dropped 42.5% (3.0→4.0) alongside that percentage-point dip, so the absolute amount of non-useful work fell substantially even though its *share* of a much smaller total ticked up slightly.

**Standing caveat, same as 2.0's and 3.0's**: measured under real background system load, not a quiet dedicated run — relative deltas (same process, same run) are trustworthy, absolute throughput figures are not, until re-measured on an idle machine.
