# ADR-032: SQLite Semantic Subset A & Unified Executor

## Status

Accepted (2026-06-30)

## Context

ADR-031 defined SQLite compatibility scope. Prior gates (P10/P11) measured pass rate on
sqllogictest corpora with harness concessions (`coltypes:I` integer truncation). Remaining
failures (~76 on official L1) clustered around 3VL (IN/NOT NULL/BETWEEN), aggregates, and
UPDATE SET expressions. Execution was split across SqlExecutor (V1), SqlExecutorV2, and
SqlExecutorV3 with `NeedsRichSelectExec` heuristics.

Engine constraints from ADR-025 remain fixed: no MVCC changes, subquery depth ≤ 3, MIC
`@max_pages`, index-key filtering, single-threaded correlated subqueries via `Engine::Scan`.

## Decision

### Subset A (in scope)

- sqllogictest evidence paths: in2, aggfunc, update, createview, trigger, index-between
- OLTP DML/DDL, curated 515, official 800 L1 sample
- Semantic oracle micro-corpus (`test/data/sqllogic/semantic/`)

Out of scope: ATTACH, VIRTUAL TABLE, extensions, full 120k raw sqllogictest, SQL:2016.

### Semantic kernel (`sql/eval/`)

| Component | Role |
|-----------|------|
| `SqlValue` / `TruthValue` | Typed values + SQLite 3VL |
| `ExprEval::EvalValue` / `EvalTruth` | Projection vs predicate evaluation |
| `CompareSqlValues` | Affinity-aware comparison returning `TruthValue` |
| `AggEngine` | COUNT/SUM/AVG/MIN/MAX/GROUP_CONCAT with `ToDisplayString` |
| `SchemaContext` | Column affinity from catalog |

Legacy `RowMap` (string/null-as-empty) remains at executor boundaries; conversion uses
`SqlValue::FromLegacyString` / `ToLegacyString`.

### Unified executor (`sql/exec/`)

| Module | Role |
|--------|------|
| `UnifiedExecutor` | Single `Database::executor_` entry |
| `PhysicalScan` | Table range scan + index-key filter |
| `QueryPipeline` | WHERE filter helper (`EvalTruth`) |
| `DmlExecutor` | UPDATE with SET expression evaluation |
| `SqlExecutorV3` | SELECT rich pipeline (join/agg/CTE); single `ExecSelectRich` path |

All SELECT with `select_rich` uses `ExecSelectRich` and `EvalTruth` (no `NeedsRichSelectExec` fork).

`SubqueryRunner::Run` must preserve inner row column values in IN/EXISTS result rows; outer
correlation is injected only into `eval_fields` for WHERE (qualified names), not merged into
projected subquery rows (fixes `col0 IN (SELECT col3 …)` and label-110 index-between cases).

### Parse facade (`sql/parse/`)

| Module | Role |
|--------|------|
| `parse_facade.h` / `RegistryParser` | Single public parse entry |
| `parse/stmt/dml/` | INSERT/UPDATE/DELETE/SELECT rules |
| `parse/stmt/ddl/` | CREATE/DROP/ALTER/VIEW/TRIGGER/REINDEX rules |
| `parse/native/` | Expr/select/advanced parsers + `SyncLegacySelectFields` |

Tables without PRIMARY KEY use **implicit rowid** keys (`Catalog::implicit_rowid`).

### Dual-track gates

| Gate | Requirement |
|------|-------------|
| **P12-semantic-sql** | `SemanticOracle` test + `semantic.test` **100%** strict vs expected (oracle script validates corpus vs `sqlite3` when available) |
| **P11-real-sql** | official 800 **strict** **100%** |
| **P11-program-honest** | full suite + curated **strict** ≥ 90% + official **100%** |

`IntTruncateMatch` removed from `sqllogic_common.h`. Integer display uses
`SqlValue::ToDisplayString('I')` instead of truncating REAL strings in the harness.

90% targets **L1 sampled corpora**, not the full 120k sqllogictest import.

### Corpus tiers (non-blocking)

- **L1**: official cap 800 — CI P11 gate
- **L2**: expanded import — nightly trend only

## Consequences

- P11 may dip briefly after strict matching; recovery tracked per milestone (P12 hard-blocks core semantics).
- `sqlite3` CLI optional in CI; `run_semantic_oracle.ps1` skips with warning if missing.
- EXPLAIN/plan (`plan/lower.cc`) unchanged; runtime index choice via physical scan heuristics.

## References

- ADR-025: SQL OLTP complete spec (engine boundaries)
- ADR-031: SQLite compat spec (subset scope)
- Plan: SQLite Semantic Kernel (subset A)
