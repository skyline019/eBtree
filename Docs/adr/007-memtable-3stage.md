# ADR-007: 3-stage MemTable pipeline

## Buffers

| Stage | Name | Role |
|-------|------|------|
| 1 | `active_` | Accept Put/Delete |
| 2 | `immutable_` | Rotated snapshot awaiting flush |
| 3 | `flushing_` | Flusher target being persisted |

## Rotate on Flush/Checkpoint

1. `active_.Swap(&immutable_)`
2. `immutable_.Swap(&flushing_)`
3. `Flusher::Flush` consumes `flushing_`, then `flushing_.Clear()`

## Read overlay order

`active_` → `immutable_` → `flushing_` → `committed_` (newest wins).

## Invariants

- I-MT1: After rotate-for-flush, `immutable_` is empty (content in `flushing_`)
- I-MT2: Rotate preserves keys (I-MT1 test)
- I-MT3: Checkpoint clears all three buffers; `committed_` is sole durable source

## Threading

Single-threaded under Engine `mu_` (ADR-003). Structure ready for future background flush.
