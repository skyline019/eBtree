# eB-Tree 数据库项目全量客观评估报告

> **范围**：当前代码库（`e:\DBProject`）  
> **排除**：CI 流水线、跨平台移植  
> **方法**：代码结构、ADR/清单、测试矩阵、引擎/SQL 实现、本地 gate 结果（P4/P17/P18 已通过）  
> **日期**：2026-07-01（ADR-043/044 深耕 + 稳定性 Sprint 口径）

---

## 1. 执行摘要

**eB-Tree（v0.1.0）** 是一个 **C++17 嵌入式 KV 存储引擎 + 原生 SQL 层** 的组合项目。核心自研代码约 **29k LOC**（`cpp` ~7.6k、`sql` ~7.6k、测试 ~6.3k、工具 ~6.5k），工程上采用 **ADR 驱动 + 清单可执行化 + 阶段门禁（P0–P18）** 的开发模式。

**活跃路线图**（2026-07-01）：[ADR-043 内核 × RAR 深耕](../adr/043-kernel-rar-deep-cultivation.md) + [ADR-044 Phase 4 稳定性 Sprint](../adr/044-phase4-stability-sprint.md)。PR merge gate = **P4-complete → P17-deep-core → P18-stability**。CIDR / demo sprint **DEFERRED**。

| 维度 | 评级 | 一句话 |
|------|------|--------|
| 存储内核 | **A− / 准生产** | 三档耐久、WAL/B-tree/恢复/FastOpen 完整；WriteGuard + no-fallback 门禁 |
| SQL 层 | **A− / Subset B** | 语义内核+索引 eq runtime+LIKE/CAST/CHECK/SAVEPOINT；P11/P12/P13 绿；curated ≥90% |
| 测试体系 | **A** | 331+ gtest、1410 sqllogic、48 engine matrix、P18 WriteGuard 稳定性 |
| 文档 | **A−** | 44 篇 ADR + 3 份 YAML 清单 + [Docs/README.md](../README.md) 索引 |
| 性能 | **A（本地 NVMe）** | kBalanced ~120k TPS；lazy scan gate ≤45ms local（Phase 5 目标 40ms） |
| 整体成熟度 | **晚期 Beta → 早期生产（内核）** | 存储强、RAR MONITOR 硬化中、SQL 子集可用 |

**结论**：这不是「玩具 DB」，而是 **以存储契约为核心、SQL 为上层产品的嵌入式引擎**；当前最可信的是 **kBalanced/kSync 单分片 OLTP + RAR WriteGuard**，SQL 是 **有门禁的 SQLite 语义子集**，不是通用 SQL 替代品。

---

## 2. 项目定位与边界

### 2.1 是什么

```
应用层 (C API / Database SQL)
        ↓
SQL 层 (~7.6k LOC): Parser → Eval(3VL) → Exec(V1/V2/V3) → Catalog
        ↓
存储内核 (~7.6k LOC): Engine → WAL → MemTable → B-tree → Recovery
        ↓
可验证性: RAR attestation + SQL op_log
```

- **产品 SKU（耐久）**：Production（kBalanced）、Enterprise（kSync）、Benchmark（kGroup）
- **SQL 定位**：ADR-032 定义的 **Subset A** — 官方 L1 **800 case 100%**、语义 oracle **95 case 100%**、curated **515 case ≥90%**
- **明确不在范围**：完整 SQLite（~12 万 case）、ATTACH、虚拟表、MVCC、完整 CHECK/FK/LIKE 等

### 2.2 不是什么

- 不是分布式数据库（多分片是路由聚合，无共识/复制）
- 不是分析型或 HTAP 引擎
- 不是「开箱即用」服务端（无网络协议、连接池、权限系统执行层）

---

## 3. 存储内核深度评估

### 3.1 架构优势（客观强项）

1. **契约驱动设计**  
   31 项 invariant、14 条 forbidden 行为（如无 fallback read、无 stub handler、读路径不触发 WAL 全量 replay）均有 **manifest → 具体 gtest** 映射，在同类嵌入式引擎中少见。

