# ADR-0014: clang-format matched to the existing style

Status: Accepted
Date: 2026-08-05

## Context

The code was formatted consistently by hand — 4-space indent, attached braces, unindented access specifiers — but nothing enforced it. Consistency depended entirely on whoever was reviewing noticing drift.

## Decision

Added `.clang-format` on top of Google's style, overridden to match what was already there instead of imposing something new: 4-space indent, access modifiers pulled back out, left-aligned pointers/references, attached braces, short functions allowed on one line (needed so accessors like `bool isEmpty() const { return orders_.empty(); }` don't get forcibly broken up), and one space before trailing comments instead of Google's default two (which would've touched every `// namespace engine` line for no reason).

Before trusting this in CI: ran `--dry-run` against every file, read the actual diffs, confirmed the changes were mechanical — trailing whitespace, brace placement, a couple of long-line wraps, no comment reflow, no logic touched — then applied it, rebuilt, and reran all 26 tests to make sure nothing changed behaviorally.

## Alternatives considered

Using a stock preset with zero customization would have reformatted every short accessor onto multiple lines and rewritten every trailing comment's spacing — a big diff that's really just the tool fighting the codebase's actual conventions.

Writing the config and turning on the CI check without running it against the real code first was the other shortcut, and it's exactly how a format check ends up failing its first real run and losing everyone's trust in it immediately.

## Consequences

The format-check CI job passes today instead of being something that reds the first real PR. New contributors — human or otherwise — get consistency from CI instead of from review nitpicking. One small cosmetic casualty: a line wrap in the new `matchAgainstSide` template signature reads a bit awkwardly post-format. Not worth a config override for one occurrence.
