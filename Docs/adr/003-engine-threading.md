# ADR-003: Engine threading model

## Decision (P7)

`ebtree::Engine` supports **concurrent read** API calls (`Get`, `Scan`, `GetAsOf`, `ScanAsOf`) from multiple threads. Write operations (`Put`, `Delete`, `Flush`, `GroupCommit`, `Checkpoint`) must not run concurrently with each other; they are mutex-safe against concurrent readers.

Each `ShardEngine` serializes via `std::shared_mutex`: shared lock for reads, exclusive lock for writes. Multi-shard `Engine` routes point ops to one shard; `Scan` fans out sequentially per shard (each shard under shared lock).

## Rationale

P3–P6 used single-threaded API. P7 enables read concurrency with mmap Pin/Unpin under shared lock and RCU epoch rotation on checkpoint (ADR-012).

## See also

- ADR-012 concurrent reads
- ADR-013 background flush (worker thread calls `Flush` with exclusive lock)
