# CARL Eval Results

**Date**: 2026-07-01  
**Machine**: local Windows / NVMe (Release build)  
**Reproduce**:

```powershell
cmake --build build-msvc-2026 --config Release --target ebtree_carl_eval
.\scripts\papers\refresh_eval_results.ps1
# or: .\build-msvc-2026\bench\Release\ebtree_carl_eval.exe .test-runs\carl_eval_manual
```

Generated: Release bench on local NVMe (single run). Gate `P16-carl-eval` enforces CARL/no-CARL ratio ≥ 0.99 (gtest isolated temp dirs; bench numbers may vary ±5%).

## Workload-A-equivalent write (100k Put, kBalanced)

| Config | 100k Put TPS | Elapsed ms | Notes |
|--------|-------------|------------|-------|
| no-CARL | 155913 | 642 | attestation off |
| CARL MONITOR | 153286 | 653 | ratio=0.983 |

| Metric | Value |
|--------|-------|
| chain verify 1000 entries | 27 ms |
| anchor publish | 3 ms |
| lazy scan 10k rows | see [kernel-rar-baseline.md](../../archive/perf/kernel-rar-baseline.md) |
| tamper detect | CarlAnchorTamperDetect gate (P15) |
| write ratio gate | P16-carl-eval >= 0.99 |