2. **三档耐久语义清晰**  
   - **kBalanced**：`WalBatchPipeline` + 批量 memtable apply，Put 返回时 RPO=0（`flushed_lsn ≥ put_lsn`）  
   - **kSync**：`WalFsyncCoordinator`，每次 Put fsync，适合合规档  
   - **kGroup**：append-only + `GroupCommit()`，吞吐最高  

3. **恢复与 RTO**  
   FastOpen 默认跳过 WAL 全量 replay；单 key lazy restore（WAL key index）；T-Log / data_lsn 降级链；powerfail fuzz + `CommittedOracle` 分层验证。

4. **读路径分层可观测**  
   `read_tier` 显式区分 MemTable → Committed → BTreeDisk → DataFile → WalSingleKey → CommittedDirectScan；`unexpected_path_total == 0` 为硬约束。

5. **On-disk B-tree 成熟度**  
   Lazy root、delta overlay、histogram summary、checkpoint 从 committed 快照 persist（禁止 delta-only persist）、corrupt root self-heal。

### 3.2 风险与局限

| 风险 | 严重度 | 说明 |
|------|--------|------|
| 性能强依赖本地 NVMe | 中 | ADR-009 基线为单机快照，非可移植 SLO |
| Lazy scan 延迟 | 中 | committed-direct ~2ms；lazy/on-disk 仍 ~29–73ms，未达 15ms 目标 |
| kBalanced 离线 attestation | 中 | 无 SQL op_log 时 Layer-3 合约无法从 WAL 单独推断 |
| kAsync 第四档 | 低 | enum 存在但未产品化，测试覆盖薄 |
| 多分片扩展性 | 中 | 256 分片 smoke 有，Scan 并行+merge，无 multishard perf SLO |
| 压缩默认关闭 | 低 | LZMA 四层已实现，生产默认 off，perf 影响未完全 baseline |

### 3.3 内核成熟度

**Late Beta → Early Production**

单分片、kBalanced/kSync 路径：**可视为准生产内核**。多分片、压缩、kAsync：**Beta / 可选特性**。

---

## 4. SQL 层深度评估

### 4.1 架构

- **解析**：Registry + NativeParser（词法 → 分类 → DML/DDL 规则 → expr/select/advanced）
- **执行**：UnifiedExecutor → UPDATE 走 DmlExecutor，其余 SqlExecutorV3（rich SELECT）+ V2（索引/DDL 回退）+ V1（最小 OLTP）
- **语义**：`SqlValue` / `TruthValue` / `ExprEval` / `CompareSqlValues`（亲和性 + 3VL），ADR-032 统一方向正确

### 4.2 能力矩阵（摘要）

| 能力 | 状态 | 备注 |
|------|------|------|
| DDL（表/索引/视图/触发器） | ✅ 子集 | ALTER 仅 ADD COLUMN；REINDEX 校验后 no-op |
| DML（INSERT/UPDATE/DELETE） | ✅ | NOT NULL 运行时；CHECK/FK 未执行 |
| SELECT（投影/WHERE/JOIN/GROUP/ORDER/LIMIT） | ✅ | V3 rich path |
| 子查询（IN/EXISTS/相关） | ✅ 受限 | 深度 ≤3；单线程 Scan |
| 索引 | ⚠️ 演进中 | V3 range scan + 复合 leading eq（ADR-033 Phase 1）；复杂计划仍常全表扫 |
| 事务 | ⚠️ 会话级 | BEGIN/COMMIT/ROLLBACK；SAVEPOINT 不完整 |
| CTE / SET OP / WINDOW | ✅ / ✅ / ⚠️ | WINDOW 仅 ROW_NUMBER/RANK/DENSE_RANK |
| UPSERT / INSERT…SELECT | ⚠️ | 语义简化 |
| SHOW/SET/GRANT | ❌ | 仅 parse，不执行 |
| 与 SQLite 全量兼容 | ❌ | L1 800 / 上游 12 万 ≈ 0.66% 采样 |

### 4.3 典型缺陷示例（已修复）

