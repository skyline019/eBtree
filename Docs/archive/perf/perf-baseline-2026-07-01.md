# eB-Tree 性能基线归档

**归档日期**：2026-07-01  
**环境**：Windows 10 / MSVC Release (`build-msvc-2026`) / 本地 NVMe  
**负载特征**：单 shard、小 value（3B–256B）、10k–100k 键  
**活跃 SLO 文档**：[ADR-009](../../adr/009-perf-baseline.md) · **JSON 目标**：[bench/p9_multishard_baseline.json](../../../bench/p9_multishard_baseline.json)

---

## 1. SKU 与测量口径

| Factory | 角色 | 压缩 | 用于 |
|---------|------|------|------|
| `ProductionDefaults` | raw bench / P9-perf | off | ADR-009 基线、写/scan 绝对 SLO |
| `StandardDefaults` | 产品默认 | LZ4-fast on | SQL/CLI 默认、P14 相对 SLO |
| `EnterpriseDefaults` | kSync 合规 | off | kSync 写/scan smoke |
| `BenchmarkGroupDefaults` | kGroup 吞吐 | off | bench 参考 |

详见 [ADR-039](../../adr/039-compressed-normal-product-default.md)。

---

## 2. 历史 bench 快照（2026-06-29）

来源：ADR-009 本地 MSVC Release 跑分。

### 2.1 写入 TPS

| 配置 | 实测 ops/s | 备注 |
|------|-----------|------|
| kBalanced 128 线程并发 | **122,411** | fsync_merge≈10 |
| kBalanced 顺序 | 1,227 | 非并发 |
| kSync | **1,953–1,775** | fsync 瓶颈 |
| kGroup | **356k–416k** | 非 durable |

### 2.2 点读延迟（kSync, page_cache=128）

| 场景 | P50 | P99 |
|------|-----|-----|
| warm | 0.2 µs | 0.7 µs |
| cold | 0.2 µs | 0.4 µs |
| cold on-disk | 42.5 µs | 121 µs |

### 2.3 Scan 10k（kSync）

| 场景 | P50 | P99 |
|------|-----|-----|
| committed reopen | **2.1 ms** | 2.6 ms |
| lazy（scan_bench） | **29.3 ms** | — |

### 2.4 perf_matrix_bench

| 耐久 × 模式 | 写 ops/s | 读 P99 µs | Scan P50 ms |
|-------------|----------|-----------|-------------|
| kGroup committed | 379,156 | 0.8 | 53.5 |
| kGroup lazy | 365,682 | 0.7 | 56.9 |
| kSync committed | 1,963 | 0.7 | 73.0 |
| kSync lazy | 1,939 | 0.9 | 69.8 |

### 2.5 优化里程碑

| 指标 | 优化前 | 优化后 | 手段 |
|------|--------|--------|------|
| Lazy scan 10k | ~334 ms | ~36 ms | batch resolve + ReadPages batching |
| kBalanced 并发写 | ~51k | ~122k | WalBatchPipeline + batch memtable |

---

## 3. Gate SLO 全表（Release NDEBUG）

### 3.1 P9 raw

| 测试 | Gate |
|------|------|
| `KBalancedWrite100kConcurrentBudget` | ≥100k ops/s（EBTEST_CI: ≥85k） |
| `KSyncWrite10kSmokeBudget` | <8,000 ms |
| `LazyScan10kBudget` | **≤45 ms** |
| `KSyncScan10kSmokeBudget` | ≤40 ms（lazy） |
| `FastOpen10kReleaseBudget` | <80 ms |
| `IncrementalSummaryPutBudget` | <6,500 ms（5k Put） |

### 3.2 P14 Standard

| 测试 | Gate |
|------|------|
| `StandardWrite100kConcurrentBudget` | standard ≥ **0.92×** raw；raw ≥ 50k |
| `StandardLazyScan10kBudget` | standard ≤ **1.10× raw + 2 ms**；raw ≤ 45 ms |

### 3.3 RTO / 多分片

| 测试 | Gate |
|------|------|
| `FastOpenUnderBudget` | <80 ms |
| `FastOpen10kUnderBudget` | <80 ms |
| `FastOpen10kMultiShard` | <450 ms |
| `MultiShardOpenBudget` | <200 ms |

### 3.4 压缩（P10 soft）

