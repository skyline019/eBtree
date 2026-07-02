# Kernel + RAR + LSV Performance Baseline

**Date**: 2026-07-02  
**P19 gate (local Release, 2026-07-02)**: ~280–480 s strict (powerfail oracle + 并发); MVP ~73 s. EXIT=0.

```powershell
.\scripts\perf\refresh_kernel_baseline.ps1
.\scripts\test\run_gate_streak.ps1 -Gates P19-lsv -Trials 10 -MinPass 9
```

## Lazy scan 10k (ProductionDefaults, post-reopen)

| Metric | Observed | Gate |
|--------|----------|------|
| raw lazy scan P50 | 38 ms (carl_eval harness) | <=40ms local / <=60ms CI |

## RAR MONITOR write (100k Put)

| Metric | Observed | Gate |
|--------|-------|------|
| CARL/no-CARL ratio | ratio=1.015 (carl_eval harness) | P16-carl-eval >=0.99 |

## LSV snapshot read (P19-lsv)

| Metric | Observed | Gate |
|--------|----------|------|
| Snapshot get hot P50 (1000x) | local Release | <=1.05 ms / <=5 ms CI |
| Snapshot get after update P50 | local Release | <=2 ms / <=5 ms CI |
| Snapshot scan 10k | local Release | <=75 ms |
| Snapshot write overhead (5000 Put, balanced) | ~1100 TPS local | >=700 TPS |
| P19-lsv MVP streak | 10/10 — `streak-20260702-153149` (~70 s/trial) | MinPass 9/10 |
| **P19-lsv strict streak** | **10/10 ACCEPT** — `streak-20260702-200714` (282–475 s/trial) | MinPass 9/10 |
| 归档 | [`Docs/archive/lsv/lsv-strict-implementation-2026-07-02.md`](../lsv/lsv-strict-implementation-2026-07-02.md) | — |

## Verification

```powershell
.\scripts\test\run_tests.ps1 -Gate P19-lsv -Config Release
.\scripts\test\run_tests.ps1 -Gate P17-deep-core -Config Release
```

## Notes

- Hardware-bound (local NVMe). GHA uses `EBTEST_CI` relaxed lazy scan budget.
- CARL frozen eval: [eval-results.md](../../papers/carl/eval-results.md) (CIDR deferred).
