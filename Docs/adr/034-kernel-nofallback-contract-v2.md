# ADR-034: Kernel No-Fallback Contract v2

## Status

Accepted (2026-07-01)

## Context

ADR-019 introduced explicit ReadTier telemetry and `unexpected_path_total`. Subset B and P9 kernel work require **structural** no-fallback: no implicit IO backend downgrade, full tier coverage in matrix tests, and RAR tier-consistency attestation (ADR-038).

## Decision

### Contract

1. All Get/Scan paths route through `ReadResolver` / `ScanResolver` with **mandatory** `RecordReadTier`.
2. `unexpected_path_total > 0` is a **hard failure** in matrix, forbidden, audit, and PolicyGate.
3. `fallback_read_total` is **deprecated**; matrix asserts use `unexpected_path_total=0` only.
4. `ReadTier::kCount` and unknown branches increment `unexpected_path_total`.

### Tier dispatch

| RecoveryState | Get primary tiers | Scan primary tiers |
|---------------|-------------------|--------------------|
| kCommittedCold | MemTable, Committed | CommittedDirectScan |
| kCommittedHot | MemTable, Committed, BTreeDisk | BTreeScanResolve |
| kOnDiskLazy | MemTable, BTreeDisk, DataFileLsn, WalSingleKey | BTreeScanResolve |
| kWalPending | MemTable, Committed (+ replay on write) | replay then BTreeScanResolve |
| kWalCorrupt | TLog-bound paths | TLog ScanAsOf |
| kLazyKey | … WalSingleKey | BTreeScanResolve + batch LSN |

Explicit TLog / WAL fallback paths are **named RecoveryStates**, not hidden fallbacks.

### Verification

- `no_fallback.matrix` ≥ 16 cases (one hit per ReadTier minimum)
- `random_powerfail_test` optional 500-trial RAR hook (P9-audit)
- Forbidden manifest unchanged except matrix stat rename

## Consequences

- Resolver refactors must preserve tier semantics; pure-move PRs require matrix green.
- RAR `tier_consistency_attestor` validates RecoveryState vs probe tier (ADR-038).

## References

- ADR-019, ADR-024, ADR-038
