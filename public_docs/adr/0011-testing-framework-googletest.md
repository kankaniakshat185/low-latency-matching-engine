# ADR-0011: GoogleTest

Status: Accepted
Date: 2026-07-06

## Context

The engine needed a regression suite from the start — correctness first, then a behavioral contract that later optimization work isn't allowed to break. Whatever framework got picked here would shape CI compile time and how easy it'd be to add the kind of adversarial tests that showed up later (ADR-0005, ADR-0006).

## Decision

GoogleTest, fetched via CMake's `FetchContent`, pinned to v1.14.0, one `engine_tests` binary covering both test files.

Small note for the record: an early private planning doc for this project (`docs/14_testing_strategy.md`) talked about building a "lightweight, zero-dependency" test framework specifically to dodge GoogleTest's compile cost. That plan never happened — the actual code has used GoogleTest since the first commit. Not a big deal, but it's the kind of quiet contradiction worth writing down once rather than leaving two documents disagreeing with each other forever.

## Alternatives considered

The hand-rolled zero-dependency framework from that early plan lost in practice — fixtures, `EXPECT_THROW`/`ASSERT_EQ` with real diagnostics, and everyone already knowing the API outweighed the compile-time hit, especially once `FetchContent` caching in CI (ADR-0012) took most of that cost away.

Catch2 was the other reasonable option and honestly could have worked fine — no strong reason against it, GoogleTest was just the default and nothing about this project's needs pointed toward switching.

## Consequences

Fixtures made the duplicate-id and CSV-validation regression tests easy to add, and `EXPECT_THROW` lines up cleanly with the exception-based parts of ADR-0007's error policy. The cost is every CI job that builds tests pays GoogleTest's compile time — mitigated by the shared cache, not eliminated by it.
