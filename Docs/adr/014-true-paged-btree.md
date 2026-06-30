# ADR-014: True paged B-Tree

## Decision

PagedBTree persists internal nodes when multiple leaf pages exist. Open loads the tree recursively from `active_root` into the in-memory index. Range scan uses ordered iteration (`lower_bound`) instead of full-map scans.

## Rationale

P7 leaf-only snapshots left disk layout as checkpoint artifact while reads always hit RAM `std::map`. P8 makes on-disk pages authoritative on reopen and enables page-level summary pruning for Trie/SIMD work.

## Consequences

- `PersistRoot` may emit `kPageTypeInternal` roots
- `LoadRoot` recursively walks internal and leaf pages
- `pages_touched` stat tracks page reads during load/scan
