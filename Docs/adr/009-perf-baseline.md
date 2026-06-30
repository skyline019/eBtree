# ADR-009: Performance baseline (bench)

## Targets (text.txt Phase 3)

- Write kBalanced (Production): **100k+ TPS** concurrent durable single shard
- Write kSync (Enterprise): fsync-limited (~2k TPS this host)
- Write kGroup (bench): 50k+ TPS; **120–150k** single shard post perf plan
- Point read: P99 < 1ms; **lazy cold on-disk P99 < 0.7ms**
- Scan 10k: **P50 < 15ms** single shard (kSync reopen + committed)

## Harness

Build with `-DEBTREE_BUILD_BENCH=ON`:

- `ebtree_write_bench` — 100k Put kGroup + 100k Put kSync (Enterprise) + kBalanced (`ProductionDefaults`)
- `ebtree_read_bench` — 10k Get warm + cold + cold_ondisk; prints durability + page_cache
- `ebtree_scan_bench` — 10k range Scan after reopen (normal + lazy); Production defaults
- `ebtree_perf_matrix_bench` — write/read/scan × {kSync,kBalanced,kGroup} × {committed,lazy} matrix
- `ebtree_multishard_write_bench [1|4|16] [ksync=0|1]` — eager_shard_open + per-shard ops
- `ebtree_multishard_scan_bench [shards] [lazy]` — multishard range Scan after checkpoint

`EngineOptions::ProductionDefaults(path)`: **kBalanced**, fsync_batch=512, wait=600 µs, wal_durable_batch_bytes=16384, page_cache **128**.

`EngineOptions::EnterpriseDefaults(path)`: kSync, fsync_batch=1, wait=0, page_cache **128**.

`EngineOptions::StandardDefaults(path)`: alias of `ProductionDefaults`.

`EngineOptions::BenchmarkGroupDefaults(path)`: kGroup, batch 512, page_cache 256.

## CI policy

Bench is **not** a hard gate (hardware-dependent). Release smoke tests in `perf_regression_test` provide soft budgets.

## Baseline matrix (kSync sustained perf plan)

Windows MSVC Release (kSync sustained perf plan, local run 2026-06-29):

```
write_bench_kgroup ops=100000 elapsed_sec=0.280 ops_per_sec=356782
write_bench_ksync ops=100000 elapsed_sec=51.193 ops_per_sec=1953
read_bench durability=kSync page_cache=128
read_bench_warm p50_us=0.2 p99_us=0.7
read_bench_cold p50_us=0.2 p99_us=0.4
read_bench_cold_ondisk p50_us=42.5 p99_us=121.0
scan_bench durability=kSync page_cache=128 p50_ms=2.119 p99_ms=2.613
scan_bench_lazy p50_ms=29.297
write_bench_ksync ops_per_sec=1775
write_bench_kgroup ops_per_sec=416445
perf_matrix_bench:
  kGroup committed write=379156 read_p99_us=0.8 scan_p50_ms=53.5
  kGroup lazy       write=365682 read_p99_us=0.7 scan_p50_ms=56.9
  kSync committed   write=1963   read_p99_us=0.7 scan_p50_ms=73.0
  kSync lazy        write=1939   read_p99_us=0.9 scan_p50_ms=69.8
```

kSync write is fsync-limited on this host (~2k ops/s); NVMe targets in ADR-018 apply to faster storage.
Lazy Scan improved from ~334ms to **36ms** via persistent-read batch resolve + leaf ReadPages batching.

## 三档基线 (2026-06-29, archived)

Windows MSVC Release, local run. kBalanced concurrent write uses **`WalBatchPipeline`** with `min(128, hw×16)` threads (normalized load). Batch memtable apply via worker `commit_hook_` (Put/Delete no per-op `rw_mu_` exclusive).

```
write_bench_kbalanced ops=10000   ops_per_sec=1227  (sequential, sparse timeout)
write_bench_kbalanced_128thread ops=100096 ops_per_sec=122411 fsync_merge_ratio=10 target_100k=PASS

read_bench / scan_bench / kSync / kGroup: see prior sections
```

| Tier | Put SLA | Normalized concurrent write |
|------|---------|----------------------------|
| kBalanced (Production) | durable at return | **~120k/s** sustained (batch memtable apply); **100k stretch PASS** |
| kSync (Enterprise) | durable at return | ~1.7–1.8k/s |
| kGroup | append only | ~405k/s |

CI gate: `KBalancedWrite100kConcurrentBudget` ≥ **100k ops/s** (Release NDEBUG) / **85k** (`EBTEST_CI`); `fsync_merge_ratio` ≥ **8** (Release). Bench prints `target_100k=PASS|FAIL` as stretch label.

Random powerfail: `EbFailureRandomPowerfail.*` — seed fuzz, concurrent destroy, mid-checkpoint hook.

## P9-perf gate (Release)

- Pipeline perf tests pass; FastOpen10k RTO **<80ms**
- kSync 10k Put smoke **<500ms**
- kSync Scan 10k smoke **≤40ms** with `lazy_committed_load=true` (`EnterpriseDefaults`); **≤15ms** committed-direct when lazy off

Committed Scan fast path (`ScanCommittedDirect`): when `RecoveryState=kCommittedCold`, summary covers snapshot, memtables empty, and WAL replay is not pending, range Scan iterates `committed_` directly.

Lazy on-disk Scan uses `ReadResolver`/`ScanResolver` + `DataFileReader` batch resolve (no per-key `ReadVisible` on cold lazy reopen).

kSync write uses `WalFsyncCoordinator` (ADR-020): Put releases `rw_mu_` during fsync; multi-thread bench reports `fsync_merge_ratio`.

## Historical

See git history for pre-2026-06-29 baselines (write ~51k kSync-equivalent, scan ~26ms).

## References

- [ADR-017](017-lazy-committed-load.md)
- [ADR-018](018-sustained-perf-ksync.md)
- [ADR-019](019-explicit-read-tiers.md)
- [ADR-020](020-wal-fsync-coordinator.md)
- [ADR-021](021-balanced-microbatch-durable.md)
