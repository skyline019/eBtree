# ADR-028: Flashback and Time-Chain (Phase 5)

## Status

Accepted — Phase 5 T-Log snapshot chain hardening, flashback read-path tests, gate alignment.

## Context

Phase 4 closed recovery/self-heal (ADR-027). Phase 5 hardens the T-Log time chain and physical flashback APIs (`GetAsOf`, `ScanAsOf`) per [ADR-005](005-tlog-snapshot.md). Rust/CUE FFI remains deferred to P9 ([ADR-008](008-p6-deferred.md)).

## Decision

### Capabilities

| Capability | Mechanism | Tests |
|------------|-----------|-------|
| Snapshot chain write | Checkpoint → `TLogWriter::AppendSnapshot` | `EbPipelineTlog.*`, composite |
| Point-in-time read | `TLogReader::FindSnapshotAt` + partial DataFile load | `EbComplexFlashback.*`, `matrix/flashback` |
| Flashback after WAL corrupt | T-Log fallback open + `GetAsOf` | `FlashbackAfterCorruptWal`, composite flashback |
| Multishard routing | `Engine::GetAsOf` single shard; `ScanAsOf` merge | `EbComplexFlashback.Multishard*` |
| T-Log index sidecar 1:1 | `.tlogidx` entry per `.tlog` entry | `EbPipelineTlog.IndexSidecarMatchesTlogEntries` |
| Forbidden zero counters | No `fallback_read` on flashback path | `EbForbiddenEnforcement.FlashbackNoFallbackRead`, matrix assert_stat |

### Read path

- `GetAsOf(key, ts)` routes to owning shard, finds latest snapshot with `timestamp_sec <= ts`, loads DataFile up to snapshot bound, reads BTree.
- `ScanAsOf(plan, ts)` runs per-shard when multishard; merges sorted rows.
- No WAL/MemTable overlay on flashback reads (checkpoint-bound history only).

### Constraints

- No WAL, memtable, or flush semantic changes ([ADR-024](024-kernel-partial-unfreeze.md)).
- Flashback is read-only; kernel whitelist unchanged unless tests expose a bug.
- **Checkpoint timestamp scope**: `Engine::Checkpoint()` freezes one timestamp for all shard T-Log appends via `BeginCheckpointTimestampScope()` so multishard `GetAsOf`/`ScanAsOf` share a logical clock per checkpoint.

### Gates

| Gate | Suites |
|------|--------|
| **P5** | `pipeline`, `failure`, `complex`, `matrix/recovery`, `matrix/flashback` |
| **P5-complete** | `unit`, `failure`, `pipeline`, `complex`, `composite`, `matrix`, `sql`, `audit` |

## Merge gates

- `run_tests.ps1 -Gate P5 -Config Release`
- `run_tests.ps1 -Gate P5-complete -Config Release`
- Forbidden manifest counters zero (`flashback_uses_fallback_read`, `tlog_tail_stale_after_checkpoint`)

## References

- [ADR-005](005-tlog-snapshot.md), [ADR-027](027-recovery-self-heal-spec.md), [ADR-024](024-kernel-partial-unfreeze.md)
