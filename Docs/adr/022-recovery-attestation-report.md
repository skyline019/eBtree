# ADR-022: Recovery Attestation Report (RAR)

## Status

Accepted (2026-06-29)

## Context

After crash or unclean shutdown, embedded databases typically return binary success/failure on reopen. SQLite salvage explicitly marks recovered data as *suspect*. eB-Tree has tier-aware durability (kSync / kBalanced / kGroup), explicit ReadTier telemetry (ADR-019), lazy recovery (ADR-004), and T-Log fallback (ADR-005), plus a fuzz oracle (`CommittedOracle` in `test/helpers/powerfail_fuzz.h`).

We need an **external, structured attestation artifact** that explains recovery without modifying the frozen kernel (`cpp/`).

## Decision

Introduce **Recovery Attestation Report (RAR) v1** as a three-layer JSON artifact produced by `tools/ebtree_audit`:

| Layer | Name | Requires Engine | Purpose |
|-------|------|-----------------|---------|
| 1 | Physical | No | SuperBlock, WAL, DataFile, T-Log integrity and offline reconstruction |
| 2 | Recovery | Yes (read-only Open) | `recovery_state`, `read_tier_hits`, key probes |
| 3 | Contract | Yes + key set | Tier-aware missing/unexpected vs oracle or host expect file |

### Tier contract semantics

| Tier | Durable at Put return | Contract `missing` |
|------|----------------------|-------------------|
| kSync / kBalanced | Yes (RPO=0 at return, ADR-021) | Key in durable set absent or wrong value after reopen |
| kGroup | After GroupCommit only | Same; report `pending_uncommitted` separately |

### Verification modes

| Mode | Expected set | Use |
|------|--------------|-----|
| `visibility` | `SnapshotVisible` pre-crash | Default random destroy fuzz |
| `durable` | Oracle `kv_` | Control ops / mid-checkpoint |

### Policy gate (host/CLI, not kernel)

```json
{
  "recovery_max_missing": 0,
  "allow_unexpected_keys": false,
  "require_unexpected_path_zero": true
}
```

Verdict: `PASS` | `WARN` | `REFUSE_START`.

### kBalanced offline limitation

Micro-batch fsync (ADR-021) means **Layer 1 cannot infer Put-return durability from disk alone**. Layer 1 reports `reconstructed.committed_from_disk` (upper bound). Layer 3 requires:

- **Tests**: in-memory `CommittedOracle`
- **Production**: host sidecar `op_log.jsonl` (future v2)

## Invariants mapped

| Invariant | RAR field |
|-----------|-----------|
| I-D2 | `physical.invariants.data_lsn_le_wal_lsn` |
| I-D4 | `physical.superblock.active_slot`, CRC validity |
| I-NF4 | `recovery.unexpected_path_total` (policy gate) |
| ADR-019 | `recovery.shard_state[]`, `read_tier_hits[]` |

## Consequences

- Kernel remains frozen; attestation lives in `tools/` and `test/audit/`.
- CI adds `audit` test suite with oracle equivalence gates.
- Schema frozen at `tools/ebtree_audit/rar_schema_v1.json`.

## References

- [ADR-001](001-invariants.md), [ADR-004](004-lazy-recovery.md), [ADR-005](005-tlog-snapshot.md)
- [ADR-019](019-explicit-read-tiers.md), [ADR-021](021-balanced-microbatch-durable.md)

## Non-goals (v1)

- Ed25519 signed RAR (v2)
- `Engine::ExportRar()` in kernel
- Pure offline kBalanced durable inference without sidecar
