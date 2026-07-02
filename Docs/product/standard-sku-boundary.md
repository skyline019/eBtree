# eB-Tree Standard SKU 产品边界

## 是什么

嵌入式 OLTP 内核 + 原生 SQL 子集 + **常开可验证恢复（CARL）**。

## SKU 矩阵

| SKU | 耐久 | Open attestation | Runtime CARL | 写熔断 |
|-----|------|------------------|--------------|--------|
| **Standard** | kBalanced | MONITOR（默认，无 sync BuildRar） | async chain ON | stats + chain policy |
| **Enterprise** | kSync | MONITOR 或 REQUIRE_PASS opt-in | async chain ON | 同上 |
| **Compliance** | kSync | REQUIRE_PASS | async + sync Open RAR | Open 拒绝 badwal |

## 入口

| 用户 | API |
|------|-----|
| SQL | `Database::Open(OpenOptions)` — 默认 `attestation=kMonitor` |
| C API | `ebtree_sql_open` — `attestation_mode=0` → MONITOR |
| KV | `audit::OpenWithRarMonitor` — [`engine_rar.h`](../cpp/include/ebtree/engine/engine_rar.h)（**WriteGuard 自动安装**） |

**KV 注意**：裸 `Engine::Open` 不会安装 WriteGuard；MONITOR 写熔断仅在使用 `OpenWithRarMonitor` 或 SQL `Database::Open` 时生效。

## Chain drop（ADR-043）

| SKU | `reject_on_chain_drop` |
|-----|------------------------|
| Standard MONITOR | false（WARN + counter） |
| Compliance | true（写熔断） |

## 明确不提供

- 网络 server / 连接池
- 完整 SQLite 兼容
- MVCC / 可重复读
- 无 anchor 的 tamper-proof 保证
- 分布式复制

## 可验证性

- `PRAGMA rar_status` / `ebtree_sql_rar_status()`
- `ebtree_audit chain-verify [--require-anchor]`
- `ebtree_audit chain-anchor`

详见 [ADR-041](../adr/041-carl-checkpoint-attestation-recovery-log.md) 与 [ADR-043](../adr/043-kernel-rar-deep-cultivation.md).
