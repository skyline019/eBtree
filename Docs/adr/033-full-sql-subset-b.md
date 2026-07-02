# ADR-033: Full SQL Subset B Roadmap

## Status

Accepted (2026-07-01)

## Context

ADR-032 delivered Subset A (official L1 800 + semantic 95 strict). Product target is
**Subset B** (ADR-031 P8-program-complete): curated sqllogic ≥90% strict, official 800
100%, semantic 100%, with plan-driven index runtime, expression/constraint completion,
and session savepoint semantics.

## Decision

### In scope (Subset B)

| Area | Deliverable |
|------|-------------|
| Execution | Plan-driven index eq/range scan in V3 rich SELECT; EXPLAIN = runtime |
| Expressions | LIKE/GLOB, CAST, FunctionRegistry (NULLIF/IFNULL/IIF/UPPER/LOWER/TRIM/…) |
| Constraints | NOT NULL on UPDATE; CHECK at INSERT/UPDATE |
| DML | INSERT OR IGNORE/ABORT; DELETE expression WHERE |
| Transactions | ROLLBACK TO SAVEPOINT, RELEASE SAVEPOINT |
| Advanced | RANK/DENSE_RANK window; subquery PhysicalScan dedup |
| Gates | P13-expr-sql, P13-constraints-sql; P8-program-complete milestone |

### Out of scope

ATTACH, MVCC, VIRTUAL TABLE, 120k sqllogic CI gate, WAL/memtable/flush changes.

### Kernel boundary

Zero semantic kernel PRs (ADR-024). All work in `sql/`.

### Verification gates

| Gate | Requirement |
|------|-------------|
| P11-real-sql | official 800 strict 100% |
| P12-semantic-sql | semantic 95 strict 100% |
| P13-expr-sql | like/cast/coalesce expr tests |
| P13-constraints-sql | CHECK constraint tests |
| P8-program-complete | curated strict ≥90% + full suite |

## References

- ADR-024, ADR-031, ADR-032
