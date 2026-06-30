# ADR-016: On-disk B-Tree query and WAL key index

## Decision

### On-disk B-Tree (P9)

- `LoadRoot` reads **one root page** (header + summary) only; does not recurse into the tree.
- `Get` / `Scan` traverse pages at runtime with Trie/Histogram summary pruning; `pages_touched` counts real I/O.
- `index_` is a **delta overlay** for keys mutated since Open; tombstones tracked in `delta_deleted_`.
- `PersistRootFromMap(committed)` builds pages from the full committed snapshot at checkpoint (not delta-only).
- `PageFile` uses a persistent read handle and optional LRU page cache (`EngineOptions::page_cache_capacity`).

### WAL key index

- `WalKeyIndex` maps key → last record file offset; built on WAL open and updated on append.
- Lazy `RestoreKeyFromWal` uses index + `ReplayRecordAt` (O(1) seek); `wal_full_scan_total` forbidden on fast-open path.

### Histogram summary

- `kSummaryTypeHistogram = 2` on internal pages (8-bin prefix hash counts) for range pruning when `prefer_histogram_summary` is set.

## Invariants

- I-PG5: Open loads root only (`pages_touched == 1`).
- I-PG6: Histogram summary type defined.
- I-WAL1: WAL key index lookup works.

## Forbidden

- `open_eager_loads_all_btree_pages`
- `read_path_wal_full_scan`
- `persist_from_delta_only`

## Rationale

Eliminates hybrid RAM-authoritative index debt from P8 while keeping fast Open/RTO and true No-Fallback page pruning on Scan.
