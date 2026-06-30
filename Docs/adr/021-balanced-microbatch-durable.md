# ADR-021: kBalanced (Micro-batch Durable)

## Status

Accepted (2026-06-29)

## Context

Three durability SKUs serve different SLA/throughput tradeoffs:

| Tier | Factory | Put return SLA | fsync strategy |
|------|---------|----------------|----------------|
| **kBalanced** (Production) | `ProductionDefaults` | `flushed_lsn ≥ put_lsn` | `WalBatchPipeline` + 16KB NO_BUFFERING |
| **kSync** (Enterprise) | `EnterpriseDefaults` | same as kBalanced | batch=1, wait=0 |
| **kGroup** (Benchmark) | `BenchmarkGroupDefaults` | append only | explicit/auto `GroupCommit()` |

`StandardDefaults` is an alias of `ProductionDefaults`.

kBalanced keeps **kSync-equivalent durability at Put return** while amortizing fsync via async pipeline batching.

## Decision

1. `ProductionDefaults`: `fsync_batch_size=512`, `fsync_max_wait_us=600`, `wal_durable_batch_bytes=16384`.
2. **WalBatchPipeline** (kBalanced write path):
   - Producers enqueue jobs; dedicated worker drains with `AppendMany` (single WAL lock) + one `Fsync`.
   - After fsync, **batch commit hook** applies the whole batch to memtable under one `rw_mu_` exclusive lock (`ApplyWalBatchLocked`).
   - Flush when staging ≥ 16KB, batch ≥ 512, or sparse timeout (600 µs, queue ≤ 16).
   - Burst drain loop (up to 32 consecutive drains); per-op `shared_ptr` jobs with per-job wait.
   - High-load worker loop skips 20µs sleep when `ShouldFlushLocked` is already true.
3. **WalWriter write-through**: staging buffer, 4KB `NO_BUFFERING` sectors + partial `WRITE_THROUGH` tail.
4. Put returns only after job durable **and** batch memtable apply; `stable_lsn` advanced in `ApplyWalBatchLocked` (Put/Delete threads do not take per-op `rw_mu_` exclusive).

## Semantics

```text
Put → enqueue(job) → worker: AppendMany(N) → Fsync → ApplyWalBatch(memtable) → notify jobs
```

- **RPO at Put return = 0** (same as kSync).
- **Throughput scales with concurrent writers** (queue depth ≈ in-flight Put threads). Bench uses `min(128, hw×16)` threads as normalized load.

## Configuration

| Option | Enterprise (kSync) | Production (kBalanced) |
|--------|--------------------|-------------------------|
| `fsync_batch_size` | 1 | 512 |
| `fsync_max_wait_us` | 0 | 600 |
| `wal_durable_batch_bytes` | 4096 | 16384 |

## Telemetry

`fsync_batch_total`, `fsync_waiter_total`, `fsync_merge_ratio` in `EngineStats`.

## Normalized baseline (this host, Release, archived 2026-06-29)

- Concurrent durable write: **~120k ops/s** sustained (`write_bench_kbalanced_128thread`, `fsync_merge_ratio≈10`); **100k stretch PASS** (`target_100k=PASS`)
- CI gate: `KBalancedWrite100kConcurrentBudget` ≥ **100k ops/s** (Release NDEBUG); **85k** (`EBTEST_CI`); `fsync_merge_ratio` ≥ **8** (Release)

## Checkpoint WAL truncate

After successful superblock commit, `WalWriter::TruncateTo(wal_lsn)` removes replayed WAL prefix (records with `lsn ≤ wal_lsn`), preserving `max_lsn` continuity for subsequent appends.

## Random powerfail fuzz

`test/failure/random_powerfail_test.cc` + `test/helpers/powerfail_fuzz.h`: seed-driven op sequences, random destroy index, `CommittedOracle` tier semantics, concurrent Production destroy, mid-checkpoint hook (`CheckpointPhase`).

## References

- [ADR-009](009-perf-baseline.md)
- [ADR-020](020-wal-fsync-coordinator.md)
