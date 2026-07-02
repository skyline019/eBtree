# ADR-047: LSV-TSO (Commit OCC + Txn-WAL + TSL-3)

## Status

Accepted (2026-07-02)

## Context

ADR-046 delivered read SI (LSV) without page-MVCC. Gaps remained: DML reads used `Get()` instead of snapshot reads, no lost-update protection, WAL lacked txn metadata, and concurrent powerfail tests required hang watchdogs.

## Decision

### LSV-TSO stack

| Layer | Mechanism |
|-------|-----------|
| Read SI | `GetAtSnapshot` / `ScanAtSnapshot` + `read_set` sampling |
| Write SI | **Commit Ticket OCC** on `write_set` vs snapshot LSN |
| Phantom (RR) | **Range Ticket** registry on range scan + commit check |
| WAL | **v2 records** with `txn_id`; `TxnBegin`/`TxnCommit`/`TxnAbort` |
| Sidecar | `shard{N}.txidx` persisted at checkpoint |
| Lock | **TSL-3** (SPF-RW + L0 append lane) |
| Read perf | **SFS-Read** floor cache keyed by `(key_hash, S_epoch)` |

### Gate

**P20-lsv-tso** — superset of P19-lsv filters plus:

- `SnapshotOccSql.*`, `SnapshotPhantomSql.*`
- `Tsl3Lock*`, `SfsRead*`, `TxnWalRecovery*`

All suites run via `Invoke-TestRunnerWithProgress.ps1` (dual-signal CPU+log stale, exit 124 HANG).

### Non-goals

Serializable/SSI, multi-writer same directory, page-MVCC.

## Acceptance

**2026-07-03**: P20-lsv-tso gate green; ADR-044 streak **10/10 ACCEPT** — `Docs/archive/lsv/streak-20260703-022441/`.

## References

- ADR-046, ADR-044 (streak threshold)
- `Docs/archive/lsv/lsv-tso-implementation-2026-07-02.md`
