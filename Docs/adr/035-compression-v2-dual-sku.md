# ADR-035: Compression v2 Dual-SKU

## Status

Accepted (2026-07-01)

## Context

ADR-029 shipped LZMA-only C2/C4 with defaults off. P9 requires **high-speed** and **low-storage** SKUs without regressing `ProductionDefaults` perf (ADR-009).

## Decision

### Codec wire IDs (DataRecordHeader.reserved[0])

| ID | Codec | Use |
|----|-------|-----|
| 0 | Raw | default |
| 1 | Legacy RLE | read-only |
| 2 | LZ4-fast | C2 hot path |
| 3 | LZMA | C4 dense / legacy |
| 4 | Zstd-fast | C2 balanced (level-1 equivalent via fast LZMA fallback until zstd linked) |

### SKU factories

| Factory | compress_values | compress_pages | Policy |
|---------|-----------------|----------------|--------|
| `ProductionDefaults` | false | false | ADR-009 raw bench baseline |
| `StandardDefaults` | true (fast) | false | **Product default** (alias of ProductionCompressDefaults) |
| `ProductionCompressDefaults` | true (fast) | false | BalancedCompress explicit |
| `EnterpriseCompressDefaults` | true (balanced) | true (dense) | SpaceEfficient |

### CodecRegistry

- Single entry: `CodecRegistry::CompressValue` / `DecompressValue`
- Policy: `kFastOnly`, `kBalanced`, `kDense`, `kAuto`
- Store-if-smaller; skip if `|value| < 64`

### Performance SLO (local NVMe, soft gate)

| SKU | DataFile size | Put vs raw | Scan 10k vs raw |
|-----|---------------|------------|-----------------|
| BalancedCompress | -30% target | ≤1.08× | ≤1.10× |
| SpaceEfficient | -45% target | ≤1.20× | ≤1.25× |

### Invariants

- Decompress failure → `CorruptPage`; never alternate codec retry
- WAL records uncompressed in v2
- Default bench path (`ProductionDefaults`): **zero compress calls**
- Product path (`StandardDefaults`): compress on flush; WAL still raw

## References

- ADR-029, ADR-024, ADR-009 Q3 supplement, ADR-039
