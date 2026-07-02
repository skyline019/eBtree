# LSV-TSO 实现归档

**归档日期**：2026-07-02（验收 2026-07-03）  
**状态**：P20-lsv-tso gate + **10/10 streak ACCEPT**  
**规范入口**：[ADR-047](../../adr/047-lsv-tso-commit-occ.md) · [ADR-046](../../adr/046-lsn-native-snapshot-read-view.md)（读 SI 基础）

---

## 1. 能力总览

**LSV-TSO** 在 FPBC/LSV（ADR-046）之上，**不引入 page-MVCC**，提供引擎 + SQL 级 **读写 SI + lost-update 防护 + RR phantom**：

| 层级 | 机制 | 状态 |
|------|------|------|
| **读 SI** | `GetAtSnapshot` / `ScanAtSnapshot` + `read_set` 采样 | ✅ |
| **写 SI** | Commit Ticket OCC（`write_set` vs snapshot LSN） | ✅ |
| **Phantom** | Range Ticket 注册 + commit 校验 | ✅ |
| **WAL** | v2 `txn_id`；`TxnBegin`/`TxnCommit`/`TxnAbort` | ✅ |
| **Sidecar** | `shard{N}.txidx` checkpoint 持久化 | ✅ |
| **锁** | TSL-3（SPF-RW + L0 append lane） | ✅ |
| **读性能** | SFS-Read floor cache `(key_hash, S_epoch)` | ✅ |
| **跨 Engine** | WAL Tail Anchor + `SyncExternalTail` | ✅ |

**非目标（仍为 Non-goals）**：SSI/Serializable、多写者同目录、page-MVCC。

---

## 2. 内核实现索引

### 2.1 Commit OCC

| 模块 | 路径 | 说明 |
|------|------|------|
| read_set / write_set | `sql/session/transaction_state.cc` | BEGIN 采样；COMMIT OCC 校验 |
| ResolveCurrentCommittedLsn | `cpp/src/engine/shard_engine.cc` | txn 过滤的磁盘 LSN（非 key_index 未提交项） |
| RefreshExternalWalIfPending | `shard_engine.cc` | 跨 Engine 提交前 `SyncExternalTail` |
| CrossEngine 诊断 | `test/failure/snapshot_occ_engine_test.cc` | OCC 可见性回归 |

### 2.2 Txn-WAL v2 + 恢复

| 模块 | 路径 | 说明 |
|------|------|------|
| WAL append txn_id | `cpp/src/engine/shard_engine.cc` | Put/Delete 带 txn overlay |
| txidx sidecar | checkpoint 路径 | committed txn 索引 |
| FoldWalToVcs | `shard_engine.cc` | 仅 committed 折叠 |
| 恢复测试 | `test/failure/txn_wal_recovery_test.cc` | 提交可见 / 未提交隐藏 |

### 2.3 WAL Tail Anchor（跨 Engine + perf）

| 模块 | 路径 | 说明 |
|------|------|------|
| `tail_data_end_` | `cpp/include/ebtree/concept/wal/wal.h` | 单 writer O(1) 早退 |
| SyncExternalTail | `cpp/src/concept/wal/wal.cc` | 锚点增量扫描 |
| embedded magic 扫描 | `cpp/src/concept/wal/wal_segment.cc` | checkpoint 后 sector 偏移 |
| 批 flush | `cpp/src/concept/wal/wal_batch_pipeline.cc` | 单条 Put 立即 flush |

### 2.4 TSL-3 + SFS-Read

| 模块 | 路径 | 说明 |
|------|------|------|
| SnapshotFairRwLock | `cpp/src/engine/snapshot_fair_rw_lock.cc` | `lock_append_shared` L0 lane |
| SfsReadCache | `cpp/src/engine/sfs_read_cache.cc` | scan floor 缓存 |
| 单测 | `test/unit/tsl3_lock_test.cc`, `test/unit/sfs_read_test.cc` | — |

### 2.5 Range Ticket（Phantom）

| 模块 | 路径 | 说明 |
|------|------|------|
| Engine API | `cpp/src/engine/engine_txn_lsv.cc` | Register / Validate |
| SQL 联动 | `sql/session/transaction_state.cc` | range scan 注册 ticket |
| SQL 测试 | `test_sql/snapshot_phantom_test.cc` | INSERT 冲突 |

---

## 3. SQL 层联动

