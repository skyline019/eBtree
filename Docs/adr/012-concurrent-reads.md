# ADR-012: Concurrent read API

## Decision

`Engine::Get`, `Scan`, `GetAsOf`, and `ScanAsOf` may be invoked concurrently from multiple threads on the same instance. `Put`, `Delete`, `Flush`, `GroupCommit`, and `Checkpoint` remain write operations and must not run concurrently with each other; they are safe relative to concurrent readers via per-shard `std::shared_mutex`.

Each `ShardEngine` uses **shared lock** for reads and **exclusive lock** for writes. Read paths hold the shared lock while consulting MemTable stages, `committed_`, and mmap-pinned DataFile windows.

## Rationale

P7 opens read scalability without changing write pipeline semantics. mmap RCU (ADR-010) plus shared_mutex gives readers a stable view while writers rotate memtables and flush.

## Constraints

- Cross-shard `Scan` fan-out remains single-threaded inside `Engine` (one shard at a time per call).
- Concurrent readers do not trigger deferred WAL replay (`wal_replay_deferred_total` must stay 0).
- Large DataFile recovery uses sequential mmap windows (`PinWindow` + `LoadRecordsIncremental`).

## Tests

- `EbComplexConcurrentRead.ParallelGetWhileWriting`
- `EbInvariantConcurrency.ConcurrentReadDoesNotDeferWal`
- `EbFailureConcurrentChaos.ManyReadersDuringWrites`
