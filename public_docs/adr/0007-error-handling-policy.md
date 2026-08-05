# ADR-0007: One error-handling policy across throw / bool / `[[nodiscard]]`

Status: Accepted
Date: 2026-08-05

## Context

Before this got written down, error handling was inconsistent — not because anything conflicted, but because nobody had decided. `CSVParser` threw on bad input, `cancelOrder` returned a bool, and `addOrder`/`processOrder` had no failure path at all, including for the duplicate-id case in ADR-0005 that clearly needed one. Every new method was picking its own convention on the spot.

## Decision

Written once, as a comment block at the top of `MatchingEngine.h`, and applied everywhere:

- `std::invalid_argument` when the *caller* breaks a precondition — zero quantity, a duplicate id already resting. These are programming errors at the call site, so they throw.
- `[[nodiscard]] bool` for a routine outcome that isn't an error — `cancelOrder` returning `false` for "not found" is a normal thing to check, not exceptional control flow. Every bool-returning method is `[[nodiscard]]` so that check can't be silently skipped.
- `std::runtime_error`, reserved for the external-data boundary (`CSVParser`, ADR-0006) — different from `invalid_argument` because the failure comes from outside the program, not from a caller misusing an API.

## Alternatives considered

`std::expected<T, Error>` everywhere was the more modern option and avoids exceptions on the hot path entirely, but it's a bigger API change than a bug-fix pass justified — and the actual hot-path code (`matchAgainst`, the `matchAgainstSide` template) doesn't throw at all today. Only the two entry checks in `processOrder` do, before any matching runs. Worth revisiting if profiling ever shows exception setup cost mattering, which it can't right now since nothing throws mid-loop.

A hand-rolled `enum class Result` return code was the other option, and it loses to `[[nodiscard]] bool` for the routine cases (same enforcement, less new API surface) while being overkill for the rare exceptional ones, where "can't ignore this" is worth more than a return code anyone could still ignore.

## Consequences

Writing this down immediately caught two places that were discarding a result on purpose: a test macro checking for a thrown exception (which necessarily discards the return value it's checking), and the benchmark's replay loop cancelling an order that might already be filled. Both got an explicit `(void)` cast instead of quietly compiling clean — which is the whole point of `[[nodiscard]]`.

Three different failure mechanisms in one small codebase is more to keep in your head than one would be. Writing the policy down once is the mitigation, not a fix.
