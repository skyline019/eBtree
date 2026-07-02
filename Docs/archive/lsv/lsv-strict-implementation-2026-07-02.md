# LSV / FPBC Strict 实现归档

**归档日期**：2026-07-02  
**状态**：Strict 验收完成（P19-lsv gate + 10/10 streak）。  
**规范入口**：[ADR-046](../../adr/046-lsn-native-snapshot-read-view.md) · [ADR-025](../../adr/025-sql-oltp-complete-spec.md)（读 SI）

---

## 1. 能力总览

**LSV**（LSN-Native Snapshot Read View）在 **不引入 page-MVCC** 的前提下，提供引擎级 **读快照隔离（SI）**：

| 层级 | 能力 | Strict 状态 |
|------|------|-------------|
| **FPBC 存储** | B-tree forward + VCS backward + DataFile payload | ✅ 热层 inline + VcsPager 冷层 |
| **读路径** | `GetAtSnapshot` / `ScanAtSnapshot` + ReadTier | ✅ WAL snapshot 回退 tier |
| **GC** | pin 处 defer swap（reclaim generation 交叉） | ✅ `MaybeGcSwap` + pin defer |
| **并发** | SPF-RW 锁 + 后台 flush/summary 让步 | ✅ snapshot pin 窗口读者优先 |
| **SQL** | BEGIN 捕获 token；SELECT 走 snapshot 读 | ✅ 跨连接 SI 单测 |
| **Powerfail** | sync/balanced/group × checkpoint 相位 | ✅ SnapshotOracle @ reopen |
| **Perf** | 4 项 `LsvPerfRegression.*` | ✅ pragmatic 阈值 |

**非目标（仍为 Non-goals）**：SSI/Serializable、分布式复制、多分片 RAR Phase 6、多写者同目录并发模型。

---

## 2. 内核实现索引

### 2.1 VCS 分层与持久化

| 模块 | 路径 | 说明 |
|------|------|------|
| VersionChainStore | `cpp/src/concept/vcs/version_chain_store.cc` | 热层 `kVcsInlineMax=8` + 冷层 overflow |
| VcsPager | `cpp/include/ebtree/concept/vcs/vcs_pager.h` | `shard{N}.vcs` + meta sidecar |
| VCS sidecar | `ShardEngine::SaveVcsSidecar` / `LoadVcsSidecar` | `.vidx` 与 checkpoint 同步 |
| FoldWalToVcs | `shard_engine.cc` | checkpoint 阶段 2 |

### 2.2 读路径与 Snapshot

| 模块 | 路径 | 说明 |
|------|------|------|
| SnapshotResolver | `cpp/src/engine/snapshot_resolver.cc` | memtable → committed → btree → VCS → WalSnapshotKey |
| GetAtSnapshot 升级 | `ShardEngine::PrepareSnapshotReadLocked` | 与 `Get()` 对齐 WAL replay / summary repair |
| WalSnapshotKey | `TryWalSnapshotFloor` | recovery + `wal_replay_pending` 回退 |
| WAL replay 语义 | `wal_segment.cc` | `ReplayPending` 写入 memtable **`durable=false`** |

### 2.3 并发：SPF-RW

| 模块 | 路径 | 说明 |
|------|------|------|
| SnapshotFairRwLock | `cpp/include/ebtree/engine/snapshot_fair_rw_lock.h` | 替代 `std::shared_mutex` |
| PinSnapshot | `shard_engine.cc` | atomic `snapshot_pin_count_` |
| TryFlushBackground | `shard_engine.cc` | 后台 flush 非阻塞独占 |
| TryRepairSummaryIfDriftedBackground | `shard_engine.cc` | summary validator 让步 |

### 2.4 GC defer

| 模块 | 说明 |
|------|------|
| `MaybeGcSwap` | `ReferencedLsnsAbove(pinned_snapshot_lsn_)` × reclaim generation → defer |
| `CompactBelow` | checkpoint 后在 pin floor 保留节点 |

---

## 3. SQL 层联动

| 行为 | 实现 |
|------|------|
| `BEGIN` | `TransactionState::Begin` → `CaptureSnapshot` + `PinSnapshot` |
| SELECT | `GetAtSnapshot` / `ScanAtSnapshot`（`transaction_state` / `physical_scan`） |
| DML | `txn_id` 写入 memtable；COMMIT `PromoteTxn` |
| 跨连接读 SI | `SnapshotSiSql.HidesOtherTxnUncommittedWrite` |

---

## 4. 不变量与测试