**`IsIndexEncodedKey` 误判**：行键 `1:i`（pk=`'i'`）被当成索引键过滤，导致 `index_scan_k10` 等 case 0 行 — 属于 **键编码启发式 bug**，已修复并加 unit 回归（`test/unit/catalog_key_encoding_test.cc`）。说明 SQL 路径对 catalog 编码边界仍需谨慎。

### 4.4 SQL 成熟度

**B+ / 可用子集引擎**

- **可信场景**：单表/简单 JOIN、3VL、IN/BETWEEN、聚合、UPDATE SET、索引 eq/range 点查与短 range（V2/V3 index path）  
- **不可假设**：完整 SQLite 语义、复合索引非 leading 列、并发 MVCC、完整事务隔离  

---

## 5. 测试与质量保障

### 5.1 量化

| 类别 | 规模 |
|------|------|
| GTest 注册 | **331+** |
| 测试 DLL 套件 | **9**（unit/failure/pipeline/matrix/sql/audit…） |
| Engine matrix | **48** 声明式 case |
| SQL matrix | **353** case |
| Sqllogic 活跃语料 | **1410** case（800 official + 515 curated + 95 semantic + 10 basic） |
| Audit/RAR | **26** gtest |
| Benchmark | **6** 可执行文件 |
| 核心 TODO/FIXME | **0** |

### 5.2 门禁层次（本地已验证）

| Gate | 含义 | 状态 |
|------|------|------|
| P4-complete | 引擎+SQL+audit 全功能（perf/大 sqllogic 已过滤） | ✅ 通过 |
| P12-semantic | 95 语义 case 100% | ✅ 通过 |
| P11-real-sql | Official 800 **100%** | ✅ 通过 |
| P15-carl-complete | CARL anchor + chain-verify | ✅ 通过 |
| P16-carl-eval | CARL/no-CARL write ratio ≥0.99 | ✅ 通过 |
| P16-demo-e2e | 三垂直 demo flow | ✅ 通过 |

### 5.3 测试体系评价

**项目最大资产之一**

- **金字塔完整**：unit invariant → failure/chaos → matrix → pipeline → sqllogic → semantic oracle → RAR attestation  
- **禁止项可执行化**：fallback read、stub handler 等不是文档口号，而是 gate 会红的规则  
- **弱点**：sqllogic 对上游 **高度采样**（800/120372）；perf 测试 **hardware-bound**，不宜作通用门禁  

---

## 6. 文档与工程纪律

### 6.1 优势

- **32 篇 ADR**（001–032），覆盖 invariant、pipeline、recovery、perf、SQL、RAR、semantic subset  
- **INVARIANT_MANIFEST**（30 条）、**SYNC_MANIFEST**（19 条）、**TEST_MANIFEST**（阶段 gate + forbidden）  
- 每条 invariant **id → test → adr** 可追溯  
- manifest 自身有 **consistency gtest**（非装饰性文档）

### 6.2 不足

- 无顶层 README / Docs 索引 → 已补 [`Docs/README.md`](../README.md)  
- `Docs/archive/thirdbackup-sql_parse/` 混放大量旧 parser 源码，边界模糊  
- 早期 ADR 与近期 spec 级 ADR 格式不一致  

**文档评级：B+（traceability A−）**

---

## 7. 性能（本地 Release / NVMe 语境）

来源 ADR-009 与 `perf_regression_test` / bench（归档：[perf-baseline-2026-07-01](../archive/perf/perf-baseline-2026-07-01.md)）：

| 指标 | 目标 | 观测 | 评价 |
|------|------|------|------|
| kBalanced 并发写 | 100k+ TPS | ~122k（128 线程，merge≈10） | ✅ 达标 |
| kSync 写 | ~2k TPS | ~1.8–2k | ✅ fsync 上限 |
| kGroup 写 | 50k+ | ~356–416k | ✅ 超标 |
| 点读 P99 | <1ms | warm 0.7µs；cold disk 121µs | ✅ |
| Scan 10k P50（committed reopen） | <15ms | ~2.1ms（scan_bench） | ✅ |
| Lazy scan 10k | 趋近 15ms | ~33–47ms（gate ≤45ms） | ⚠️ gate 通过，愿景未达 |
| FastOpen 10k | <80ms | 回归测试通过 | ✅ |

