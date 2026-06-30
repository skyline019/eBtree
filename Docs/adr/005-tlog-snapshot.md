# ADR-005: T-Log snapshot chain

## Purpose

Append-only snapshot chain for durable fallback when WAL is corrupt or missing, and for physical flashback queries.

## Record format

16-byte `TLogEntry` (on-disk, unchanged): `page_offset` = DataFile byte size; `timestamp_sec`; `prev_offset` chains entries.

32-byte `TLogIndexEntry` sidecar (`shard0.tlogidx`, 1:1 with `.tlog` entries): `data_lsn`, `wal_lsn`, `datafile_size`, `timestamp_sec`, `file_offset`.

## Write path

After successful `Flush` inside `Checkpoint()`:

1. `TLogWriter::AppendSnapshot(stable_lsn, datafile_size, wal_lsn)`
2. `SuperBlock.tlog_tail` updated on SuperBlock commit

## Recovery

When WAL is corrupt at open:

1. Load SuperBlock (data_lsn, wal_lsn)
2. Load DataFile via T-Log `datafile_size` (`LoadUpToByteOffset`) or `LoadUpToLsn(data_lsn)`
3. Skip WAL replay

When DataFile CRC fails, fall back to T-Log byte-bound load.

## Flashback API

- `GetAsOf(key, timestamp_sec)` / `ScanAsOf(plan, timestamp_sec)`
- `TLogReader::FindSnapshotAt(ts)` — latest snapshot with `timestamp_sec <= ts`
- Partial DataFile load only; no WAL/MemTable overlay (checkpoint-bound history)

## Invariants

- I-D1: T-Log append only after WAL fsync (via Flusher/Checkpoint ordering)
- I-D2: `data_lsn <= stable_lsn` at snapshot time
- I-R2: Bad-block recovery must not increment `fallback_read_total`
- Forbidden: `tlog_tail_stale_after_checkpoint`, `flashback_uses_fallback_read`

## Testing

`SetTimestampSourceForTest()` injects deterministic timestamps (no wall-clock flake).
