# Benchmarking Methodology

## Why this document exists
A number without a method behind it isn't evidence, it's a claim. This is the method: how a workload gets generated, how it gets timed, what gets recorded, and — just as important — what's still wrong with the measurement even after all that.

The actual loop, every time a bottleneck gets chased: record a number, guess at a cause, check the guess against hardware counters instead of trusting the guess, then only touch the code once the counters back the guess up. Skipping the counter step and going straight from "guess" to "rewrite" is how you end up with a change that "feels" faster and isn't — see 3.0's regression (`optimization_history.md`) for what happens when you actually measure instead of assuming a rewrite helped.

## The mechanics
Latency is timed around exactly one action (`processOrder` or `cancelOrder`) with `std::chrono::high_resolution_clock`. Every run does a 10% warm-up pass first — unmeasured, discarded — to get the instruction cache and branch predictor out of their cold-start state before timing begins. Three synthetic workloads exist (Random Prices, Heavy Cancels, Worst-Case Same Price), each isolating a different part of the system, plus historical replay from a real CSV (`data/sample.csv` is a small sample shipped in the repo; LOBSTER and similar providers have larger public datasets if you want to replay something bigger). Run synthetic workloads via `./engine_benchmark`, replay via `./engine_benchmark data/sample.csv`, `--help` for the rest — an unrecognized extra argument is a hard error now (exit code 1), not a silent fallback to the synthetic suite.

None of these numbers mean anything without the hardware they were measured on. Every published result names the exact chip, OS version, and compiler flags — a throughput figure with no environment attached is decoration, not data.

## CI covers correctness here, not performance
CI builds and runs the benchmark binary in both Debug (sanitizer) and Release (`-O3`), and smoke-tests it against `data/sample.csv` — a crash or build failure in the exact configuration behind the published numbers gets caught automatically. Performance itself isn't CI-gated, on purpose: shared runners carry too much scheduling noise for a nanosecond-scale number to be a trustworthy pass/fail signal. That work stays local, recorded by hand in `optimization_history.md` — a slower CI job isn't a substitute for real profiling anyway.

## What this still doesn't account for
Three things worth naming rather than glossing over: `std::chrono` itself isn't free (its own syscall overhead skews tail-latency percentiles at the nanosecond scale this project operates at), threads aren't pinned to isolated cores, so P99.9 figures likely include OS scheduling interrupts rather than pure algorithmic stalls, and standard heap allocation introduces its own page-fault variability wherever it's still in the hot path. None of these invalidate the relative comparisons between versions — they're exactly why absolute numbers get a caveat every time they're quoted, and relative deltas (same process, same run) don't.

## Phase 4: Comparing Implementations, Not Just Measuring One

A separate binary, `variant_benchmark`, runs the same three synthetic workloads through every `OrderBook` implementation side by side (1.0, 2.0, ...) using this same three-pass methodology, so the numbers are directly comparable. It's deliberately a different binary from `engine_benchmark` — the README's published 1.0/1.0.1 numbers come from `engine_benchmark` specifically, and this comparison should never be able to change that.

Where available, wall-clock numbers are paired with real hardware-counter evidence (Instruments CPU Counters, correlated to the correct variant/workload via `os_signpost` markers — see ADR-0019) rather than staying at the wall-clock level alone. Full results are in `optimization_history.md`: 2.0 was a consistent win (+33–35% throughput, every hardware bottleneck category improved); 3.0 was a genuine mixed result — two workloads improved substantially, one measurably regressed, for an identified mechanistic reason (ADR-0021); 4.0 closed that regression and then some, and its own hardware-counter nuance (one bottleneck category rising in percentage while its absolute cost held flat or dropped) is worked through in ADR-0022. All three are reported as measured, not smoothed into a uniform "and then it got faster" narrative.

**Caveat that applies to any `variant_benchmark` run**: absolute throughput/latency numbers are sensitive to background system load in a way the *relative* comparison between variants (measured back-to-back, same process, same machine state) is not. Treat absolute figures from this binary as indicative, and re-measure on an idle machine before quoting them outside this project.
