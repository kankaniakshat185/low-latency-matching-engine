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
