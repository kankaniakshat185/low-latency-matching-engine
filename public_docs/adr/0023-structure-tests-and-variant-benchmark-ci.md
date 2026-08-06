# ADR-0023: direct unit tests for the Phase 4 structures, and CI coverage for `variant_benchmark`

Status: Accepted
Date: 2026-08-06

## Context

With Phase 4's comparative study functionally complete (1.0 → 4.0), a direct question — "are the tests exhaustive?" — surfaced two real, closeable gaps rather than a clean bill of health.

**Test gap.** The differential test suite (`tests/differential_test.cpp`) proves all four `OrderBook` variants agree on *valid* input. It cannot exercise any variant's own defensive/boundary behavior, because none of its randomized workloads ever deliberately insert a duplicate id, request an out-of-range price, request an out-of-range `OrderId`, or exhaust a pool — doing any of those on purpose would throw and abort the run before a ledger could be compared at all. A coverage pass confirmed this concretely: every throw/early-return validation branch in `OrderPool`, `OrderBookV3`, and `OrderBookV4` had zero coverage, and `OrderBook`'s own duplicate-id guard — defense-in-depth for any caller other than `MatchingEngine`, which checks `hasOrder` first and so never actually reaches it — had never been exercised by anything at all.

**CI gap.** `variant_benchmark` (the binary that produces every Phase 4 number in this ADR log and `optimization_history.md`) has never been built or run in CI, in any configuration. `engine_benchmark` got this exact fix earlier in the project (see the `build_and_test`/`release_build` jobs); `variant_benchmark` was added afterward and never got the same treatment.

## Decision

**`tests/structures_test.cpp`** (new): 20 direct unit tests instantiating `OrderPool`/`OrderBookV2`/`OrderBookV3`/`OrderBookV4` (and `OrderBook` itself, for the untested duplicate-id guard) without going through `MatchingEngine`. Covers: pool exhaustion (`std::runtime_error`), constructor validation (`std::invalid_argument` for an inverted or zero-tick-size price range), out-of-range price rejection (`std::out_of_range`), `OrderBookV4`'s out-of-range-`OrderId` policy split (`addOrder` throws, `hasOrder`/`cancelOrder` return `false` — a caller/config error vs. a routine query, per the same policy as price-range validation), duplicate-id rejection on every variant directly, and a market sweep that fully drains one side of the book (the scenario that exercises the occupancy-bitmap helpers' "nothing left to scan" return paths — a real behavioral case, a thin market getting fully swept, not just a coverage target).

**`variant_benchmark` CLI + CI wiring.** `compare_variants.cpp`'s `main()` now accepts an optional first argument overriding the total action count (default unchanged: 1.1M, the full local/manual study), with warm-up recomputed as 10% of whatever count is given rather than a fixed absolute number. `printComparison` now returns whether trade counts matched and `main()` exits non-zero on any mismatch. Both `build_and_test` (Debug+sanitizers) and `release_build` CI jobs now build and run `variant_benchmark 20000` as a smoke test — same reasoning as `engine_benchmark`'s existing smoke test: catch crashes/thrown exceptions/trade-count divergence in every configuration this binary is expected to actually run in, without treating its *timing* numbers as CI-gated (shared runners are too noisy for that — see `docs/09_benchmarking.md`'s "Threats to Validity").

## Alternatives considered

**Fuzzing OrderBookV3/V4's constructors and boundary inputs** instead of hand-written targeted tests. Rejected for now: the boundary set here is small and fully enumerable (below-min, above-max, at-both-boundaries, zero tick size, inverted range, id-at-capacity, id-beyond-capacity) — a fuzzer would spend most of its budget rediscovering the same handful of cases these tests already name explicitly. Worth reconsidering if the validation logic grows more branches than this.

**Running the full 1.1M-action `variant_benchmark` in CI** instead of a small override. Rejected: under Debug+sanitizer instrumentation on a shared runner, the full study would be slow and its *timing* would be exactly the kind of noisy number this project has explicitly decided not to gate on since `engine_benchmark`'s CI job was added. The override exists so CI exercises the same code paths — all four variants, all three workload shapes — without pretending the resulting numbers mean anything as CI output.

## Consequences

Coverage moved from 94.8% line (measured fresh on this date — the file set has changed enough since the 2026-08-05 baseline in `docs/14_testing_strategy.md` that the two numbers aren't a like-for-like comparison) to **97.9%** line, 100% function, after adding the 20 tests. `OrderBook.h`, `OrderBookV2.h`, and `OrderPool.h` are now at 100% line coverage each. The residual ~2% is two pre-existing, unrelated items (`CSVParser.h`/`MatchingEngine.h`, a gcovr line-attribution quirk on already-tested `throw` statements) and a couple of `OrderBookV3`/`V4` lines a manual trace through the new full-drain test confirms are actually reached — most likely the same attribution quirk, not a real gap, and not worth further chasing.

All 20 new tests passed on first run — no bugs found in the validation logic itself, which is itself worth stating plainly rather than treating a clean pass as uninteresting: it means the throw/early-return branches were *correct* the whole time, just unverified. `variant_benchmark 20000` runs in ~2 seconds under Release and ~2 seconds under UBSan on this machine — fast enough that CI cost is not a concern for either job it was added to.