| 行为 | 实现 |
|------|------|
| SELECT / scan | `read_set` 采样 + `GetAtSnapshot` / `ScanAtSnapshot` |
| DML write intent | `RecordWriteIntent` → `write_set` |
| COMMIT | OCC 校验 + Range Ticket + `PromoteTxn` |
| 并发 lost-update | `SnapshotOccSql.ConcurrentUpdateSecondCommitConflicts` |
| 自写可见 | `SqlTxn.SnapshotReadOwnWriteBeforeCommit` |

---

## 4. 不变量与测试

### 4.1 Manifest（I-LSV-*）

见 [`INVARIANT_MANIFEST.yaml`](../../invariants/INVARIANT_MANIFEST.yaml)：

| ID | 测试 |
|----|------|
| I-LSV-OCC | `SnapshotOccSql.ConcurrentUpdateSecondCommitConflicts` |
| I-LSV-TXN-WAL | `TxnWalRecovery.CommittedVisibleAfterReopen` |
| I-LSV-PHANTOM | `SnapshotPhantomSql.InsertDuringActiveRangeScanConflicts` |

（P19 的 I-VCS-* 仍由 P20 gate 一并回归。）

### 4.2 P20-lsv-tso Gate 范围

Filter（`run_tests.ps1` `$P20LsvTsoFilter`）：

- **unit**：P19 子集 + `Tsl3Lock*`、`SfsRead*`
- **failure**：P19 powerfail + `TxnWalRecovery*` + `SnapshotOccEngine.*`（分进程）
- **pipeline**：`LsvPerfRegression.*`
- **sql**：P19 sql + `SnapshotOccSql.*`、`SnapshotPhantomSql.*`
- **matrix**：`EbMatrix/NoFallbackMatrixTest.*`

Harness：`Invoke-TestRunnerWithProgress.ps1`（双信号 stale、log FAILED 强制 exit 1、HANG exit 124、`KillProcessTree`）。

---

## 5. 验收证据

### 5.1 单次 gate（2026-07-03）

| 项 | 证据 |
|----|------|
| P20-lsv-tso watchdog gate | **PASS** ~88s，全 suite 无 FAILED |
| Build | `build-msvc-2026` Release (MSVC) |

### 5.2 Streak（2026-07-03）

| 项 | 证据 |
|----|------|
| ADR-044 streak | **10/10 ACCEPT**（MinPass 9/10） |
| 源运行 | `.test-runs/streak-20260703-022441` |
| 归档副本 | [`streak-20260703-022441/summary.csv`](streak-20260703-022441/summary.csv) |

**Streak 耗时（秒）**：130.2, 90.8, 202.9, 131.1, 195, 170.7, 203, 146.3, 170.3, 170.4

### 5.3 复现命令

```powershell
.\scripts\test\run_tests_with_watchdog.ps1 -Gate P20-lsv-tso -Config Release
.\scripts\test\run_gate_streak.ps1 -Gates P20-lsv-tso -Trials 10 -MinPass 9 -Config Release
```

---

## 6. Perf 阈值（Pragmatic）

来源：`test/pipeline/lsv_perf_regression_test.cc`

| 测试 | Gate | 备注 |
|------|------|------|
| SnapshotGetHotKeyBudget P50 | ≤1.05 ms local / ≤5 ms CI | — |
| SnapshotGetAfterUpdateBudget P50 | ≤2 ms local / ≤5 ms CI | — |
| SnapshotScan10kBudget | ≤75 ms | 预热 + best-of-3 |
| SnapshotWriteOverheadBudget | ≥700 TPS | kBalanced batch |

---

## 7. 已知限制与后续

| 项 | 说明 |
|----|------|
| 隔离级别 | SI + RR phantom ticket；非 SSI |
| 多写者同目录 | 未正式支持；Tail Anchor 缓解读/append 可见性 |
| OCC 成本 | `LatestCommittedLsnForKey` 仍为 WAL 磁盘扫描（txn 过滤） |
| Scan10k 原 55 ms | 本机 stressed 58–77 ms；gate 统一 pragmatic 75 ms |
| Windows harness | test_runner 不 FreeLibrary（DllMain 约束） |

---

## 8. 参考 ADR

- [ADR-047](../../adr/047-lsv-tso-commit-occ.md) — LSV-TSO 决策与 gate
- [ADR-046](../../adr/046-lsn-native-snapshot-read-view.md) — 读 SI / FPBC
- [ADR-044](../../adr/044-phase4-stability-sprint.md) — streak 阈值（MinPass 9/10）
- [P19 Strict 归档](lsv-strict-implementation-2026-07-02.md) — 读路径前置验收
