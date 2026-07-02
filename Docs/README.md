# eB-Tree 文档索引

## 架构决策（ADR）

[`adr/`](adr/) — 001–044 架构决策记录。**活跃路线图**：[ADR-043 深耕](adr/043-kernel-rar-deep-cultivation.md) + [ADR-044 稳定性 Sprint](adr/044-phase4-stability-sprint.md)

| ADR | 主题 |
|-----|------|
| **[047](adr/047-lsv-tso-commit-occ.md)** | LSV-TSO：Commit OCC + Txn-WAL + TSL-3（P20 gate） |
| **[046](adr/046-lsn-native-snapshot-read-view.md)** | LSV 读快照隔离（P19-lsv gate、VCS pager） |
| **[045](adr/045-phase5-lazy-scan.md)** | Phase 5 lazy scan（40ms gate、DataFile 读窗口） |
| **[044](adr/044-phase4-stability-sprint.md)** | Phase 4 稳定性（P18 gate、WriteGuard 硬化） |
| **[043](adr/043-kernel-rar-deep-cultivation.md)** | 内核 + RAR 深耕（CIDR 搁置） |
| [009](adr/009-perf-baseline.md) | 性能基线与 gate |
| [017](adr/017-lazy-committed-load.md) | Lazy committed load |
| [034](adr/034-kernel-nofallback-contract-v2.md) | No-fallback 契约 v2 |
| [038](adr/038-rar-kernel-full-auditability.md) | RAR 动态链 / MONITOR |
| [040](adr/040-rar-standard-sku-defaults.md) | RAR Standard SKU 默认 MONITOR |
| [041](adr/041-carl-checkpoint-attestation-recovery-log.md) | CARL 抽象（工程层） |
| [042](adr/042-phase2-evidence-sprint.md) | Phase 2 evidence sprint（历史） |
| [039](adr/039-compressed-normal-product-default.md) | 压缩产品默认 SKU |

## 产品

| 路径 | 内容 |
|------|------|
| [`product/standard-sku-boundary.md`](product/standard-sku-boundary.md) | Standard/Enterprise SKU 边界 |
| [`product/vertical-playbooks.md`](product/vertical-playbooks.md) | 垂直 demo（**后续项目**，非活跃维护） |

## 论文（后续项目 / DEFERRED）

| 路径 | 内容 |
|------|------|
| [`papers/carl/`](papers/carl/) | CARL 论文素材 — **CIDR 搁置**，见 [cidr-track-decision.md](papers/carl/cidr-track-decision.md) |

## 性能跟踪

| 路径 | 内容 |
|------|------|
| [`archive/perf/kernel-rar-baseline.md`](archive/perf/kernel-rar-baseline.md) | 内核 + RAR perf 基线（活跃） |
| [`archive/perf/lazy-scan-track-2026-07-01.md`](archive/perf/lazy-scan-track-2026-07-01.md) | Lazy scan 15ms 目标跟踪 |

## 契约清单

| 路径 | 内容 |
|------|------|
| [`invariants/INVARIANT_MANIFEST.yaml`](invariants/INVARIANT_MANIFEST.yaml) | 内核不变量 |
| [`syncs/SYNC_MANIFEST.yaml`](syncs/SYNC_MANIFEST.yaml) | 同步/禁止行为规则 |

## 报告

| 路径 | 内容 |
|------|------|
| [`reports/ebtree-project-evaluation-report.md`](reports/ebtree-project-evaluation-report.md) | 2026-07-01 全量项目评估 |

## 归档

| 路径 | 内容 |
|------|------|
| [`archive/rar/rar-kernel-implementation-2026-07-01.md`](archive/rar/rar-kernel-implementation-2026-07-01.md) | ADR-038 内核联动 + 动态链 |
| [`archive/rar/rar-product-implementation-2026-07-01.md`](archive/rar/rar-product-implementation-2026-07-01.md) | ADR-040 Standard SKU 产品化 |
| [`archive/lsv/lsv-strict-implementation-2026-07-02.md`](archive/lsv/lsv-strict-implementation-2026-07-02.md) | ADR-046 LSV strict 验收（10/10 streak） |
| [`archive/lsv/lsv-tso-implementation-2026-07-02.md`](archive/lsv/lsv-tso-implementation-2026-07-02.md) | ADR-047 LSV-TSO（P20 gate） |
| [`archive/perf/`](archive/perf/) | 性能基线按日归档 |
| [`archive/planning/`](archive/planning/) | 原始规划与设计笔记 |
| [`archive/test-logs/`](archive/test-logs/) | 历史 gate / 全量测试日志 |
| [`archive/thirdbackup-sql_parse/`](archive/thirdbackup-sql_parse/) | 旧 SQL parser 源码备份 |

## 代码与测试

| 路径 | 内容 |
|------|------|
| `cpp/` | C++ 存储内核 |
| `sql/` | 原生 SQL 层 |
| `test/` | gtest 套件与 matrix |
| `bench/` | 性能 bench 可执行文件源码 |
| `scripts/test/` | gate 脚本（`run_tests.ps1`） |
| `scripts/perf/` | 内核 perf 基线刷新 |

**Merge gate**: `P4-complete` + **`P17-deep-core`** + **`P18-stability`**（PR 必绿）
