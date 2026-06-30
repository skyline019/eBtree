# ADR-010: RCU mmap read window

## Decision

DataFile reads during recovery and flashback use **memory-mapped views** via `MmapWindow` / `MmapWindowManager` on Windows (`CreateFileMapping` + `MapViewOfFile`).

- `DataFile::LoadRecordsFromView` parses records from a mapped buffer; results must match stream load (I-MM1).
- `MmapWindowManager::Pin` / `Unpin` track reader references; `RotateEpoch` remaps after checkpoint (I-MM2).
- Write path remains append-only via `ofstream`; mmap handles use `FILE_SHARE_READ | FILE_SHARE_WRITE`.

## Rationale

Fast-open RTO for 10k+ keys benefits from zero-copy parsing. RCU epoch rotation prepares P7 concurrent readers without changing the single-threaded caller model (ADR-003).

## Forbidden

Tests forbid `mmap_read_uses_fallback`: recovery must not silently fall back to uncached ifstream when mmap is available.
