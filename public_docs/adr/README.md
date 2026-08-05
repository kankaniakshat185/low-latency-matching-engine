# Architecture Decision Log

Every decision that shaped this codebase, the build, or the CI — big or small — gets an entry here. `design_decisions.md` one level up is the short version for a casual read; this is the full record, one file per decision, numbered in the order they happened.

## Why bother with this many files

Because six months from now nobody remembers *why* `std::list` won over a vector, and `git blame` only tells you what changed, not what else was on the table. Once Phase 4 starts ripping out the baseline data structures, whoever does that work needs to know which Phase 1–3 constraints are actually load-bearing and which were just convenient defaults.

## Rules

- Don't edit an old entry to reflect a later change of mind. If we did something and later did it differently, write a new entry that says so, and add a one-line "superseded by" note at the top of the old one. What we used to think is worth keeping, not erasing.
- Every entry has a date and a status (`Accepted`, `Superseded`, occasionally `Proposed`).
- New entries append at the next number. Nobody renumbers anything.

## Index

| # | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-composition-over-inheritance.md) | Composition over inheritance | Accepted | 2026-07-06 |
| [0002](0002-baseline-data-structures.md) | `std::map`/`std::list`/`std::unordered_map` baseline | Accepted | 2026-07-06 |
| [0003](0003-single-instrument-single-threaded-scope.md) | Single instrument, single thread | Accepted | 2026-07-06 |
| [0004](0004-order-trade-value-types.md) | Order/Trade as plain structs | Accepted | 2026-07-06 |
| [0005](0005-duplicate-orderid-rejection.md) | Reject duplicate OrderIds | Accepted | 2026-08-05 |
| [0006](0006-csv-ingestion-validation.md) | Strict CSV validation | Accepted | 2026-08-05 |
| [0007](0007-error-handling-policy.md) | One error-handling policy | Accepted | 2026-08-05 |
| [0008](0008-unified-matching-algorithm.md) | Collapse the four match loops into one | Accepted | 2026-08-05 |
| [0009](0009-encapsulation-tightening.md) | OrderBook owns matching, accessors go private | Accepted | 2026-08-05 |
| [0010](0010-benchmarking-methodology.md) | Three-pass benchmark methodology | Accepted | 2026-07-06, revised 2026-07-20 |
| [0011](0011-testing-framework-googletest.md) | GoogleTest | Accepted | 2026-07-06 |
| [0012](0012-ci-pipeline-design.md) | Five-job CI pipeline | Accepted | 2026-07-06, expanded 2026-08-05 |
| [0013](0013-compiler-warnings-scoping.md) | Scope -Werror to our own targets | Accepted | 2026-08-05 |
| [0014](0014-code-formatting-standard.md) | clang-format matched to existing style | Accepted | 2026-08-05 |
| [0015](0015-license-choice.md) | MIT license | Accepted | 2026-08-05 |
| [0016](0016-repo-hygiene-binary-artifacts.md) | Untrack the committed binary | Accepted | 2026-08-05 |