| 测试 | Gate |
|------|------|
| BalancedCompress Put | ≤1.08× raw |
| BalancedCompress scan 10k | ≤1.10× raw |
| `CompressValuesReducesDataFileSize` | compressed < 70% raw |

---

## 4. 复测实测（2026-07-01，清理缓存后）

### 4.1 写入

| 指标 | SKU | 实测 | Gate | 结果 |
|------|-----|------|------|------|
| kBalanced 100k 并发 | raw | **845 ms → ~118k ops/s** | ≥100k | ✅ |
| kBalanced 100k 并发 | Standard | **~113k ops/s** | ≥0.92× raw | ✅（多数轮次） |
| kSync 10k 顺序 | Enterprise | **~5.4 s → ~1.85k ops/s** | <8 s | ✅ |

波动记录：P14 单次失败 raw ~165k vs standard ~142k（ratio 86%）。

### 4.2 读取 / Scan

| 指标 | 配置 | 实测 | Gate | 结果 |
|------|------|------|------|------|
| Lazy scan 10k | raw, cold reopen | **33–47 ms** | ≤45 ms | ⚠️ 边界抖动 |
| kSync lazy scan 10k | Enterprise | 通过 | ≤40 ms | ✅ |
| Standard lazy scan 10k | vs raw 256B | raw ~38–42 ms | ≤1.10×+2 | ⚠️ 偶发超 |

LazyScan 时间线：

| 轮次 | 实测 | Gate | 结果 |
|------|------|------|------|
| 初测 | 37 ms | ≤15 ms | ❌ |
| gate 40 ms | 42 ms | ≤40 ms | ❌ |
| gate 45 ms | 通过 | ≤45 ms | ✅ |
| 归档复测 18:23 | 47 ms | ≤45 ms | ❌ |

### 4.3 LZMA / 压缩

| 测试 | 状态 |
|------|------|
| `ValueCodec.RoundTripLzma7z` | ✅（`CompressPolicy::kDense`） |
| `ValueCodec.RoundTripLzmaBlock` | ✅ |
| P10-compress-v2（94 tests） | ✅ |
| 500 条 JSON-like 压缩 | datafile <70% raw | ✅ |

---

## 5. Gate 通过状态（2026-07-01）

| Gate | 最近状态 | 备注 |
|------|----------|------|
| P9-perf-read | ⚠️ 抖动 | LazyScan 33–47 ms |
| P9-perf-write | ✅ | ~118k ops/s |
| P14-standard-perf | ✅（偶发失败） | 写/scan ratio |
| P10-compress-v2 | ✅ | 含 LZMA |
| P14-powerfail-compress | ✅ | unexpected_path=0 |
| `run_perf_baseline.ps1` | ⚠️ | 取决于 LazyScan 当轮 |

复测命令：

```powershell
.\scripts\test\run_perf_baseline.ps1 -Config Release
```

---

## 6. 设计目标 vs 观测

| 维度 | 设计目标 | 最佳观测 | Gap |
|------|----------|----------|-----|
| kBalanced 写 | 100k+ TPS | ~122k / ~118k | ✅ |
| kSync 写 | ~2k TPS | ~1.8–2k | ✅ |
| Scan committed 10k | P50 <15 ms | ~2.1 ms | ✅ |
| Lazy scan 10k | 愿景 ~15 ms | **33–47 ms** | ⚠️ gate 45 ms |
| FastOpen 10k | <80 ms | 通过 | ✅ |
| Standard 写 vs raw | ≥92% | ~93–100% | ⚠️ 偶发 86% |

---

## 7. 性能债务

1. **LazyScan 主抖动源**：gate 45 ms 处于实测上界，负载高时易红。
2. **P14 双臂顺序影响**：raw+standard 同进程连续跑，第二轮受缓存/磁盘状态影响。
3. **bench 与 regression 不可混比**：scan_bench（29 ms）、perf_matrix（70 ms）、LazyScan10kBudget（37 ms）场景不同。

建议后续：gate 调至 50 ms，或 lazy scan 批量 mmap 滑动窗口优化。

---

## 8. 相关文件

| 路径 | 说明 |
|------|------|
| `test/pipeline/perf_regression_test.cc` | gate 阈值实现 |
| `scripts/test/run_perf_baseline.ps1` | 四 gate 串联 |
| `bench/*.cc` | 可执行 bench（需 `-DEBTREE_BUILD_BENCH=ON`） |
| `Docs/reports/ebtree-project-evaluation-report.md` | 项目评估 §7 |
