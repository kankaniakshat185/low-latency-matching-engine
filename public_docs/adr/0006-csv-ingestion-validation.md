# ADR-0006: Strict validation at the CSV ingestion trust boundary

Status: Accepted
Date: 2026-08-05 (parser itself dates to 2026-07-06)

## Context

`CSVParser` is the one place external data gets into the system, and the original version was permissive in ways that actually mattered. `std::stoull("-5")` returns `18446744073709551611` instead of throwing — that's a real two's-complement wraparound, not a hypothetical — so a negative price or quantity in a bad row silently produced a garbage order instead of an error. An unrecognized `Action` or `Side` character silently defaulted to Insert/Buy. A short row missing its last field just left that field empty and kept going. Nothing threw. It just quietly made a different, wrong order that looked perfectly valid.

## Decision

Rewrote `parseLine` around a shared `parseUnsigned<T>()` helper plus explicit whitelists:

- Reject a leading `-` before `std::stoull` even runs.
- Reject anything where `std::stoull` doesn't consume the whole token (catches trailing junk like `"100abc"`).
- Range-check against `std::numeric_limits<T>::max()` before the narrowing cast, so a `Quantity` that overflows `uint32_t` gets caught instead of silently truncated.
- Require exactly 5 comma-separated fields, and name the actual count in the error if not.
- Require `Action` to be exactly `I` or `C`, `Side` to be exactly `B` or `S` — anything else throws.

Every failure raises `std::runtime_error` naming the line and the field, per ADR-0007's policy for this boundary.

## Alternatives considered

Leaving it permissive and just documenting the caveat was on the table for about five minutes. This is the one component whose whole job is being the trust boundary for outside data — a documented gap here means a benchmark run against slightly corrupted replay data produces numbers nobody can actually trust, which cuts against the entire point of the project.

Switching to `std::from_chars` instead of `stoull` plus manual checks was considered too. It doesn't inherently solve the sign problem any more cleanly for an unsigned target, and the rest of this file's error handling is already exception-based — changing the parsing primitive without changing the error model would've been a half-refactor. Worth reconsidering if this parser ever ends up on an actual hot path, which it currently isn't (parsing happens before benchmark timing starts).

## Consequences

The `stoull("-5")` bug was reproduced independently with a standalone snippet before it got fixed, so this isn't a theoretical concern being addressed defensively — it's a demonstrated one. Eight new tests in a `CSVParserValidation` suite lock the fix in, including a positive control (`AcceptsWellFormedCancelRowWithZeroPlaceholders`) confirming the stricter parser doesn't reject legitimate `0,0` placeholders on cancel rows.

The cost is that a real historical dataset with any formatting quirks — stray whitespace, a trailing comma, an odd encoding — will now throw instead of best-effort parsing through it. Right tradeoff for this project, but worth knowing before pointing this at a large, unvetted third-party dataset for the first time.
