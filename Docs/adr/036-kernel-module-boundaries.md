# ADR-036: Kernel Module Boundaries

## Status

Accepted (2026-07-01)

## Context

`shard_engine.cc` (~985 LOC) and `paged_btree.cc` (~1000 LOC) block P9 parallel work. Refactor must not change WAL/memtable/flush semantics (ADR-024).

## Decision

### Target modules

| Module | Responsibility | LOC budget |
|--------|----------------|------------|
| `shard_engine.cc` | Facade, delegate | ≤200 |
| `shard_scan_values.cc` | ResolveScanValues, ScanCommittedDirect | ≤300 |
| `shard_read_path.cc` | ReadVisible helpers (future) | ≤200 |
| `shard_write_path.cc` | Put/Delete/Checkpoint orchestration (future) | ≤300 |
| `codec_registry.cc` | Value/page codec dispatch | ≤200 |

### Refactor rules

1. Pure move PRs: no logic diff in hot paths
2. Each PR runs `P9-nofallback-hard` subset
3. Public API unchanged unless ADR-024 whitelist extended

### Include layering

- `engine/` may include `concept/` interfaces, not page format internals
- `tools/ebtree_audit` uses `engine_attest.h` only

## References

- ADR-024, ADR-035
