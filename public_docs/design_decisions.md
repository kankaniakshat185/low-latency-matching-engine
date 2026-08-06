# Key Design Decisions

## Overview
This document logs the primary architectural and algorithmic decisions that govern the matching engine's implementation, detailing the rationale behind each choice.

*For the full, granular record — every decision, major or minor, as its own numbered and dated entry — see the [Architecture Decision Log](adr/README.md). This document is the short version.*

## 1. Establishing a Slow Baseline (Phase 1)
**Decision**: The initial version of the engine intentionally utilizes standard library containers (`std::map`, `std::list`) known to possess poor cache locality.
**Rationale**: In systems engineering, optimization without a baseline cannot be quantified. Correctness must be established first. By building a mathematically correct, highly testable baseline, we possess a strict behavioral regression suite against which all future hardware-specific optimizations (custom allocators, contiguous arrays) can be objectively measured.

## 2. Iterators for O(1) Cancellation
**Decision**: The `OrderBook` utilizes a `std::unordered_map` mapping `OrderId` to `std::list::iterator`.
**Rationale**: `std::list` provides strict iterator stability upon insertion and erasure. This allows us to cancel resting orders from the middle of a price level queue in O(1) time without traversing the book, while ensuring that surrounding orders are unaffected.

## 3. Composition Over Inheritance
**Decision**: The system utilizes strict composition (`MatchingEngine` owns `OrderBook`, which owns `PriceLevel`) rather than deep inheritance trees or interface abstractions.
**Rationale**: Deep class hierarchies introduce vtable lookup overhead and indirect memory jumps (cache misses). Composition provides a flat, predictable memory model that remains modular enough to swap out internal data structures easily.

## 4. Infrastructure & Testing
**Decision**: The repository strictly limits infrastructure to what is essential for verifying correctness and performance. We use **GoogleTest** as our permanent correctness contract for unit and integration testing. Debug builds enable **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** to strictly guarantee memory safety, while Release builds remain optimized (`-O3`) and free of overhead.
**Rationale**: Systems programming requires rigorous memory validation. However, deploying complex infrastructure (Docker, Kubernetes, cloud CI) adds no engineering value to a localized, focused performance project. A single GitHub Actions workflow ensures the standard is upheld on every commit.

