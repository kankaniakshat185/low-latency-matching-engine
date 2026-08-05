# ADR-0016: Untrack the committed binary

Status: Accepted
Date: 2026-08-05

## Context

A compiled Mach-O binary (`engine_benchmark`) was sitting in git at the repo root — confirmed via `git ls-files`, not just present on disk. `.gitignore` caught `build/`, `bin/`, and the usual extensions, but not an extensionless binary outside those directories, so it slipped through. Binaries in git bloat every future clone forever (history doesn't shrink on its own) and are exactly the kind of thing code review is supposed to catch and, here, didn't.

## Decision

`git rm --cached` on it, deleted it from disk after confirming it really was just a build artifact, and hardened `.gitignore` with explicit entries for both binary names plus a pattern for the new `build-*/` directories the expanded CI pipeline introduced.

## Alternatives considered

A full history rewrite (`git filter-repo`, force-push) would actually remove the blob from old commits, not just stop tracking it going forward. Left undone on purpose — it rewrites commit hashes and means anyone with an existing clone has to re-fetch, which isn't a call to make unilaterally.

Deleting the file without also fixing `.gitignore` would've fixed the symptom and left the actual gap — an extensionless binary at the repo root still isn't caught by anything — ready to happen again the next time someone builds locally at the root.

## Consequences

New clones don't get the stray binary anymore. It's still sitting in history in the commits before this fix, and will stay there until someone decides the history rewrite is worth doing.
