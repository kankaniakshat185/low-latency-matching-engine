# ADR-0012: Five-job CI pipeline

Status: Accepted
Date: 2026-07-06 (single job), expanded 2026-08-05

## Context

The original pipeline was one job: Debug with ASan/UBSan, build `engine_tests`, run it. An audit found the gaps that leaves. `engine_benchmark` — and everything only it exercises, like `CSVParser` inside a real `main()` — had zero CI coverage. The `-O3` configuration that actually produces the published numbers had never been confirmed to even compile in CI. No static analysis, no formatting check, no coverage number. "We have tests" and "the code is clean" were both things people had to take on faith.

## Decision

Five jobs now, all sharing one `FETCHCONTENT_BASE_DIR` cache keyed on `CMakeLists.txt`'s hash:

`build_and_test` — the original Debug+sanitizer job, now also building and smoke-testing `engine_benchmark` (`--help`, rejecting `argc > 2`, a real `data/sample.csv` replay).

`release_build` — new. The actual `-O3` configuration, full test suite re-run, benchmark smoke test re-run.

`static_analysis` — new, `clang-tidy` over `src/`, set to `continue-on-error: true`.

`format_check` — new, `clang-format --dry-run -Werror`, and this one *is* blocking.

`coverage` — new, `gcovr` on an instrumented build, summary written to the job's step summary, HTML report uploaded.

## Alternatives considered

One combined job doing all of this sequentially was the simpler option, and it lost because unrelated failures (a style nit vs. a real test failure) get harder to read at a glance when they're all in one log.

Making `static_analysis` blocking right away was tempting and wrong — `clang-tidy` has literally never run against this codebase (not installed on the machine this pipeline was built on, which is its own honest limitation). Turning on a strict check set and failing every PR against findings nobody's triaged yet just teaches people to ignore the check. `continue-on-error` first, tighten later.

A hard coverage threshold — fail under 90%, say — got skipped too. The number here (95.8% line, 100% function, measured locally before trusting the CI job) is reported as a tracked fact. A threshold at this project's size invites padding coverage with low-value tests more than it invites better ones.

## Consequences

Every target that ships now gets built and smoke-tested, not just the test binary — which closes a real gap where the benchmark's own bugs could have regressed silently forever.

Two things flagged rather than assumed clean: the static-analysis job's first real run is genuinely its first run, full stop — no way to dry-run `clang-tidy` locally without an LLVM install that was out of scope here. And the Debug+ASan+UBSan job has only been confirmed to *compile* on the local dev machine, not to run — Apple Clang 17's ASan hangs on any thrown exception on this particular machine, unrelated to this code, reproduced with a ten-line throw/catch loop. Correctness got verified through non-sanitized and UBSan-only builds instead. The first real push is the first real signal for that exact job.
