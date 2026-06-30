# ADR-030: Secondary Index and Catalog v3 (Phase 6)

## Status

Accepted — SQLite-style secondary indexes on engine KV.

## Decision

### Index key encoding

`{table_id}:i{index_id}:{encoded_col_value}:{user_pk}`

Primary row key unchanged: `{table_id}:{user_pk}`.

### Catalog v3

- `IndexDef`: name, table, columns[], unique
- Persisted in `ebtree.catalog.json`
- `CREATE INDEX idx ON t(col)` / `DROP INDEX idx`

### Maintenance

- On INSERT/UPDATE/DELETE: synchronous Put/Delete of index entries in SQL executor (same durability as row)
- Future: batch via Flusher (v2)

### Query

- Equality/range on indexed column → prefix scan on index keys → resolve PK → Get row
- `PlanStepKind::kIndexScan` in `lower.cc`

### Kernel whitelist (ADR-024 supplement)

- Index entries are ordinary engine keys; no WAL semantic change

### Gates

- **P6-sql**: `index.matrix` parse + exec (20+ cases)

## References

- [ADR-025](025-sql-oltp-complete-spec.md), [ADR-031](031-sqlite-compat-spec.md)
