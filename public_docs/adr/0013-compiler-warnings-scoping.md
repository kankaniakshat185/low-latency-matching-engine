# ADR-0013: Scope `-Werror` to our own targets

Status: Accepted
Date: 2026-08-05

## Context

`-Wall -Wextra -Werror` was set globally in `CMakeLists.txt`, before GoogleTest got fetched — which means it applied to GoogleTest's own source too. A dependency whose warning history we don't control. Next compiler upgrade, or next GTest release, one new warning in vendored code and the build breaks for a reason that has nothing to do with anything we changed.

## Decision

Dropped the global flag. Warnings now go on via `target_compile_options` on exactly three targets — `engine`, `engine_tests`, `engine_benchmark`. The fetched `gtest`/`gtest_main` targets never see them.

## Alternatives considered

Keeping the global flag and adding `-Wno-error` exceptions as GoogleTest warnings pop up is reactive by definition — you only find out after something breaks, and every new GTest version could need a different exception.

Pinning GoogleTest to a version known to compile clean (already true, v1.14.0) helps but doesn't cover the compiler side — a newer Clang or GCC turning on a new default warning would still surface a pre-existing issue in that same pinned source.

## Consequences

A red build on warnings now always means our code changed, never a dependency's problem showing up as our failure. Confirmed by reconfiguring and rebuilding clean across Debug, Release, and the coverage build, with all 26 tests still passing. Three explicit `target_compile_options` lines instead of one global one — a little more typing for real isolation.
