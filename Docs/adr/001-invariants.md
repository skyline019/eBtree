# ADR-001: eB-Tree storage invariants

## I-D1 .. I-D4 (durability)

- I-D1: committed writes appear in WAL first
- I-D2: SuperBlock.data_lsn <= fsynced WAL tail
- I-D3: data file order matches WAL replay order
- I-D4: superblock dual-slot commit with critical CRC

## I-NF0 .. I-NF3 (read path)

- I-NF0: invalid plan rejected
- I-NF1: summary predicate prune (L0)
- I-NF2: stale summary returns StaleSummary (L1); one repair retry from committed/BTree allowed (I-NF2b)
- I-NF2b: summary repair rebuilds from committed map only; no WAL replay
- I-NF3: no read-path WAL full replay / full scan (L2)
- I-NF4: explicit read tiers only; `unexpected_path_total == 0` (see ADR-019)

Tests enforce these via pipeline, matrix, and no_fallback suites.