**客观结论**：写路径与 FastOpen **在文档化硬件上已验证**；lazy/on-disk 读扫描仍是 **已知性能债务**。

---

## 8. 安全、审计与可验证性

### 8.1 RAR → CARL（Phase 1–2）

- **物理层 / 恢复层 / 合约层** 分级 attestation（ADR-022/023/026）  
- **Standard SKU 默认 `MONITOR` + async chain**（ADR-040）：Open 无同步 `BuildRar`；`PRAGMA rar_status` / C API 可观测  
- **CARL 抽象**（ADR-041）：external STH anchor（`.sth.jsonl`）、Merkle batch、`chain-verify --require-anchor`  
- **Phase 2**（ADR-042）：anchor 签名校验、worker auto-publish、`P16-carl-eval` / `P16-demo-e2e`、三垂直 e2e demo  
- Open 时可 `AttestationMode` 拒绝 badwal / 超阈值 missing keys（`REQUIRE_PASS` opt-in）  
- **35+ audit gtest** + powerfail fuzz 与 oracle 等价性  
- `EBTREE_RAR_KEY` 自动 chain/anchor 签名（`EBTREE_RAR_SIGNING` 构建时）

### 8.2 安全边界（诚实评估）

- **无** SQL 注入防护层讨论（嵌入式单进程假设）  
- **无** 网络攻击面（无 server）  
- **无** 加密 at-rest（压缩≠加密）  
- **权限**：GRANT 仅 parse，无 RBAC 执行  

**适合**：可信本地/嵌入式、需 **可证明恢复** 的场景；**不适合**：多租户、公网暴露、合规加密存储（未内置）。

---

## 9. 技术债务汇总

### 9.1 按优先级

| 优先级 | 债务项 | 影响 |
|--------|--------|------|
| **P0** | 索引模型过浅（单列 eq；V3 少走索引） | 大表查询性能、与 planner 预期 gap |
| **P0** | 无 MVCC / 会话级事务不完整 | 并发写+读语义受限 |
| **P1** | 执行器三代并存（V1/V2/V3） | 维护成本、行为不一致风险 |
| **P1** | Legacy RowMap 字符串边界（ADR-032） | 类型/NULL 语义 corner case |
| **P1** | Lazy scan 性能 gap | 冷数据 range query SLA |
| **P2** | Sqllogic 0.66% 采样 | 未知 upstream 失败面 |
| **P2** | kAsync / 压缩 / 多分片 perf 未产品化 | 特性 flag 误用风险 |
| **P2** | ADR-008 延期项（histogram summary type 等） | 长期 roadmap 不确定性 |

### 9.2 积极信号

- 核心 **零 TODO/FIXME**  
- 「fallback」作为 **反模式** 被测试禁止，而非隐藏代码路径  
- 债务多 **写在 ADR**，而非散落 hack  

---

## 10. 多维度评分卡

| 维度 | 分数 (1–10) | 说明 |
|------|:-----------:|------|
| 架构清晰度 | **9** | 分层明确，ADR 与代码大体一致 |
| 存储正确性 | **9** | invariant/forbidden/chaos 覆盖 exceptional |
| 存储性能 | **8** | 写/FastOpen 强；lazy scan 扣分 |
| SQL 功能广度 | **5** | 相对 SQLite 全量约 5–15% 量级能力 |
| SQL 语义深度（子集内） | **8** | 3VL/IN/BETWEEN/aggregate 有 strict gate |
| 测试深度 | **9** | 矩阵+sqllogic+semantic+audit 罕见完整 |
| 文档/可追溯 | **8** | manifest 优秀，缺 index/README |
| API/生态 | **4** | 薄 C API，无 server/驱动/工具链 |
| 运维就绪 | **5** | attestation 强；无 backup/replication/monitoring 产品化 |
| 代码卫生 | **8** | 核心干净；vendor 树臃肿但未全量编译 |

**加权整体（嵌入式 OLTP 引擎视角）：8.2 / 10**  
**加权整体（通用 SQL 数据库视角）：5.5 / 10**

