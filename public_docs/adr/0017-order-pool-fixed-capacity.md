# ADR-0017: Fixed-capacity pool for 2.0's order storage, not a growable one

Status: Accepted
Date: 2026-08-06

## Context

2.0 replaces `std::list<Order>`'s node allocation with an intrusive linked list backed by a custom pool, specifically to get `malloc`/`free` off the hot path. Before writing that pool, it needs one design decision made: what happens when it runs out of slots — grow, or refuse.

## Decision

Fixed capacity, sized generously upfront (a constructor/template parameter, not a hardcoded constant), backed by a single pre-allocated array of node slots and a LIFO free-list of indices for O(1) alloc/free. Exceeding capacity is a hard error — throws, the same way a duplicate `OrderId` or a zero-quantity order does — not a silent fallback to heap allocation.

## Alternatives considered

A growable pool (allocate a new chunk and extend the free-list when exhausted) was the obvious "safer" option and it's actually the wrong one here. Growth means an allocation event at an unpredictable moment — and the moment a pool actually needs to grow is precisely when the book is under the heaviest load, which is the worst possible time to introduce a latency spike. A pool that can silently grow under load defeats the entire point of having a pool in the first place. Falling back to plain `malloc` on exhaustion has the same problem by a different route.

## Consequences

2.0 needs a defined, sized-for-the-workload capacity before it can be benchmarked at all — this has to be picked deliberately (based on what the synthetic workloads and `data/sample.csv` actually need) rather than guessed. Hitting the cap during a real benchmark run is a bug in the sizing, not a case to handle gracefully, and it should be loud when it happens, not quietly slow.
