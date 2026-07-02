# ADR-044: Phase 4 Stability Sprint

## Status

Accepted (2026-07-01)

Extends [ADR-043](043-kernel-rar-deep-cultivation.md). Phase 5 lazy scan gate tighten **blocked** until P18 stability criteria met.

## Context

ADR-043 landed P17-deep-core (WriteGuard, lazy scan batch IO, perf-first gate order). Post-merge review found:

1. **False-green risk** — P9/P17 used `EbMatrix.NoFallbackMatrixTest.*` (dot) while GTest parameterized names use `EbMatrix/NoFallbackMatrixTest.RunCase/N` (slash); matrix step could run **zero** no-fallback cases.
2. **WriteGuard stability gap** — existing tests bump stats in-process; no powerfail/reopen or real chain queue drop coverage.
3. **Gate doc drift** — baseline doc said ≤40ms local while code gate is 45ms.

**Priority constraint**:

```text
stability regression > perf gate tighten > new features
```

Do **not** tighten lazy scan local gate 45→40ms until P17+P18 GHA **10 consecutive runs ≥9 green**.

## Decision

### Fix no-fallback filter (P0)

Canonical filter (matches [test_runner.cc](../../test/runner/test_runner.cc) matrix/no_fallback):

```text
EbMatrix/NoFallbackMatrixTest.*:EbMatrixSchema.NoFallbackCasesNonEmpty
```

Apply via **separate** `matrix/no_fallback` runner invocation (GTest negative filters after `-` must not be concatenated with matrix sub-filter append).

Add `GateGtestFilterMatchesTests` in manifest_consistency_test to prevent filter drift.

### Gate P18-stability

| Component | Content |
|-----------|---------|
| Suites | unit, failure, audit |
| Filter | `KvRarMonitor*:RarStability*:-RarOracleEquivalence.ProductionRandomDestroy` |
| CI | PR: P4-complete → P17-deep-core → **P18-stability** |

Tests in `test/audit/rar_stability_test.cc`:

| Test | Validates |
|------|-----------|
| `WriteGuardSurvivesPowerfailReopen` | crash/reopen + re-Install keeps WriteGuard |
| `RealChainQueueDropOpensCircuit` | real `RarChainWorker` queue drop (not synthetic stats) |
| `ConcurrentWriteCircuitRace` | concurrent Put under open circuit, no UB |

`RarMonitorOptions.max_queue_depth` exposed (default 64); tests use `0` to force drop on checkpoint enqueue.

### P17 oracle slim (optional, included)

P17 step 2 excludes `RarOracleEquivalence.ProductionRandomDestroy` (~43s). Full oracle remains in `P9-audit-complete`.

### Perf-first order (documented)

Running heavy oracle before perf caused StandardWrite/lazy scan flake locally. P17 runs perf first; do not revert order.

## Phase 5 (blocked)

After P18 GHA stability:

- `DataFile::ReadRecordsAtOffsets` read windows
- `ResolveScanValues` extension
- Gate 45→40ms local (CI stays 60ms)

See ADR-043 Phase 3 backlog and [lazy-scan-track](../archive/perf/lazy-scan-track-2026-07-01.md).

## Acceptance

1. Local Release: P17 + P18 green
2. `NoFallbackMatrixTest` case count > 0 under fixed filter
3. GHA P4+P17+P18: 10 runs ≥9 green (workflow_dispatch or post-merge observation)
4. Docs aligned: 45ms local / 60ms CI

## Consequences

- PR merge path: P4 → P17 → P18
- ADR-043 Phase 3 gate tighten deferred to Phase 5
- Evaluation report updated to ADR-043/044 roadmap

## References

- ADR-043, ADR-034, ADR-038, ADR-040
- [kernel-rar-baseline.md](../archive/perf/kernel-rar-baseline.md)