---

## 11. ADR-043/044 后续路线（2026 H2）

| Phase | 内容 | 状态 |
|-------|------|------|
| **Phase 4** | no_fallback filter 修复、P18-stability、WriteGuard 硬化 | **已完成**（10/10 ACCEPT） |
| **Phase 5** | DataFile 读窗口、ResolveScanValues、lazy scan gate 40ms local | **已完成**（perf 绿，未重跑 streak） |
| **Phase 6** | Multishard RAR、500-trial powerfail×MONITOR、KV op_log C API | H1 2027 backlog |

**优先级**：`稳定性回归 > perf gate 收紧 > 新特性`

---

## 12. 适用场景 vs 不适用场景

### ✅ 适合

- 嵌入式/边缘 **本地持久化 KV + 子集 SQL**  
- 需要 **可验证崩溃恢复**（RAR + powerfail fuzz）  
- **写多读少、点查+短 range** 的 OLTP  
- 对 **RPO=0 Put 返回** 有明确 SKU（kBalanced/kSync）需求  
- 团队能维护 **阶段 gate + ADR** 文化  

### ❌ 不适合（当前版本）

- 需要 **完整 SQLite/PostgreSQL 兼容**  
- 高并发 **读写隔离**（MVCC、可重复读）  
- **分析型大 scan**（lazy scan 延迟）  
- **多租户安全 / 网络暴露服务**  
- 期望 **开箱即用 DBaaS**  

---

## 13. 战略建议（按 ROI 排序）

1. **统一执行路径**：V3 接入 index scan / 推进 planner，减少全表扫；逐步收缩 V1/V2 表面  
2. **索引 v2**：复合索引 + range scan，与 `plan/lower.cc` 对齐  
3. **Lazy scan 专项**：延续 ADR-018 路线，以 P50/P99 为内部 SLO 跟踪  
4. **事务补全**：ROLLBACK TO SAVEPOINT / RELEASE；文档化隔离级别  
5. **Sqllogic L2 扩展策略**：按失败 cluster（函数/LIKE/CHECK）分批 import，而非盲目扩 800 cap  
6. **文档**：根 README + ADR index + 明确「产品边界一页纸」  
7. **attestation 产品化**：kBalanced + op_log 默认集成路径文档化  

---

## 14. 总结

eB-Tree 是一个 **工程纪律显著高于体量** 的嵌入式数据库项目：**存储内核**在耐久、恢复、契约测试方面达到 **准生产水准**；**SQL 层**在 ADR-032 划定的子集内已通过 **800+95 strict gate**，但架构上仍是 **三代执行器并存、浅索引、无 MVCC** 的 Beta 形态。

若用一句话定位：

> **强存储、弱 SQL 广度、极强测试与文档契约的嵌入式 OLTP 引擎 — 适合作为「可证明正确的本地数据库内核」，而非 drop-in SQLite 替代品。**

---

## 附录：关键路径索引

| 类别 | 路径 |
|------|------|
| 存储内核 | `cpp/` |
| SQL 层 | `sql/` |
| 测试清单 | `test/TEST_MANIFEST.yaml` |
| Invariant 清单 | `Docs/invariants/INVARIANT_MANIFEST.yaml` |
| Sync 清单 | `Docs/syncs/SYNC_MANIFEST.yaml` |
| 语义子集 ADR | `Docs/adr/032-sqlite-semantic-subset.md` |
| Subset B 路线图 ADR | `Docs/adr/033-full-sql-subset-b.md` |
| 活跃 ADR | `Docs/adr/043-kernel-rar-deep-cultivation.md`, `Docs/adr/044-phase4-stability-sprint.md` |
| Merge gate | `P4-complete` + `P17-deep-core` + `P18-stability` |
| 官方 sqllogic | `test/data/sqllogic/sqlite/imported.test` |
| 语义 oracle | `test/data/sqllogic/semantic/semantic.test` |
| Benchmark | `bench/` |

---

*报告基于仓库静态分析与代码审计；性能数据来自 ADR-009 及本地 Release gate。*
