# ADR-043: Kernel × RAR Deep Cultivation

## Status

Accepted (2026-07-01)

Supersedes active roadmap from [ADR-042](042-phase2-evidence-sprint.md). CIDR / demo / market sprint **deferred**.

## Context

Phase 2 closed CARL engineering and CIDR evidence artifacts. Product ROI now shifts to **storage kernel correctness + performance** and **RAR product boundary hardening** — not conference packaging.

Objective evaluation (2026-07-01) identified:

1. **Lazy scan 10k** — gate ≤45ms but ADR-009 vision 15ms; largest kernel perf debt
2. **KV write circuit gap** — bare `Engine::Put` bypasses `RarMonitor::AllowsWrite()` unless SQL layer intercepts
3. **Gate drift** — P16-demo / CIDR docs active while core merge path unclear

## Decision

### Scope (in)

| Area | Focus |
|------|-------|
| `cpp/` | Lazy scan, ReadPages batch IO, no-fallback contract |
| `tools/ebtree_audit/` | WriteGuard hook, chain-drop policy, KV/SQL parity |
| Gates | **P17-deep-core** = PR merge gate |
| Perf baseline | `Docs/archive/perf/kernel-rar-baseline.md` (not CIDR eval-results) |

### Non-goals (out)

- CIDR 2027 submission, demo maintenance, eval-results refresh cadence
- SQL breadth (P11/P12 frozen)
- MVCC, distributed replication, formal verification

### North-star SLO

| Metric | Target | Gate (local Release) | Gate (EBTEST_CI / GHA) |
|--------|--------|----------------------|-------------------------|
| Lazy scan 10k P50 | 15ms (long-term) | **≤40ms local** / ≤60ms CI | ≤60ms |
| kBalanced write | 100k+ TPS | `KBalancedWrite100kConcurrentBudget` | ≥85k |
| RAR MONITOR write overhead | ≥0.99× raw | `KBalancedWrite100kWithRarMonitor` | same |
| Standard compress SKU | ≤1.10× lazy scan vs raw | `StandardLazyScan10kBudget` | relaxed ratio |
| KV write circuit | Put blocked on violation | `KvRarMonitorWriteCircuitBlocksPut` | — |

### Engine WriteGuard

Mirror `CheckpointObserver` — kernel stays free of `ebtree_audit` link:

```cpp
using WriteGuard = std::function<Status()>;
void Engine::SetWriteGuard(WriteGuard guard);
```

`RarMonitor::Install` registers guard; `Stop` clears it. `Put`/`Delete` invoke guard before shard dispatch.

### Chain drop policy

| SKU | `reject_on_chain_drop` | Behavior |
|-----|------------------------|----------|
| Standard MONITOR | false | increment `rar_chain_drop_total`, WARN log |
| Compliance | true | open write circuit on drop increment |

### Gate P17-deep-core

Composite of:

- `P9-nofallback-hard` — tier contract + forbidden stats
- `P14-rar-product` — MONITOR smoke + RAR chain perf
- `P14-standard-perf` — Standard SKU write/lazy scan budgets

PR and main CI run P17 after P4-complete.

### CIDR deferred

Trigger to resume CIDR track (future project):

- Lazy scan local gate ≤25ms sustained
- KV WriteGuard closed + design partner interest

Artifacts remain in `Docs/papers/carl/` (read-only reference).

### Phase 3 lazy scan (Q3–Q4)

Per [lazy-scan-track](../archive/perf/lazy-scan-track-2026-07-01.md):

1. `PageFile::ReadPages` — coalesce contiguous page runs (single IO)
2. `DataFile::ReadRecordsAtOffsets` — sorted batch with shared read windows
3. `.didx` sidecar — already persisted at checkpoint; ensure reopen uses sidecar (existing `BuildLsnIndex`)

Gate tighten 45ms → **40ms** local (Phase 5, ADR-044 ACCEPT 2026-07-02); CI remains 60ms.

### Backlog (H1 2027)

- Multishard RAR chain semantics + tests
- powerfail × RAR 500-trial hook (ADR-034 optional)
- Histogram summary type: implement or formal abandon (ADR-008)
- KV op_log C API for Layer-3 contract

## Consequences

- New gate P17-deep-core in CI (PR required green)
- ADR-042 archived as historical; papers/carl marked DEFERRED
- P15/P16-carl-eval retained as manual regression, not PR default path
- P16-demo-e2e retained but excluded from P17

## References

- ADR-009, ADR-018, ADR-034, ADR-036, ADR-038, ADR-040, ADR-042
- [lazy-scan-track-2026-07-01.md](../archive/perf/lazy-scan-track-2026-07-01.md)
