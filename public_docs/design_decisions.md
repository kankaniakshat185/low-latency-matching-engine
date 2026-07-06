# Key Design Decisions

## Overview
This document logs the primary architectural and algorithmic decisions that govern the matching engine's implementation, detailing the rationale behind each choice.

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
