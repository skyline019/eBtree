# ADR-020: kSync-B DurabilityCoordinator + WalFsyncCoordinator

## Status

Accepted (2026-06)

## Context

kSync Put held `rw_mu_` through WAL fsync, preventing concurrent writers from sharing a single fsync and blocking concurrent reads during durability wait (ADR-012).

## Decision

1. Split kSync write into **Append + memtable** (under `rw_mu_`) and **Await(lsn)** via `WalFsyncCoordinator`.
2. Put returns only after `flushed_lsn >= put_lsn`; concurrent Puts on the same shard may merge into one `WalWriter::Fsync()`.
3. Track `fsync_batch_total`, `fsync_waiter_total`, and `fsync_merge_ratio` in `EngineStats`.

## Invariants preserved

- Order: Append → memtable → Await(lsn) (equivalent to core_rules kSync semantics).
- I-D1 WalPrecedesDataFile unchanged (flush/checkpoint paths).
- Concurrent reads proceed while threads Await fsync (shared lock not held).

## Consequences

- Multi-thread kSync bench reports merge ratio > 1 on NVMe/Linux; single-thread Windows remains fsync-bound.
- `write_throughput_bench` includes N-thread kSync segment.
