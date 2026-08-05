# ADR-0010: Three-pass benchmark methodology

Status: Accepted
Date: 2026-07-06, timer-contamination fix 2026-07-20

## Context

Throughput and per-operation latency answer different questions, and measuring both in one pass is a good way to corrupt both. Time every operation individually while also accumulating a throughput total, and the timer's own overhead — `std::chrono::high_resolution_clock`, roughly 20–40ns per call for the syscall/vDSO — gets counted twice per action and bleeds straight into the throughput number. Cold caches and an unwarmed branch predictor at the start of a run skew things further.

## Decision

Every scenario runs three passes. Warm-up first — 10% of the actions, no timing at all, just to get instruction caches and branch prediction into a steady state. Then a throughput pass: one `Timer` wraps the whole remaining batch, no per-operation timer calls inside it, so the wall-clock time reflects the engine's work and nothing else. Then a latency pass: a fresh `MatchingEngine` replays the identical sequence from scratch (including its own warm-up), with a new `Timer` constructed and read around every single operation, to get real percentiles.

## Alternatives considered

The obvious alternative — one pass, timing both things at once — is actually what shipped originally, and it was wrong. Per-op timer calls were sitting inside the throughput pass, contaminating it with observer overhead. Fixed on 2026-07-20; see the `1.0.1` row in `optimization_history.md` for the before/after numbers that fix produced.

`rdtsc` instead of `std::chrono` would cut the residual overhead further but adds platform-specific code nobody's needed yet — noted as future work, not adopted.

Core pinning (`taskset`) to cut scheduler jitter in the tails is the same story: known, useful, not done yet, written down as a threat to validity instead of quietly ignored.

## Consequences

Throughput and latency each get measured by the pass built for them, with the main confounding factor designed out on purpose. Every measurement limitation that's still open — observer overhead, scheduler jitter, allocator noise, no hardware profiling — is written down in `benchmarking.md` rather than left for someone to rediscover.

The cost: three passes means each scenario processes its action set something like 2.2x over. Worth it for numbers you can actually trust.
