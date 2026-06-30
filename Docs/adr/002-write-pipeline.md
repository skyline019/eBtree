# ADR-002: Write pipeline and durability classes

## Write path order

1. WAL append (I-D1)
2. MemTable active put/delete
3. Durability hook (sync / group / async)
4. Flusher (on `Flush` or `Checkpoint`): rotate active→frozen, fsync WAL, append frozen to DataFile, update `committed_` and BTree, advance stable_lsn

DataFile, `committed_`, and BTree are **not** updated on the write path. Only Flusher (and Recovery datafile load) populate the durable index.

## MemTable triple buffer

- `memtable_`: active writes
- `immutable_`: rotated snapshot awaiting flush
- `flushing_`: Flusher target (formerly dual-buffer `frozen_`)
- `Flush()` calls `RotateMemTableForFlush()` then `Flusher::Flush` on `flushing_`
- Reads consult active → immutable → flushing → `committed_`
- `Scan` overlays MemTable keys onto BTree hits before `ReadVisible`

See [ADR-007](007-memtable-3stage.md).

## Durability classes

| Class | Put behavior | stable_lsn advanced |
|-------|--------------|----------------------|
| kSync | WAL fsync per put | On each put |
| kGroup | Batch; explicit or auto `GroupCommit()` | On group commit |
| kAsync | No fsync on put | On flush / group commit / checkpoint |

`Checkpoint()` for kGroup runs `GroupCommit()` before flush and superblock commit.

## Recovery

1. Load SuperBlock; reject corrupt slots
2. DataFile → `committed_` + BTree
3. WAL replay (lsn > superblock.wal_lsn) → **active MemTable** only

## Sync rules

- `kWrite`: core_rules (WAL + memtable)
- `kGroupCommit`: GroupCommitter::Commit
- `kFlush`: Flusher via Engine::FlushInternal
- `kSuperBlockCommit`: dual-slot superblock

See [SYNC_MANIFEST.yaml](../syncs/SYNC_MANIFEST.yaml).
