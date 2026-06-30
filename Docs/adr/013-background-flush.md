# ADR-013: Background flush worker

## Decision

Each `ShardEngine` may host a `BackgroundFlushWorker` when `EngineOptions::background_flush` is true (default **false**). The worker polls memtable size; when `memtable` entries ≥ `memtable_flush_threshold_keys` (default 4096), it calls `ShardEngine::Flush()` on the shared flush path (`RotateMemTableForFlush` + `Flusher::Flush`).

`ShardEngine` destructor and `Engine` teardown **join** the worker and drain via `Stop()` before closing files.

## Rationale

Decouples write latency from flush work while preserving a single flush implementation (no duplicate write logic). Default off keeps P6/P7 tests deterministic unless explicitly enabled.

## Options

```cpp
EngineOptions opts;
opts.background_flush = true;
opts.memtable_flush_threshold_keys = 4096;
```

## Tests

- `EbPipelineBackgroundFlush.AutoFlushWithoutManualCall`
- `EbPipelineBackgroundFlush.SyncPutSurvivesDestroy`
- `EbFailureConcurrentChaos.BackgroundFlushWithReaders`
