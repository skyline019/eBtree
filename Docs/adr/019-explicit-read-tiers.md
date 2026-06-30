# ADR-019: Explicit Read Tiers — Real No-Fallback

## Status

Accepted (2026-06)

## Context

Prior no-fallback enforcement relied on `fallback_read_total`, which was never incremented in C++ (contractual gate only). Get/Scan used nested if-else paths that could silently fall back between IO backends.

## Decision

1. Introduce `ShardRecoveryState` computed at Open/Checkpoint and cached on `ShardEngine`.
2. Route all Get/Scan through `ReadResolver` / `ScanResolver` with explicit `ReadTier` telemetry.
3. Replace contractual fallback detection with `unexpected_path_total` and `read_tier_hits[]`.
4. Unify hot-path datafile reads via `DataFileReader`; WAL tail checks via `WalSegmentReplayer::HasPending(max_lsn)`.

## Read tier mapping

| RecoveryState | Get | Scan |
|---------------|-----|------|
| kCommittedCold | MemTable → Committed | ScanCommittedDirect |
| kCommittedHot | MemTable → Committed → BTree overlay | BTree + ResolveScanValues |
| kOnDiskLazy | MemTable → BTree → ReadByLsn | BTree + batch LSN |
| kWalPending | Hot path; replay on write/scan | No direct scan |
| kWalCorrupt | TLog-bound committed | TLog ScanAsOf |
| kLazyKey | … → WAL key_index | BTree + batch LSN |

## Consequences

- Matrix/forbidden gates assert `unexpected_path_total == 0`.
- `fallback_read_total` retained for one release compatibility period; deprecated.
