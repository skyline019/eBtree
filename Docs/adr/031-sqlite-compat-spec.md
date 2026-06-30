# ADR-031: SQLite Embedded Compatibility (Phase 6–8)

## Status

Accepted — target ~90% of curated sqllogictest subset.

## Scope

### In scope (P6–P8)

| Phase | Features |
|-------|----------|
| P6 | CREATE INDEX, index scan, binary row |
| P7 | PREPARE/bind, UPSERT, VIEW, EXPLAIN QUERY PLAN, full JOIN semantics, CHECK/NOT NULL |
| P8 | RANK/DENSE_RANK OVER, recursive CTE subset, TRIGGER subset, PRAGMA table_info |

### Out of scope

- Server, GRANT/REVOKE, replication
- SQLite file ATTACH, extension loading
- Full SQL:2016

### Verification

- `scripts/test/run_sqllogictest.ps1` on `test/data/sqllogic/*.test`
- P7 milestone: **60%** pass on core subset
- P8 **P8-program-complete**: **90%** pass, `min_sql_tests: 200`

## References

- [ADR-025](025-sql-oltp-complete-spec.md), [ADR-030](030-secondary-index-catalog-v3.md)
