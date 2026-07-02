# ADR-025: SQL OLTP Complete Specification (Phase 3 / 3b / 3c / 3d)

## Status

Accepted — Phase 3d adds CTE/SET OP/WINDOW exec and session txn blocks (SQL layer).

## Context

Phase 3b replaced legacy parsers with native token-registry stack. Phase 3c deepens OLTP exec and parse AST for advanced SQL. Phase 3d clears SQL-layer technical debt: hardened subqueries, CTE/SET OP/WINDOW execution, session-level txn journal. **Read snapshot isolation (SI)** is provided by engine LSV (ADR-046); write path remains single-version commit. Legacy `thirdbackup/sql_parse` archived to `Docs/archive/thirdbackup-sql_parse`.

## Decision

### Parser architecture (3b+)

- Entry: `RegistryParser` → `ParseBootstrap` → `NativeParser`
- Pipeline: `LexPipeline` / `TokenCursor` → `FirstMatchRegistry` stmt routes
- Expressions: Pratt-style `expr_parse`; recursive `SelectParse` for subqueries
- Advanced: `advanced_parse` for CTE / SET OP / WINDOW (full AST)

### Executable (Phase 3d)

| Category | Statements / features |
|----------|----------------------|
| DML | INSERT, UPDATE, DELETE with expression WHERE |
| DDL | CREATE/DROP TABLE, ALTER ADD COLUMN |
| SELECT | Project, WHERE, INNER/LEFT JOIN (multi-table chain) |
| Subqueries | IN / EXISTS / correlated (depth ≤3); MIC row budget |
| Aggregates | GROUP BY, COUNT/SUM/MIN/MAX, HAVING |
| Ordering | ORDER BY, LIMIT, DISTINCT |
| CTE | WITH materialize + virtual scan (non-recursive) |
| Set ops | UNION / INTERSECT / EXCEPT (+ ALL on UNION) |
| Window | ROW_NUMBER() OVER (PARTITION/ORDER) |
| Transactions | BEGIN, COMMIT, ROLLBACK, SAVEPOINT (SQL undo journal) |

### Parse-only (exec returns `SQLFeatureNotSupported`)

| Category | Statements |
|----------|------------|
| CTE | Recursive WITH |
| Window | RANK, DENSE_RANK, aggregates OVER (deferred) |
| Admin | GRANT, REVOKE, SHOW, EXPLAIN, SET |

### Storage model

- Catalog v2: N-column schema; row value = JSON object keyed by column name
- Engine keys remain `{table_id}:{user_key}`
- Session txn: undo journal in `Database` / `TransactionState` (not engine MVCC)
- **Read SI**: `BEGIN` captures engine `SnapshotToken`; SELECT uses `GetAtSnapshot` / `ScanAtSnapshot` (ADR-046 LSV). Writes remain single-version until commit.

### Error taxonomy

- `InvalidArgument` — syntax / unknown table / subquery depth exceeded
- `SQLFeatureNotSupported` — admin / unsupported WINDOW func
- `MicContractViolation` — scan/row budget exceeded when `@max_pages` set

### Kernel interaction (3c)

Nested correlated `EXISTS` uses multiple `Engine::Scan` on one thread; Phase 3c narrowed `ScanResolver` lock scope ([ADR-024](024-kernel-partial-unfreeze.md) Phase 3c supplement). Phase 3d removes SQL-layer correlation shortcuts; no further kernel changes.

### Build tooling (3d)

- CMake `EBTREE_PYTHON3_ROOT` (default `d:/anaconda3`) for `gen_sql_matrix_inc.py`
- Preset: `cmake/presets/windows-dev.json`

## Merge gates

- `ebtree_test_runner --suite=sql,audit,pipeline,matrix` all green
- `P3d-complete` manifest gate
- Configure: `-D Python3_EXECUTABLE=d:/anaconda3/python.exe` (or `EBTREE_PYTHON3_ROOT`)

## References

- [ADR-023](023-phase1-sql-attestation.md), [ADR-024](024-kernel-partial-unfreeze.md), [ADR-026](026-rar-v2-signing-and-sidecars.md)
