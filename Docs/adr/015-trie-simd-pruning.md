# ADR-015: Trie SIMD page pruning

## Decision

Internal and leaf pages use `kSummaryTypeTrie` when fan-out exceeds threshold (`kTrieSummaryThreshold=8`). Page headers store a 14-byte prefix used to prune scans before touching child pages. MSVC builds may enable `EBTREE_SIMD` for 16-byte vector compare; portable builds use `memcmp`.

## Rationale

Delivers ADR-008 Trie full-path pruning in C++ without fallback reads. MinMax summaries remain for small pages.

## Consequences

- `I-PG3` skeleton becomes functional via prefix checks in `PageSummaryCoversKey`
- `pages_touched` exposed on `BTreeIndex` for tests
