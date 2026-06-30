# ADR-029: Value Compression (Phase 6–10)

## Status

Accepted — four-layer compression under ADR-024 constraints. Phase 10 replaces pseudo-RLE with **7-Zip LZMA** (Public Domain, `7zip-main/C`).

## Decision

### Layers

| Layer | Mechanism | Flag |
|-------|-----------|------|
| C1 SQL row | Binary TLV row codec (replaces JSON for new rows) | `schema_version >= 3` |
| C2 DataFile value | **7-Zip LZMA** (fast preset) in `DataFile::Append` | `EngineOptions::compress_values`, `DataRecordHeader.reserved[0]` codec |
| C3 Leaf prefix | Shared prefix keys in leaf pages | `PageHeader` format bit |
| C4 Page block | **7-Zip LZMA** whole page at checkpoint | `EngineOptions::compress_pages`, PageFile wrapped format |

Defaults: all **off** for ADR-009 perf parity.

### DataRecordHeader codec (reserved[0])

| Value | Meaning |
|-------|---------|
| 0 | Raw value bytes |
| 1 | Legacy RLE (Phase 6, **read-only**) |
| 2 | Reserved |
| 3 | **LZMA (7zip LzmaLib)** — Phase 10 write path |

Generation remains in `reserved[1]` when compression enabled.

### LZMA presets (Phase 10)

| Preset | Use | level | dictSize |
|--------|-----|-------|----------|
| FastValue | C2 values | 1 | 256 KiB |
| PageBlock | C4 pages | 5 | 1 MiB |

Wire payload: `[u32 uncompressed_size][5-byte LZMA props][compressed bytes]`.

### PageFile wrapped format (C4)

When `compress_pages=true`, each stored page is `[u32 total_size][u8 codec][payload...]`; logical page remains 4096 bytes after decompress. `PageFileHeader.format_flags` bit 0 marks wrapped layout.

### Invariants

- T-Log / flashback: per-record compression; sidecar indexes store **file offsets** to compressed records
- Decompress failure → `CorruptPage`, never fallback read
- WAL records **uncompressed** in v1

### Gates

- **P6-compress** / **P10-compress**: unit codec + pipeline DataFile roundtrip + flashback + page LZMA
- **P10-program-honest**: compress perf dual-track (bytes ↓30%, scan ≤1.25×)

## References

- [ADR-002](002-write-pipeline.md), [ADR-024](024-kernel-partial-unfreeze.md), [ADR-028](028-flashback-timechain-spec.md)
- 7-Zip C source: `7zip-main/C/LzmaLib.c` (Igor Pavlov, Public Domain)
