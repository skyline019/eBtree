# ADR-008: P9 deferred scope

## Completed in P6-core

- RCU mmap read windows for DataFile (`MmapWindowManager`, ADR-010)
- Multi-shard vShard routing (`shard_count` 1/4/16, ADR-011)
- Chaos harness (1000 ops, powerfail/bad-block, 4-shard)
- Scale bulk (100k keys / 10k CI)
- RTO 10k mmap (80ms local / 150ms CI)

## Completed in P7-full

- Paged on-disk B-Tree (`PagedBTree`, `shard{N}.pages`, SuperBlock `active_root`)
- Background flush worker (ADR-013)
- Concurrent read API + shared_mutex (ADR-012)
- High-concurrency chaos (512 reader threads local / 64 CI)
- Sequential mmap window scan for large DataFiles

## Completed in P8 C++ kernel

- True internal+leaf paged B-Tree (ADR-014)
- Trie prefix page pruning (ADR-015)
- 256 vShard routing (`shard_count` 1/4/16/256)
- Forbidden manifest enforcement + test floor ≥165
- Hot-path perf: DataFile persistent append, incremental summaries, mmap window reuse, parallel shard open

## Deferred to P9

- Rust FFI / CUE / SQL preparer
- 512→1000 thread local chaos scale (optional after perf validation)
- Histogram summary type (blueprint type 2)

## Rationale

P8 closes C++ storage, concurrency, and routing debt. Language surfaces and advanced summary types remain out of kernel scope until P9.

## Non-deferred

Kernel C++ remains the product direction.
