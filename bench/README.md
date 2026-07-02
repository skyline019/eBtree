# eB-Tree Benchmark

构建：CMake `-DEBTREE_BUILD_BENCH=ON`。

| 可执行文件 | 说明 |
|-----------|------|
| `ebtree_write_bench` | kGroup / kSync / kBalanced 写吞吐 |
| `ebtree_read_bench` | warm/cold/cold_ondisk 点读延迟 |
| `ebtree_scan_bench` | 10k range scan（committed + lazy） |
| `ebtree_perf_matrix_bench` | 写/读/scan × 耐久 × lazy 矩阵 |
| `ebtree_multishard_write_bench` | 多分片写 |
| `ebtree_multishard_scan_bench` | 多分片 scan merge |

## 目标值

[`p9_multishard_baseline.json`](p9_multishard_baseline.json) — JSON 形式 SLO 目标。

## 归档与 gate

- 活跃 SLO：[Docs/adr/009-perf-baseline.md](../Docs/adr/009-perf-baseline.md)
- 历史快照：[Docs/archive/perf/](../Docs/archive/perf/)
- CI gate：`scripts/test/run_perf_baseline.ps1`（P9-perf + P14-standard-perf + P10-compress-v2）
