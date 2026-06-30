# ADR-017: Opt-in lazy committed load (LsnIndex)

## Context

P9 on-disk B-Tree query loads root metadata only at Open. The default recover path still mmap-scans the DataFile into `committed_`, which makes cold read benches hit the in-memory hash rather than the disk path.

## Decision

Add `EngineOptions::lazy_committed_load` (default **false**).

When enabled at recover:

1. Skip `LoadDatafileMmap → committed_` population.
2. Build `DataFileLsnIndex` (lsn → file offset) via `BuildFromFile` or incremental append updates.
3. `LoadRoot` unchanged; WAL key index unchanged.

Read path extension (only when `lazy_committed_load || on_disk_mode` and committed miss):

```
MemTable → committed_ → btree.GetFromDisk → ReadValueByLsn → RestoreKeyFromWal
```

Scan value resolution uses `ResolveScanValues`: memtable/committed first; on miss, batch-friendly `ReadValueByLsn` per hit LSN. On-disk Scan does **not** overlay full `committed_`.

## Constraints

- Lazy Get must not increment `wal_full_scan_total` (WAL index only).
- Disk reads via LsnIndex are **not** fallback reads (`fallback_read_total` unchanged).
- GC generation filtering uses `reclaim_generation` on record read.
- Durability / WAL→DataFile ordering (ADR-002) unchanged.

## Status

Accepted (P9-perf follow-up).
