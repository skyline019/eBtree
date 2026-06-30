# ADR-027: Recovery and Self-Heal (Phase 4)

## Status

Accepted — Phase 4 recovery hardening, async summary validation, gate alignment.

## Context

Phase 3d completed SQL OLTP exec. Phase 4 closes kernel recovery/self-heal debt from `text.txt` Week 7–8: lazy recovery, summary drift repair, GC generation visibility, RTO < 80ms (including bad-block reopen).

## Decision

### Capabilities

| Capability | Mechanism | Tests |
|------------|-----------|-------|
| Lazy recovery | ADR-004 FastOpen + lazy key restore + T-Log fallback | `recovery.matrix`, `EbFailureLazyRecovery.*` |
| On-demand summary heal | `RepairSummary()` on stale Get/Scan | `EbSummaryHeal.*`, `EbInvariantSummary.*` |
| **Async summary validate** | `BackgroundSummaryValidator` after Open | `EbSummaryAsyncHeal.*` |
| GC hot/cold (active/reclaim) | ADR-006 generation swap | `EbPipelineGc.*` (`ReclaimGenerationInvisibleAfterReopen`), `EbInvariantGc.*` |
| RTO 80ms | FastOpen timing budgets | `EbPipelineRto.*` |

### BackgroundSummaryValidator

- Started per `ShardEngine` after successful `Recover()` when `EngineOptions::background_summary_validate` is true (production defaults enable it).
- Thread checks `BTreeIndex::SummaryDrifted()` (`summary_lsn < max_lsn`); on drift calls existing `RepairSummary()`.
- Increments `summary_repair_total`; must **not** increment `recovery_total`.
- No WAL replay, flush, or memtable semantics changes ([ADR-024](024-kernel-partial-unfreeze.md) Phase 4 supplement).

### Gates

| Gate | Suites |
|------|--------|
| **P4** | `pipeline`, `failure`, `complex`, `matrix/recovery` |
| **P4-complete** | `unit`, `failure`, `pipeline`, `complex`, `composite`, `matrix` |

SQL/audit smoke (`--suite=sql,audit`) runs after kernel changes; not part of P4 incremental gate.

## Merge gates

- `run_tests.ps1 -Gate P4 -Config Release`
- `run_tests.ps1 -Gate P4-complete -Config Release`
- `ebtree_test_runner --suite=sql,audit` green
- Forbidden manifest counters zero

## References

- [ADR-004](004-lazy-recovery.md), [ADR-006](006-active-reclaim-gc.md), [ADR-024](024-kernel-partial-unfreeze.md)
