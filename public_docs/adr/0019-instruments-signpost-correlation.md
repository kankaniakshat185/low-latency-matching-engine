# ADR-0019: Correlating Instruments CPU Counters data with `os_signpost` markers

Status: Accepted
Date: 2026-08-06

## Context

`variant_benchmark` runs six sequential benchmark passes in one process (1.0 and 2.0, times three workloads). A single Instruments CPU Counters recording captures the whole run as one undifferentiated stream of samples — there's no way to tell which sample belongs to which variant or workload from the raw counter data alone. Getting hardware-counter evidence attributed correctly to "1.0 vs 2.0, per workload" needed a way to mark exact time boundaries around each of the six runs, reproducibly and without manual GUI region-selection.

Getting to this point also needed clearing three environment blockers on this machine, worth recording since they'll recur for 3.0/4.0's profiling: no Xcode installed (only Command Line Tools) → installed Xcode with just macOS platform support (skipped iOS/watchOS/tvOS/visionOS and the predictive-completion model to fit in limited disk space) → `xcode-select --switch` to point at it → `sudo DevToolsSecurity -enable` (Developer Mode was off, which silently hung the CPU Counters recording with zero error message rather than failing loudly — the target process just never progressed).

## Decision

Wrapped each of the six `runBenchmark<...>()` calls in `compare_variants.cpp` with an RAII `SignpostRegion` that emits `os_signpost_interval_begin`/`end` (Apple-only, compiled out via `#if defined(__APPLE__)` everywhere else, including the Linux CI runner) under a distinct name (`"1.0-Random"`, `"2.0-Random"`, etc.). Recorded with `xcrun xctrace record --template 'CPU Counters' --launch -- ./variant_benchmark`, then exported both the `os-signpost` table (for exact begin/end timestamps per named region) and the `CounterMetricAggregatedForProcess` table (for the actual counter samples) via `xcrun xctrace export --xpath ...`, and correlated the two by timestamp with a small Python script — each counter sample gets attributed to whichever signpost window its timestamp falls inside.

## Alternatives considered

Six separate trace recordings (one process launch per variant/workload) were the obvious alternative and got rejected: six times the manual invocation, six times the chance to mismatch a trace file to the wrong run, and no real benefit over one recording with markers.

Reading the results directly in the Instruments GUI (selecting each region visually on the timeline) was rejected for the same reason `xctrace` was used at all instead of Instruments.app itself — this needs to be scriptable and repeatable for 3.0 and 4.0, not a one-off manual exercise redone by hand every time.

## Consequences

**On the CPU Counters template's data itself:** this Xcode version's CPU Counters template defaults to a "Guided / CPU Bottlenecks" configuration, which reports four per-sample values Apple labels `Cycles`, `Instruction Delivery Bottleneck`, `Discarded Bottleneck`, and `Instruction Processing Bottleneck` (a fifth label, `Useful`, appears in the template's internal metadata but isn't present in the exported per-process table). These are Apple's own proprietary categorization (built on an internal, undocumented "Recount" framework) — not raw PMU event names like `L1D_CACHE_MISS` or `BRANCH_MISPRED`. The exact formula relating these four numbers to each other isn't publicly documented, and `Cycles` is not simply their sum (in the Random Prices 1.0 sample, the other three sum to more than `Cycles`, confirming it's an independent counter, not a running total). Reporting the relative change per category, per workload, is defensible; asserting a precise mechanistic story for *why* the magnitude of improvement varies by category and workload is not, and this ADR doesn't attempt to.

**The actual measured deltas** (2.0 vs. 1.0, same signpost-marked windows, same trace):

| Workload | Cycles | Instruction Delivery Bottleneck | Discarded Bottleneck | Instruction Processing Bottleneck |
|---|---|---|---|---|
| Random Prices | −28.7% | **−51.4%** | −14.2% | −16.1% |
| Heavy Cancels | −27.5% | −29.9% | −10.2% | −19.6% |
| Worst Case | −19.8% | −15.4% | −18.0% | **−31.2%** |

Every category improved in every workload — removing per-order heap allocation didn't just save "malloc time" in one isolated place, it measurably reduced cycles lost to all three bottleneck categories simultaneously, consistent with heap allocation's well-known knock-on effects on cache locality across the whole pipeline rather than one cost center. The uneven pattern (Instruction Delivery improving most for Random Prices; Instruction Processing improving most for Worst Case) is real and reproducible in this data, but *why* it varies this way isn't something this ADR can confidently explain yet — noted as a specific thing for 3.0's investigation (which changes the price-level lookup structure directly) to help disentangle, rather than speculated about here.