## 5. Validation at Every External Boundary
**Decision**: `OrderBook` rejects a duplicate `OrderId` instead of silently overwriting its cancel index. `CSVParser` rejects negative numbers, out-of-range values, malformed rows, and bad Action/Side characters instead of coercing them into something structurally valid but wrong.
**Rationale**: An audit found both boundaries had permissive failure modes — a reused id silently orphaned the earlier order; `std::stoull` silently wraps a negative number into a huge unsigned value instead of throwing (checked this directly, it's real). A project built on "no conclusion without measured evidence" can't afford a boundary that quietly corrupts its own input. Each fix shipped with a test that fails without it, including a 20,000-iteration fuzz test that reuses ids on purpose and checks the book's invariants hold anyway.

## 6. One Matching Algorithm, Owned by `OrderBook`
**Decision**: The four near-identical match functions that used to live in `MatchingEngine` are now one method on `OrderBook`, parameterized on side and an optional limit price. The mutable accessors those four functions needed are gone.
**Rationale**: `MatchingEngine` was doing `OrderBook`'s job through accessors that existed only to make that possible, and the duplication across the four functions was the direct symptom. Moving the operation to where the data actually lives fixed both problems at once — one path to test, and an encapsulation boundary the compiler now enforces instead of a comment asking nicely.

## 7. One Error-Handling Policy
**Decision**: `std::invalid_argument` when a caller breaks a precondition (zero quantity, a duplicate id). `[[nodiscard]] bool` for a routine, expected outcome (`cancelOrder` returning false). `std::runtime_error` for the external-data boundary (`CSVParser`).
**Rationale**: Error handling used to be inconsistent by omission rather than by any real conflict — some paths had no failure signal at all. Writing this down once and applying it everywhere caught two places quietly discarding a result on purpose (a test checking for a thrown exception, a benchmark loop cancelling an order that might already be filled), both now made explicit instead of silently compiling clean.

## 8. CI as Five Jobs, Not One
**Decision**: Debug+sanitizers (now also building the benchmark binary), a Release build, a non-blocking static-analysis pass, a formatting check, and a coverage report.
**Rationale**: The original single job only ever checked `engine_tests` — the benchmark binary had zero coverage, and the `-O3` build behind every published number had never even been confirmed to compile. Static analysis starts non-blocking on purpose; failing every PR against findings nobody's triaged yet just teaches people to ignore it.

## 9. Fixed-Capacity Pool for 2.0's Order Storage
**Decision**: `OrderPool` (backing 2.0's intrusive order list) has a fixed capacity, sized upfront, with a hard error on exhaustion rather than growing or falling back to `malloc`.
**Rationale**: A pool that can grow means an allocation event at an unpredictable moment — and the moment it actually needs to grow is precisely when the book is under the heaviest load, the worst possible time for a latency spike. A pool that can silently grow under load defeats the entire point of pooling.

## 10. One Matching Engine, Templated Over the Book
**Decision**: `MatchingEngine` became `MatchingEngineT<BookT>` (`using MatchingEngine = MatchingEngineT<OrderBook>;`), so a second book implementation (`OrderBookV2`, an intrusive linked list + pool allocator instead of `std::list`) runs through the identical engine logic with zero code changes elsewhere.
**Rationale**: This is the composition-over-inheritance decision from entry 3 actually being cashed in rather than just asserted — "swap OrderBook's internals without touching MatchingEngine" was a stated goal since Phase 1 and untested until there was a second implementation to try it with. Every new variant is checked against the 1.0 baseline with differential testing (identical trade ledgers, same input) before its performance numbers are trusted — a faster implementation that's subtly wrong is worthless, so this stopped being optional the moment a second implementation existed.

## 11. Real Hardware Counters, Not Just Wall-Clock
**Decision**: Used Instruments' CPU Counters (via `xctrace`, driven from the command line) rather than a Linux VM's `perf`, and marked each benchmark run with `os_signpost` so hardware-counter data could be attributed to the correct variant/workload in one combined trace.
**Rationale**: Apple Silicon's virtualization layer doesn't expose real hardware performance counters to a VM guest — `perf` in a VM here would produce numbers that don't reflect anything real. Profiling natively on the same chip the existing benchmark numbers already come from is also more methodologically correct, not just more convenient. Getting this working needed clearing real environment blockers (no Xcode installed, then Developer Mode disabled) that failed silently rather than with useful errors — worth knowing about before the next variant's profiling run.

## 12. Bounded Price Range for 3.0's Flat Array
**Decision**: `OrderBookV3` takes an explicit `[minPrice, maxPrice, tickSize]` at construction and throws on any price outside it, rather than growing the array or silently clamping.
**Rationale**: Same reasoning as entry 9's pool capacity — a resize is an allocation event at an unpredictable time, defeating the point of a flat array. A bounded range isn't a toy simplification either: real exchanges enforce price bands/circuit breakers for unrelated risk-management reasons, so a flat array's constraint happens to line up with something real systems already have.

## 13. Ship the Regression, Don't Hide It
**Decision**: 3.0 (flat array price levels) improved Random Prices and Heavy Cancels substantially but measurably regressed Worst-Case-Same-Price by ~10% — reported plainly, with the mechanism explained and backed by hardware-counter data, rather than only reporting the two workloads that improved.
**Rationale**: The bitmap-based lookup scans from the array's edge on every call with no cached "current best price," so its cost scales with how far the sole occupied price is from that edge — cheap when many price levels are active, expensive when there's exactly one. That's a real, identified, fixable gap in this specific pass, not evidence the approach is wrong — and reporting it plainly is what makes the *other* result trustworthy: the Worst-Case/Random-Prices ratio that had been stuck since Phase 2 finally moved (1.84 → 1.20), real progress on the standing hypothesis, achieved by both sides moving rather than the tidier story a guess would have predicted.

## 14. Bundle Two Changes Into 4.0, Not Two More Versions
**Decision**: 4.0 (`OrderBookV4`) ships the cached best-price tick and the flat OrderId-indexed cancellation index together, rather than splitting them into separate 4.0/5.0 versions the way 2.0 and 3.0 isolated order storage from price-level lookup.
**Rationale**: Both changes are instances of the same underlying idea — a real, bounded domain (order-id sequence range, same as 3.0's price range) can be indexed directly instead of handled through a general-purpose structure — not two different subsystems being isolated the way 2.0 vs. 3.0 were. Splitting them wouldn't have isolated a second variable so much as split one idea across two ADRs.

## 15. Report the Hardware Trade-Off, Not Just the Win
**Decision**: 4.0's ADR (0022) reports that Discarded Bottleneck dropped sharply (confirming the fix) *and* that Instruction Processing Bottleneck rose across every workload (an unexplained cost), rather than leading only with the ~2x wall-clock throughput number.
**Rationale**: Same principle as entry 13, applied to a win instead of a regression — a good headline number is exactly when it's easiest to stop looking. The wall-clock result is real and large, but "every hardware category improved" would have been a false claim; "the mechanism we targeted improved a lot, and cost moved somewhere else we don't yet have a confirmed explanation for" is the true one, and it's the one written down.

## 16. Test the Structures Directly, Not Just Through the Engine
**Decision**: `tests/structures_test.cpp` instantiates `OrderPool`/`OrderBookV2`/`OrderBookV3`/`OrderBookV4` directly rather than only exercising them through `MatchingEngineT<BookT>` and the differential test suite.
**Rationale**: A coverage pass showed the reason this mattered concretely: differential testing only ever supplies *valid* input (a duplicate id, an out-of-range price, or an out-of-range OrderId would just throw and abort the run before a ledger could be compared), so every defensive/validation branch across all four variants had zero coverage — including `OrderBook`'s own duplicate-id guard, which `MatchingEngine` never actually reaches in practice since it checks `hasOrder` first. Direct tests, bypassing the engine, are what closed that — coverage moved from 94.8% to **99.2%** line (97.9% after the first pass, 99.2% after a second pass closed the remaining `CSVParser` and full-side-drain gaps — see ADR-0023's update), and more importantly, over 20 previously-unverified defensive code paths are now proven correct rather than merely assumed so. The one real surprise along the way: one of the two remaining uncovered lines in `OrderBookV4` turned out to be genuinely unreachable dead code, not just an untested one — its only call site already guards against ever hitting it.

## 17. Give `variant_benchmark` Real CI Coverage, Without CI-Gating Its Numbers
**Decision**: Both CI jobs now build and run `variant_benchmark 20000` (a CLI override added for this purpose) as a smoke test — catching crashes, thrown exceptions, and trade-count mismatches — without treating its timing output as a pass/fail signal.
**Rationale**: `variant_benchmark` produces every number in this project's Phase 4 story and had never been built in CI at all, in any configuration — a real, and by this point overdue, gap given `engine_benchmark` got the equivalent fix earlier in the project. Running the numbers themselves through CI would repeat the mistake this project already decided against for `engine_benchmark`: shared runners are too noisy for nanosecond-scale timing to be a trustworthy gate. Smoke-testing for crashes and correctness, while leaving performance measurement to local/dedicated-machine runs, gets the real benefit (catching a build or logic break before it ships) without the false confidence or false failures noisy timing would introduce.
