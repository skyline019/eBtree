# ADR-023: Phase 1 SQL + Attestation + MIC + op_log Sidecar

## Status

Accepted — Phase 1 delivery on frozen kernel (`cpp/` unchanged).

## Context

ADR-022 established Recovery Attestation Report (RAR) v1 with three layers (physical, recovery, contract). Layer 3 contract for `kBalanced` cannot be inferred offline from WAL alone in production. Phase 1 adds a SQL wrapper, MIC (Measured I/O Contract), and an append-only op_log sidecar without modifying the engine.

## Decision

### Scope (in)

| Component | Location | Role |
|-----------|----------|------|
| Minimal SQL exec | `sql/` | OPEN / CREATE / INSERT / SELECT |
| OPEN WITH ATTESTATION | `sql/session/` | BuildRar + PolicyGate before `Engine::Open` |
| MIC `@max_pages` | `sql/exec/mic_guard` | `btree()->pages_touched()` delta gate |
| op_log sidecar | `sql/op_log/` + `tools/ebtree_audit/op_log_*` | Production Layer 3 contract source |
| C ABI | `c_api/ebtree_sql.h` | `ebtree_sql_open` / `execute` / `close` |
| Parse-only port | `sql/parse/ported/` | Golden parse matrix (exec still via MinimalParser) |

### Non-goals (out)

- Kernel `Engine::ExportRar()` / public `recovery_state()` (Phase 2 whitelist)
- JOIN, subqueries, MVCC exec, Ed25519 RAR signing
- Full `thirdbackup` 99-file port in one step

## OPEN WITH ATTESTATION

```sql
OPEN DATABASE 'path'
  WITH DURABILITY BALANCED
  WITH RECOVERY_MAX_MISSING 0
  WITH ATTESTATION REQUIRE_PASS;
```

Modes:

| Mode | Allows open when verdict is |
|------|----------------------------|
| `REQUIRE_PASS` | PASS only; also rejects `badwal_marker` |
| `ALLOW_WARN` | PASS or WARN |
| `OFF` | Skips RAR contract path |

Flow: physical attest → `Engine::Open` (inside BuildRar) → recovery probes → optional op_log contract → PolicyGate. On REFUSE_START the SQL layer returns error and does not expose `Database`.

## op_log format (v1)

Default path: `{engine_path}/ebtree.op_log.jsonl`

```json
{"v":1,"op":"put","key":"1:k1","value_sha256":"…","lsn":42,"durable_at_return":true,"tier":"balanced","ts_unix":1719660000}
```

`durable_at_return` rules (ADR-021/022):

| Tier | At Put/Delete return |
|------|----------------------|
| kSync / kBalanced | `true` |
| kGroup | `false` until GroupCommit flips batch |

Open-time contract: read op_log entries with `durable_at_return=true`, build `ExpectSnapshot` (`key_set_source=op_log`), run ContractAttest + `recovery_max_missing` gate.

CLI: `ebtree_audit verify --path <dir> --op-log <file> [--mode durable]`

## MIC `@max_pages`

| `@max_pages` | Prepare+Scan 前后 `btree()->pages_touched()` delta；range scan 额外以返回行数作为 committed-direct 路径的补充约束 |

## Relationship to ADR-022

RAR v1 offline attestation remains valid for tests/oracle. Production kBalanced Layer 3 uses op_log as the durable key-set source written at SQL exec time. LSN in op_log snapshots `engine->stable_lsn()` after each op — documented approximation per ADR-022.

## Verification

- `ebtree_test_runner --suite=audit` — 12/12 + op_log equivalence
- `ebtree_test_runner --suite=sql` — smoke, attestation, MIC, op_log, parse matrix, C API
- Kernel perf/failure gates unchanged
