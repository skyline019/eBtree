# ADR-006: Active/Reclaim GC regions

## MVP (single shard)

- Sidecar file `shard0.gcmeta` with dual 4KB slots (epoch + CRC)
- `active_generation` toggles 0/1 on swap
- `reclaim_generation` stored in `GcMetaCritical.reserved[0]` — generation being reclaimed
- DataFile records tag `reserved[0] = generation` on append

## Swap trigger

When `datafile_size >= gc_reclaim_threshold_bytes` (default 0 = disabled), `RegionManager::SwapRegions()` runs at the **start** of `FlushInternal` before appending with the new generation.

## Visibility

On open, `DataFile::LoadAll(..., reclaim_generation)` skips records whose generation equals `reclaim_generation`. Reclaimed-generation records are invisible after reopen even if physically present. Latest LSN wins per key among visible records.

## Threading

Swap under Engine `mu_` during Flush/Put post-hook. No pause (single-threaded).

## Invariants

- I-GC1: LoadAll does not return keys from non-visible generation after swap + reopen
