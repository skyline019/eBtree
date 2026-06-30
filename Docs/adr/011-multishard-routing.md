# ADR-011: Multi-shard vShard routing

## Decision

- `EngineOptions::shard_count` legal values: **1, 4, 16, 256** (`ValidateShardCount`).
- Each shard owns `shard{N}.{wal,data,super,tlog,gcmeta,pages}` under the engine path.
- `RouteShard(key, shard_count)` uses **FNV-1a 32-bit** hash modulo shard count (stable across runs).
- `RoutingTable` (`alignas(64)`, 256 slots) precomputes shard ids for single-byte key prefixes when `shard_count == 256`.
- `Engine` uses **lazy shard init**: `Open` allocates placeholder slots; `EnsureShard` creates `ShardEngine` on first access.
- Put/Get/Delete route by key; Scan uses parallel worker pool and merges sorted results.
- Per-shard SuperBlock, WAL, T-Log, and GC are independent.

## Invariants

- I-SH1: same key always maps to one shard.
- I-SH2: route hash stable for 16 shards.
- I-SH3: 256-shard smoke correctness.
- I-SH4: 256-shard Open does not eagerly create all shards.

## Forbidden

`cross_shard_duplicate_key` — a key must not appear in multiple shards.

## Rationale

Horizontal scale-out without changing single-shard semantics. Lazy init keeps 256-shard Open fast; parallel Scan preserves range semantics across shards.
