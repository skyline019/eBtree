# ADR-004: Lazy recovery

## Modes

| Mode | Trigger | Behavior |
|------|---------|----------|
| kHot | Default after FastOpen | WAL replay deferred until first mutating/read op |
| kLazy | `active_root == 0` in SuperBlock | Skip eager WAL replay; single-key restore from WAL on Get miss |
| kFull | `RecoveryStrategy::kFullReplay` | Legacy eager WAL replay at open |

## FastOpen (default)

1. Load SuperBlock + DataFile → `committed_` + BTree
2. Set `wal_replay_pending_ = true`
3. Do **not** scan WAL at open (RTO target)

Deferred replay runs on first `Put`/`Delete`/`Flush`/`Scan`, or `RecoverFull()`.

## Lazy single-key restore

When `recovery_mode_ == kLazy` and Get misses committed/memtable, replay WAL records for that key only (I-NF3 compliant: not full WAL replay).

## WAL corrupt fallback

If WAL header is corrupt (`BADWAL` test tag), load DataFile up to `SuperBlock.data_lsn` and skip WAL. T-Log snapshot validates durable view (ADR-005).

## Threading

Single-threaded Engine (ADR-003). All recovery under `mu_`.
