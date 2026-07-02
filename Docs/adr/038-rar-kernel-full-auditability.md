# ADR-038: RAR–Kernel Full Auditability

## Status

Accepted (2026-07-01)

## Context

ADR-022/026 define RAR v1/v2 and `AttestExport`. P9 requires **complete auditability**: tier consistency, sidecar hash chain, checkpoint attestation, compress/forbidden stats — without slowing default Put/Scan (async checkpoint RAR).

## Decision

### RAR v3 sections

- `kernel`: AttestExport v2 stats (forbidden subset, compress, pages_touched, checkpoint_lsn)
- `tier_contract`: RecoveryState vs probe ReadTier consistency
- `sidecar_chain`: `prev_rar_sha256`, `op_log_head_sha256`, `sequence`

### AttestExport v2 (kernel whitelist, read-only)

Extends v1 with:

- `EngineStatsSnapshot`: fsync_merge_ratio, wal_full_scan_total, …
- `CompressStatsSnapshot`: per-codec counts, bytes_saved, decompress_fail
- `ForbiddenStatsSnapshot`: unexpected_path_total (aggregate)

`AttestExportSnapshot` is the no-probe variant used by the async chain worker (stats/recovery only).

### CheckpointObserver + async chain (L0)

```cpp
using CheckpointObserver = std::function<void(Engine*, uint64_t checkpoint_lsn)>;
void Engine::SetCheckpointObserver(CheckpointObserver cb);
```

- Kernel invokes callback **after** superblock commit
- Observer sync path: enqueue only (<1µs target); JSON/IO on `RarChainWorker` thread
- Chain file: `{engine_path}/ebtree.rar.chain.jsonl` — one JSON object per line with `sequence`, `checkpoint_lsn`, `prev_rar_sha256`, `rar_sha256`, `op_log_head_sha256`, `body_json`
- Queue full → drop + increment `EngineStats::rar_chain_drop_total`; never rollback checkpoint

### SQL attestation levels

| Mode | Open | Runtime writes |
|------|------|----------------|
| `OFF` | no sync BuildRar | allowed |
| `MONITOR` | no sync BuildRar; async chain on | **refused** when `unexpected_path_total > 0` or decompress policy violated; reads allowed |
| `REQUIRE_PASS` | sync BuildRar; REFUSE on fail | allowed if open succeeded |
| `ALLOW_WARN` | sync BuildRar; WARN allowed | allowed if open succeeded |

`WITH ATTESTATION MONITOR` in SQL OPEN; C API: `EBTREE_SQL_ATTEST_MONITOR = 3`.

### PolicyGate v2

- `require_tier_consistent: true` → REFUSE_START on mismatch
- `max_decompress_fail: 0` (reads `kernel.compress.decompress_fail`)
- `EvaluateSnapshotPolicy()` for chain snapshot evaluation
- Existing: `require_unexpected_path_zero`, `recovery_max_missing`

### Sidecar files

| File | Purpose |
|------|---------|
| `ebtree.op_log.jsonl` | durable boundary (ADR-023) |
| `ebtree.rar.chain.jsonl` | canonical RAR chain |

CLI: `ebtree_audit chain-verify --path <dir> [--chain <file>]`

### Performance / durability (dual-lossless)

- **Perf**: checkpoint observer enqueue-only; `RarChainPerf.CheckpointObserverEnqueueBudget` ≥0.99× baseline (Release)
- **Durability**: observer strictly post-commit; chain write failure degrades to WARN/drop, never rolls back checkpoint
- `EngineOptions::attestation_async` default **true** for checkpoint RAR
- Open attestation remains sync opt-in (SQL REQUIRE_PASS); **Standard default is MONITOR** per [ADR-040](040-rar-standard-sku-defaults.md)

## Gate

- `P9-audit-complete`: audit suite, RarTierConsistency
- `P14-rar-dynamic`: `RarChainRoundtrip`, `RarAsyncCheckpoint`, `RarChainPolicyDecompress`, `RarBuildRarV3Sections`, `SqlRarMonitor`
- `P14-rar-product`: default MONITOR smoke, `PRAGMA rar_status`, `KBalancedWrite100kWithRarMonitor`, `RarChainAutoSign` (when signing enabled)

## References

- ADR-022, ADR-023, ADR-026, ADR-034, ADR-035, ADR-024, **ADR-040**