### 4.1 Manifest（I-VCS-*）

见 [`INVARIANT_MANIFEST.yaml`](../../invariants/INVARIANT_MANIFEST.yaml)：

| ID | 测试 |
|----|------|
| I-VCS-FWD | `VcsInvariantTest.ForwardPointerMatchesHead` |
| I-VCS-WAL | `SnapshotResolverTest.IncompleteVcsUsesWalSnapshotTier` |
| I-VCS-TOMB | `SnapshotResolverTest.DeleteTombstoneInvisibleAtSnapshot` |
| I-VCS-TIER | `SnapshotResolverTest.VersionChainTierRecorded` |

### 4.2 P19-lsv Gate 范围

Filter（`run_tests.ps1` `$P19LsvFilter`）：

- **unit**：`VcsChain*`、`VcsInvariant*`、`VcsGcPin*`、`SnapshotResolver*`
- **failure**：`VcsPowerfail*`、`VcsSnapshotPowerfail*`（分两进程跑，避免 DLL 内挂死）
- **pipeline**：`LsvPerfRegression.*`
- **sql**：`SqlTxn.SnapshotReadOwnWriteBeforeCommit`、`SnapshotSiSql.*`
- **matrix**：`EbMatrix/NoFallbackMatrixTest.*`

Harness：`Invoke-TestRunnerWithProgress.ps1`（分 suite 日志、log 增长重置 stale、正确 propagate exit code）。

---

## 5. 验收证据

### 5.1 MVP（2026-07-02）

| 项 | 证据 |
|----|------|
| P19-lsv 单次 gate | ~70s local Release |
| MVP streak | **10/10** — `.test-runs/streak-20260702-153149` |

### 5.2 Strict（2026-07-02）

| 项 | 证据 |
|----|------|
| SPF-RW + GetAtSnapshot WAL 升级 + WAL replay `durable=false` | 代码 + powerfail/SQL 全绿 |
| Strict streak | **10/10 ACCEPT** — `.test-runs/streak-20260702-200714` |
| Streak 归档副本 | [`streak-20260702-200714/summary.csv`](streak-20260702-200714/summary.csv) |

**Strict streak 耗时（秒）**：306.5, 282.1, 418.6, 322.7, 371.2, 475.4, 410.6, 338.7, 459.1, 434.9

### 5.3 复现命令

```powershell
.\scripts\test\run_tests.ps1 -Gate P19-lsv -Config Release
.\scripts\test\run_gate_streak.ps1 -Gates P19-lsv -Trials 10 -MinPass 9 -Config Release
```

---

## 6. Perf 阈值（Pragmatic Strict）

来源：`test/pipeline/lsv_perf_regression_test.cc`（local + CI 同 pragmatic 档）

| 测试 | Gate |
|------|------|
| SnapshotGetHotKeyBudget P50 | ≤1.05 ms local / ≤5 ms CI |
| SnapshotGetAfterUpdateBudget P50 | ≤2 ms local / ≤5 ms CI |
| SnapshotScan10kBudget | ≤75 ms |
| SnapshotWriteOverheadBudget | ≥700 TPS |

活跃基线摘要：[kernel-rar-baseline.md](../perf/kernel-rar-baseline.md)

---

## 7. 已知限制与后续

| 项 | 说明 |
|----|------|
| 写 MVCC | 写路径仍单版本 commit；无 SSI |
| 多 Engine 同目录 | 读 SI 已测；多写者未定义 |
| WAL txn 元数据 | replay 靠 `durable=false` 兜底，非终态 |
| Perf strict 原目标 | scan 55 ms / write 776 TPS 未恢复；现 pragmatic 75 ms / 700 TPS |
| Windows harness | test_runner 不 FreeLibrary（DllMain 约束） |

---

## 8. 参考 ADR

- [ADR-046](../../adr/046-lsn-native-snapshot-read-view.md) — LSV 决策与 gate
- [ADR-025](../../adr/025-sql-oltp-complete-spec.md) — SQL 读 SI
- [ADR-043](../../adr/043-kernel-rar-deep-cultivation.md) — 原 MVCC non-goal（读 SI 由 046 补充）
- [ADR-044](../../adr/044-phase4-stability-sprint.md) — streak 阈值契约（MinPass 9/10）
- [ADR-047](../../adr/047-lsv-tso-commit-occ.md) — 写 OCC / Txn-WAL（后续）
- [LSV-TSO 归档](lsv-tso-implementation-2026-07-02.md) — P20 验收（2026-07-03）
