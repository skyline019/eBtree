# ADR-018: Sustained kSync performance strategy

## Status

Accepted (2026-06-29)

## Context

Production defaults are `DurabilityClass::kBalanced` via `ProductionDefaults` (`WalBatchPipeline`). Enterprise/compliance tier uses `EnterpriseDefaults` (kSync, `sync_on_commit=true`).

## Decision

1. **Product vs benchmark tiers**
   - Production: `ProductionDefaults` — kBalanced, page_cache **128**, histogram summaries.
   - Enterprise: `EnterpriseDefaults` — kSync, page_cache **128**.
   - Benchmark comparison: `BenchmarkGroupDefaults` — kGroup, batch 512, page_cache 256.
   - Record both tiers in ADR-009 via `perf_matrix_bench` and dedicated kSync write section.

2. **kSync write hot path**
   - Reuse WAL sync handle (`FlushFileBuffers` / `fdatasync`) instead of opening the WAL file per Put.
   - `PutSyncFast` / `DeleteSyncFast`: inline WAL append → memtable → fsync, bypassing `SyncExecutor::Dispatch(kWrite)` for kSync.
   - Optional `eager_shard_open` pre-creates all shards at Engine open for multishard benches.

3. **Read / lazy Scan**
   - DataFile persistent read handle + batch reads (`ReadRecordsAtOffsets`).
   - Optional `.didx` sidecar for LSN index; saved on Checkpoint.
   - `ResolveScanValues`: committed hash fast path; lazy path sorts offsets and batch-parses.
   - **Committed Scan direct path**: `ScanCommittedDirect` bypasses B-Tree when memtables are empty post-reopen.

4. **Scan B-Tree**
   - `ScanLeafChain` collects leaf offsets and uses `ReadPages` batch read.
   - `PageFile::ReadPages` sorts offsets and merges adjacent page reads.

5. **Sync dispatch**
   - `kRead` is a no-op rule; Get/Scan skip `SyncEventType::kRead` dispatch on the hot path.

6. **Regression policy**
   - Release-only smoke in `perf_regression_test`: kSync 10k Put <500ms, single-shard Scan 10k <40ms (tighten toward 15ms over time).
   - Not a CI hard gate (hardware-dependent), same as RTO policy.

## Consequences

- kSync write approaches fsync-limited throughput on NVMe.
- Lazy Scan cost drops from per-hit `ifstream` open to mmap batch IO.
- page_cache 128 increases RAM ~32MB budget vs 64; configurable via `EngineOptions`.
- Durability semantics unchanged: every kSync Put still calls `WalWriter::Fsync()`.

## References

- [ADR-009](009-perf-baseline.md) — dual matrix baselines
- [ADR-017](017-lazy-committed-load.md) — lazy committed load semantics
