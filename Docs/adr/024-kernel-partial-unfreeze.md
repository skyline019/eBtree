# ADR-024: Kernel Partial Unfreeze (Phase 2 Whitelist)

## Status

Accepted — Phase 2 partial unfreeze of `cpp/` for attestation exports and one write-path hook; Phase 3c adds read-path scan lock-scope fix.

## Context

Phase 1 kept the kernel frozen; RAR recovery attestation duplicated shard introspection in `tools/ebtree_audit`. SQL attestation included `tools/` headers. kGroup op_log durable boundaries require notification after `GroupCommit`.

## Decision

### Allowed changes (whitelist)

| API | Location | Class |
|-----|----------|-------|
| `RecoveryShardSnapshot` / `RecoverySnapshot` | `recovery_state.h` | Read-only |
| `Engine::RecoverySnapshot()` | `engine.h` | Read-only |
| `Engine::AttestExport()` | `engine_attest.h` | Read-only probes |
| `ShardEngine::wal_corrupt()` / `lazy_root_corrupt()` | `shard_engine.h` | Read-only |
| `ReadTierToString` / `ShardRecoveryStateToString` | `read_tier.h`, `recovery_state.h` | Read-only |
| `GroupCommitObserver` callback | `engine.h` | **Only allowed write-path hook** |

### Phase 3c supplement (read-path only)

| Change | Location | Class |
|--------|----------|-------|
| Release `rw_mu_` before `ResolveScanValues` | `scan_resolver.cc` | Read-path concurrency fix |

Rationale: SQL nested subqueries (2-level correlated `EXISTS`) call `Engine::Scan` multiple times on the same thread. Holding `std::shared_mutex` shared lock through `ResolveScanValues` can deadlock when the scan path re-enters (non-recursive shared lock). This does **not** change WAL/memtable/flush semantics or scan results.

Regression: `SqlComplex.NestedExists` and `ReadResolverTest.SequentialScansSameThread`.

Phase 3d SQL work (CTE/SET OP/WINDOW exec, session txn journal, subquery hardening) stays in `sql/` — **no** additional kernel whitelist entries.

### Phase 4 supplement (read-path / background heal)

| Change | Location | Class |
|--------|----------|-------|
| `BackgroundSummaryValidator` | `summary_validator.h` | Post-open async summary drift check + `RepairSummary()` |
| `BTreeIndex::SummaryDrifted()` | `btree.h` | Read-only summary probe |

Rationale: `text.txt` requires post-open async internal-node summary validation. Validator reuses existing `SummaryHealer` repair path; does not alter WAL/memtable/flush ordering.

Regression: `EbSummaryAsyncHeal.*`, `EbPipelineRto.FastOpenBadBlockFallback`.

### Phase 6–8 supplements (compression + index)

| Change | Location | Class |
|--------|----------|-------|
| `DataFile` codec append/read | `datafile.cc` | Format flag only; flush order unchanged |
| `ValueCodec` LZ4 | `value_codec.cc` | Compress at append when `compress_values` |
| Leaf prefix keys | `paged_btree.cc` | Page format bit; old pages readable |
| Page block compress | `page_file.cc` | Optional at checkpoint |
| Index scan keys | SQL executor | Ordinary Put/Scan; no WAL change |
| `EngineStats` compress counters | `config.h` | Observability |

### Phase 10 supplement (7-Zip LZMA)

| Change | Location | Class |
|--------|----------|-------|
| `ebtree_lzma` static lib | `cmake/7zip_lzma.cmake` | Vendored 7-Zip C LzmaLib |
| `lzma_codec` wrapper | `lzma_codec.cc` | FastValue / PageBlock presets |
| C2 codec=3 write | `value_codec.cc`, `datafile.cc` | Format flag; codec=1 RLE read-only |
| C4 wrapped pages | `page_file.cc` | Variable-length stored pages |

Regression: `LzmaCodec.*`, `ValueCodec.*`, `EbPageFile.LzmaWrappedPagesRoundTrip`, `CompressDatafile.*`, `EbPipelineCompressPerf.*`.

- Public `*ForTest`, `Corrupt*`, `*Internal`, `committed_mut`, `mutable_stats` expansion
- WAL / memtable / flush / balanced batch semantics changes
- Perf regression on `pipeline/perf_regression` gate

### Merge gates (kernel PRs)

- `unit`, `failure`, `pipeline`, `audit`, `sql` suites green
- ADR-024 checklist in PR description

## Consequences

- Audit recovery layer delegates to `Engine::AttestExport`
- SQL attestation uses `ebtree/engine/engine_attest.h` only (no `tools/` include)
- kGroup SQL op_log uses `SetGroupCommitObserver` to flip durable entries

## References

- [ADR-022](022-recovery-attestation-report.md), [ADR-023](023-phase1-sql-attestation.md)
