# ADR-046: LSN-Native Snapshot Read View (LSV / FPBC)

## Status

Accepted (2026-07-02)

## Context

eB-Tree already separates index pointers (`key → data_lsn`) from payload (`DataFile` append-by-lsn) and maintains monotonic LSNs. ADR-043 listed engine MVCC as non-goal; this ADR introduces **LSV** as a bounded, LSN-native read-isolation layer without page-level multi-version B-Trees.

## Decision

### Architecture (FPBC)

- **Forward**: B-Tree `key → latest_lsn` (unchanged semantics)
- **Backward**: `VersionChainStore` (VCS) per-key `(lsn, prev_lsn)` chain — **A scheme**: no offsets in VCS
- **Payload**: `DataFile` + `.didx` only

### Invariants

| ID | Rule |
|----|------|
| I-VCS-FWD | When key exists in B-Tree: `vcs.Head(key) == btree[key]` |
| I-VCS-GC | DataFile records referenced by VCS with `lsn > pin_lsn` are not reclaimed |
| I-VCS-WAL | `WAL.TruncateTo` only after `FoldWalToVcs` completes |
| I-VCS-TOMB | Delete lsns remain on chain; `floor(S)` tombstone → NotFound |
| I-VCS-TIER | VCS reads use `ReadTier::kVersionChain`; `unexpected_path_total == 0` |
| I-VCS-SQL | Snapshot reads ignore other txns' uncommitted memtable writes |

### Checkpoint order

1. Flush (Flusher → DataFile + B-Tree + VCS)
2. FoldWalToVcs
3. Save `.didx` + `.vidx`
4. T-Log snapshot
5. SuperBlock commit
6. WAL truncate

### SQL

- `BEGIN` captures `SnapshotToken` + assigns `txn_id`
- SELECT uses snapshot reads; DML attaches `txn_id` to memtable entries
- `COMMIT` promotes txn memtable entries to durable visibility

### Gate

**P19-lsv** — independent of P17/P18 streak contracts.

**Acceptance (2026-07-02 MVP)**: P19-lsv gate green (~70s); streak **10/10 ACCEPT** — `.test-runs/streak-20260702-153149`.

**Strict acceptance (2026-07-02)**: P19-lsv strict gate green (SPF-RW + GetAtSnapshot WAL upgrade + WAL replay `durable=false`); streak **10/10 ACCEPT** — `.test-runs/streak-20260702-200714` (282–475 s/trial). Archive: [`Docs/archive/lsv/lsv-strict-implementation-2026-07-02.md`](../archive/lsv/lsv-strict-implementation-2026-07-02.md).

### Strict completion (MVP → strict)

| Area | MVP | Strict |
|------|-----|--------|
| VCS storage | In-memory chain + `.vidx` | Hot inline (≤8) + `VcsPager` overflow in `shard{N}.vcs` |
| GC | Skip swap when `snapshot_pin_count > 0` | Defer swap when pinned VCS lsns overlap reclaim generation |
| Perf gate | 2 relaxed tests | 4 LSV perf gates (`LsvPerfRegression.*`) |
| Powerfail | Single durability | `sync` / `balanced` / `group` matrix + `SnapshotOracle` |
| Recovery tier | VCS + btree | `ReadTier::kWalSnapshotKey` when VCS incomplete + WAL pending |

### Test harness

`test/runner/test_runner.cc` skips `FreeLibrary` on Windows to avoid DllMain deadlocks with background engine threads. This is a harness constraint, not a product defect.

**Snapshot-Priority Fair RW lock (SPF-RW)** replaces `std::shared_mutex` on `ShardEngine::rw_mu_`:
- Foreground shared locks: `Get` / `GetAtSnapshot` / scan readers.
- Foreground exclusive: Put/Delete/Checkpoint (waits for readers).
- Background exclusive (`flush_worker`, `summary_validator`): `try_lock_background_exclusive()` yields when `snapshot_pin_count > 0` or readers are active, eliminating snapshot reader starvation on MSVC.

`GetAtSnapshot` uses the same WAL-replay / summary-repair upgrade path as `Get()` when `wal_replay_pending` or stale summary is detected.

## Non-goals

Distributed replication, SSI/Serializable, multishard RAR Phase 6 backlog.

## References

- ADR-002, ADR-006, ADR-017, ADR-034, ADR-043, ADR-045
