# Lazy Scan 性能跟踪（ADR-018）

**日期**: 2026-07-01  
**目标**: 10k scan P50 向 **15ms** 靠拢（M8–M18）

## 当前基线（Release smoke）

| 测试 | 当前 budget | 目标 |
|------|-------------|------|
| `EbPipelinePerf.LazyScan10kBudget` | ≤40ms local / ≤60ms CI | 15ms P50 |
| `EbPipelinePerf.StandardLazyScan10kBudget` | ≤ raw×1.10+2 | 同上 |
| `EbPipelinePerf.KSyncScan10kSmokeBudget` (lazy) | ≤40ms | 15ms |

## 策略（ADR-018）

1. Committed scan direct path（memtable empty post-reopen）
2. `.didx` sidecar + batch `ReadRecordsAtOffsets`
3. `ScanLeafChain` + `ReadPages` 合并相邻页读

## Gate 策略

- 短期：local ≤40ms / CI ≤60ms（ADR-045 Phase 5，2026-07-02）
- **Q4 2026 及以后**：每季度 local -5ms 直至 15ms 或修订 ADR-009

## 跟踪

- 性能矩阵：ADR-009 perf-baseline
- 代码：`ResolveScanValues`, `ScanCommittedDirect`, `ScanLeafChain`
